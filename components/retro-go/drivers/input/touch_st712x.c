#include "rg_system.h"
#include "rg_input.h"
#include "touch_st712x.h"

#if defined(RG_TOUCH_ST712X)

#include <esp_lcd_panel_io.h>
#include <esp_lcd_io_i2c.h>
#include <esp_idf_version.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_st7123.h>
#include <driver/i2c_master.h>

#include "touch_gesture.h"

#define PANEL_W 720
#define PANEL_H 1280

static esp_lcd_touch_handle_t touch;
static touch_gesture_t gesture;

void rg_touch_init(void)
{
    i2c_master_bus_handle_t bus = rg_i2c_get_bus_handle();
    if (!bus)
    {
        RG_LOGE("Touch needs the I2C master bus");
        return;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
    io_cfg.scl_speed_hz = 400000;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    if (esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io) != ESP_OK)
#else
    if (esp_lcd_new_panel_io_i2c_v2(bus, &io_cfg, &io) != ESP_OK)
#endif
    {
        RG_LOGE("Touch I/O init failed");
        return;
    }

    const esp_lcd_touch_config_t cfg = {
        .x_max = PANEL_W,
        .y_max = PANEL_H,
        .rst_gpio_num = -1, // driven by the IO expander, see rg_tab5_reset_lcd_and_touch()
        .int_gpio_num = GPIO_NUM_23,
    };
    if (esp_lcd_touch_new_i2c_st7123(io, &cfg, &touch) != ESP_OK)
    {
        RG_LOGE("Touch controller not found");
        touch = NULL;
        return;
    }
    RG_LOGI("Touch ready");
}

uint32_t rg_touch_read(void)
{
    if (!touch)
        return 0;

    esp_lcd_touch_point_data_t point;
    uint8_t count = 0;
    bool touching = false;
    int x = 0, y = 0;

    if (esp_lcd_touch_read_data(touch) == ESP_OK
        && esp_lcd_touch_get_data(touch, &point, &count, 1) == ESP_OK && count > 0)
    {
        // The panel is portrait and we present it rotated by 270 degrees CCW (see drivers/display/mipi_dsi.h),
        // so logical X runs along the physical Y axis and logical Y runs against the physical X axis.
        const int scale = 3; // LCD_SCALE in drivers/display/mipi_dsi.h
        x = (point.y - (PANEL_H - RG_SCREEN_WIDTH * scale) / 2) / scale;
        y = (PANEL_W - 1 - point.x) / scale;
        touching = true;
    }

    const touch_keys_t keys = {
        .up = RG_KEY_UP, .down = RG_KEY_DOWN, .left = RG_KEY_LEFT, .right = RG_KEY_RIGHT,
        .a = RG_KEY_A, .b = RG_KEY_B, .menu = RG_KEY_MENU,
    };
    return touch_gesture_update(&gesture, &keys, rg_input_touch_ui_active(), touching, x, y, rg_system_timer());
}

#else
void rg_touch_init(void) {}
uint32_t rg_touch_read(void) { return 0; }
#endif
