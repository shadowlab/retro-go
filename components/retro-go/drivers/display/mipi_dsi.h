// MIPI-DSI display driver for ESP32-P4 boards (M5Stack Tab5, ST7121/ST7123 720x1280 panel).
// Like the official BSP, the panel is told apart by the touch controller: firmware 1 is an ST7121, 3 an ST7123.
//
// The emulators render into a small logical canvas (RG_SCREEN_WIDTH x RG_SCREEN_HEIGHT) through the
// regular lcd_* primitives. On sync, the PPA scales (and rotates) the canvas into the back buffer of
// the DPI panel, then the buffers are flipped. The panel is natively portrait, we present it as landscape.
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_st7121.h>
#include <esp_lcd_st7123.h>
#include <esp_lcd_io_i2c.h>
#include <esp_lcd_touch_st7123.h>
#include <esp_idf_version.h>
#include <driver/i2c_master.h>
#include "rg_i2c.h"
#include "st7123_init.h"
#include <esp_ldo_regulator.h>
#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include "drivers/board/m5stack_tab5.h"
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define LCD_PANEL_W 720
#define LCD_PANEL_H 1280
#define LCD_SCALE   (LCD_PANEL_H / RG_SCREEN_WIDTH) // 3 => 1272 of 1280 lines used
#define LCD_FB_PAD  ((LCD_PANEL_H - RG_SCREEN_WIDTH * LCD_SCALE) / 2)

static esp_lcd_panel_handle_t lcd_panel;
static ppa_client_handle_t lcd_ppa;
static SemaphoreHandle_t lcd_vsync_sem;
static uint16_t *lcd_canvas;        // RG_SCREEN_WIDTH x RG_SCREEN_HEIGHT, RGB565 little endian
static uint16_t *lcd_fb[2];
static size_t lcd_fb_size;
static int lcd_back = 1;
static bool lcd_dirty = false;
static struct {int left, top, width, height, x, y;} lcd_win;
static uint16_t lcd_line_buffer[LCD_BUFFER_LENGTH];

static bool lcd_on_vsync(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(lcd_vsync_sem, &woken);
    return woken == pdTRUE;
}

typedef enum {LCD_PANEL_UNKNOWN, LCD_PANEL_ST7121, LCD_PANEL_ST7123, LCD_PANEL_ILI9881C} lcd_panel_type_t;

// Same logic as bsp_detect_display_type() of the BSP. Needs the reset to be released and the I2C bus to be up.
static lcd_panel_type_t lcd_detect_panel(void)
{
    i2c_master_bus_handle_t bus = rg_i2c_get_bus_handle();
    if (!bus)
        return LCD_PANEL_UNKNOWN;

    if (i2c_master_probe(bus, 0x14, 50) == ESP_OK) // GT911 touch (backup address)
        return LCD_PANEL_ILI9881C;

    if (i2c_master_probe(bus, 0x55, 50) != ESP_OK)
        return LCD_PANEL_UNKNOWN;

    // ST7121/ST7123 touch controller, its firmware version register tells the two apart
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_cfg = ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
    tp_cfg.scl_speed_hz = 100000;
    lcd_panel_type_t type = LCD_PANEL_ST7123; // The BSP's fallback when the version is unreadable
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    if (esp_lcd_new_panel_io_i2c(bus, &tp_cfg, &tp_io) == ESP_OK)
#else
    if (esp_lcd_new_panel_io_i2c_v2(bus, &tp_cfg, &tp_io) == ESP_OK)
#endif
    {
        uint8_t fw_version = 0;
        if (esp_lcd_panel_io_rx_param(tp_io, 0x0000, &fw_version, 1) == ESP_OK && fw_version == 1)
            type = LCD_PANEL_ST7121;
        esp_lcd_panel_io_del(tp_io);
    }
    return type;
}

static void lcd_init(void)
{
    rg_tab5_reset_lcd_and_touch();

    lcd_panel_type_t panel_type = lcd_detect_panel();
    RG_ASSERT(panel_type != LCD_PANEL_ILI9881C, "ILI9881C (GT911 touch) Tab5 panels are not supported");
    if (panel_type == LCD_PANEL_UNKNOWN)
    {
        RG_LOGW("Panel not detected, assuming ST7121");
        panel_type = LCD_PANEL_ST7121;
    }
    const bool is_st7121 = panel_type == LCD_PANEL_ST7121;
    RG_LOGI("Initializing %s over MIPI-DSI...", is_st7121 ? "ST7121" : "ST7123");

    // Backlight
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_12_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t channel = {
        .gpio_num = RG_GPIO_LCD_BCKL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&channel));

    // DSI PHY power
    esp_ldo_channel_handle_t phy_ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = {.chan_id = 3, .voltage_mv = 2500};
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &phy_ldo));

    esp_lcd_dsi_bus_handle_t bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 965,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &bus));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = ST7121_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &io));

    // Timing as in the BSP: 70 MHz, the vertical porches differ between the two controllers.
    // Framebuffers are RGB565, the panel is fed 24-bit like the vendor BSP does.
    esp_lcd_dpi_panel_config_t dpi_cfg = ST7121_1280_720_PANEL_60HZ_DPI_CONFIG_CF(LCD_COLOR_FMT_RGB565);
    dpi_cfg.out_color_format = LCD_COLOR_FMT_RGB888;
    dpi_cfg.num_fbs = 2;
    dpi_cfg.video_timing.vsync_pulse_width = is_st7121 ? 20 : 2;
    dpi_cfg.video_timing.vsync_back_porch = is_st7121 ? 24 : 8;
    dpi_cfg.video_timing.vsync_front_porch = is_st7121 ? 200 : 220;

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 24,
    };
    st7121_vendor_config_t st7121_cfg = {.mipi_config = {.dsi_bus = bus, .dpi_config = &dpi_cfg}};
    st7123_vendor_config_t st7123_cfg = {
        .init_cmds = st7123_init_cmds,
        .init_cmds_size = sizeof(st7123_init_cmds) / sizeof(st7123_init_cmds[0]),
        .mipi_config = {.dsi_bus = bus, .dpi_config = &dpi_cfg},
    };
    if (is_st7121)
    {
        panel_cfg.vendor_config = &st7121_cfg;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7121(io, &panel_cfg, &lcd_panel));
    }
    else
    {
        panel_cfg.vendor_config = &st7123_cfg;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7123(io, &panel_cfg, &lcd_panel));
    }
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));

    lcd_vsync_sem = xSemaphoreCreateBinary();
    esp_lcd_dpi_panel_event_callbacks_t cbs = {.on_vsync = lcd_on_vsync};
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(lcd_panel, &cbs, NULL));

    void *fb0 = NULL, *fb1 = NULL;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(lcd_panel, 2, &fb0, &fb1));
    lcd_fb[0] = fb0;
    lcd_fb[1] = fb1;
    lcd_fb_size = LCD_PANEL_W * LCD_PANEL_H * 2;
    memset(fb0, 0, lcd_fb_size);
    memset(fb1, 0, lcd_fb_size);
    esp_cache_msync(fb0, lcd_fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    esp_cache_msync(fb1, lcd_fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    // The PPA and the cache sync need the canvas aligned to (and sized in multiples of) the PSRAM cache line
    const size_t cache_align = CONFIG_CACHE_L2_CACHE_LINE_SIZE;
    size_t canvas_size = (RG_SCREEN_WIDTH * RG_SCREEN_HEIGHT * 2 + cache_align - 1) & ~(cache_align - 1);
    lcd_canvas = heap_caps_aligned_calloc(cache_align, 1, canvas_size, MALLOC_CAP_SPIRAM);
    RG_ASSERT(lcd_canvas, "Out of memory for the display canvas");

    ppa_client_config_t ppa_cfg = {.oper_type = PPA_OPERATION_SRM};
    ESP_ERROR_CHECK(ppa_register_client(&ppa_cfg, &lcd_ppa));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel, true));
}

static void lcd_deinit(void)
{
    esp_lcd_panel_disp_on_off(lcd_panel, false);
}

static void lcd_set_backlight(float percent)
{
    uint32_t duty = (uint32_t)(4095.f * RG_MIN(RG_MAX(percent, 0.f), 100.f) / 100.f);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

static void lcd_set_window(int left, int top, int width, int height)
{
    lcd_win.left = left;
    lcd_win.top = top;
    lcd_win.width = width;
    lcd_win.height = height;
    lcd_win.x = lcd_win.y = 0;
}

static inline uint16_t *lcd_get_buffer(size_t length)
{
    return lcd_line_buffer;
}

// Receives pixels in big endian 565 (the format SPI panels want), the canvas is little endian.
static inline void lcd_send_buffer(uint16_t *buffer, size_t length)
{
    while (length > 0 && lcd_win.y < lcd_win.height)
    {
        int y = lcd_win.top + lcd_win.y;
        size_t n = RG_MIN(length, (size_t)(lcd_win.width - lcd_win.x));
        if (y >= 0 && y < RG_SCREEN_HEIGHT && lcd_win.left + lcd_win.x >= 0)
        {
            uint16_t *dst = lcd_canvas + y * RG_SCREEN_WIDTH + lcd_win.left + lcd_win.x;
            size_t max = RG_SCREEN_WIDTH - (lcd_win.left + lcd_win.x);
            for (size_t i = 0; i < n && i < max; ++i)
                dst[i] = (buffer[i] >> 8) | (buffer[i] << 8);
            lcd_dirty = true;
        }
        buffer += n;
        length -= n;
        lcd_win.x += n;
        if (lcd_win.x >= lcd_win.width)
        {
            lcd_win.x = 0;
            lcd_win.y++;
        }
    }
}

static void lcd_sync(void)
{
    if (!lcd_dirty)
        return;
    lcd_dirty = false;

    // The canvas is written by the CPU, the PPA msyncs its input window itself.
    ppa_srm_oper_config_t srm = {
        .in = {
            .buffer = lcd_canvas,
            .pic_w = RG_SCREEN_WIDTH,
            .pic_h = RG_SCREEN_HEIGHT,
            .block_w = RG_SCREEN_WIDTH,
            .block_h = RG_SCREEN_HEIGHT,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = lcd_fb[lcd_back],
            .buffer_size = lcd_fb_size,
            .pic_w = LCD_PANEL_W,
            .pic_h = LCD_PANEL_H,
            .block_offset_x = 0,
            .block_offset_y = LCD_FB_PAD,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_270,
        .scale_x = LCD_SCALE,
        .scale_y = LCD_SCALE,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    if (ppa_do_scale_rotate_mirror(lcd_ppa, &srm) != ESP_OK)
        return;

    // Flip, then wait for the panel to latch it so the previous buffer is free for the next frame.
    xSemaphoreTake(lcd_vsync_sem, 0);
    esp_lcd_panel_draw_bitmap(lcd_panel, 0, 0, LCD_PANEL_W, LCD_PANEL_H, lcd_fb[lcd_back]);
    xSemaphoreTake(lcd_vsync_sem, pdMS_TO_TICKS(40));
    lcd_back ^= 1;
}

const rg_display_driver_t rg_display_driver_mipi_dsi = {
    .name = "mipi-dsi",
};
