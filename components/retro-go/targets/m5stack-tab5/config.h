// Target definition
#include <time.h>
#define RG_TARGET_NAME             "M5STACK-TAB5"

// Board initialization (I2C bus, IO expanders, charging), called very early by rg_system_init()
void rg_tab5_init(void);
#define RG_TARGET_INIT()            rg_tab5_init()
bool rg_tab5_wifi_prepare(void);
#define RG_TARGET_NETWORK_PREPARE() rg_tab5_wifi_prepare()
// Real time clock (see rg_system_load_time/rg_system_save_time), the time is UTC
bool rg_tab5_rtc_read(time_t *utc);
bool rg_tab5_rtc_write(time_t utc);
#define RG_TARGET_RTC_READ(utc)     rg_tab5_rtc_read(utc)
#define RG_TARGET_RTC_WRITE(utc)    rg_tab5_rtc_write(utc)

// Battery (INA226 power monitor): level %, volts, charging. Driver 3 is the target's own function.
bool rg_tab5_battery_read(float *level, float *volts, bool *charging);
#define RG_BATTERY_DRIVER           3
#define RG_TARGET_BATTERY_READ(level, volts, charging) rg_tab5_battery_read(level, volts, charging)

// I2C (system bus: IO expanders, codecs, touch, battery monitor, RTC, IMU)
#define RG_GPIO_I2C_SDA             GPIO_NUM_31
#define RG_GPIO_I2C_SCL             GPIO_NUM_32
#define RG_I2C_USE_MASTER_DRIVER    1   // The touch and codec drivers need the new i2c master driver

// Storage
#define RG_STORAGE_ROOT             "/sd"
#define RG_STORAGE_SDMMC_HOST       SDMMC_HOST_SLOT_0
#define RG_STORAGE_SDMMC_SPEED      SDMMC_FREQ_HIGHSPEED
#define RG_STORAGE_SDMMC_WIDTH      4
#define RG_STORAGE_SDMMC_LDO_CHAN   4   // LDO_VO4 powers the SDMMC IO on the Tab5

// Audio
#define RG_AUDIO_USE_INT_DAC        0   // 0 = Disable, 1 = GPIO25, 2 = GPIO26, 3 = Both
#define RG_AUDIO_USE_EXT_DAC        1   // 0 = Disable, 1 = Enable
#define RG_AUDIO_CODEC_ES8388       1
#define RG_GPIO_SND_I2S_MCLK        GPIO_NUM_30

// Video (MIPI-DSI, see drivers/display/mipi_dsi.h)
#define RG_GAMEPAD_USB_HID          1
#define RG_GAMEPAD_BLE_HID          1
#define RG_TOUCH_ST712X             1
#define RG_SCREEN_DRIVER            2
#define RG_GPIO_LCD_BCKL            GPIO_NUM_22
#define RG_SCREEN_HOST              0
#define RG_SCREEN_SPEED             0
#define RG_SCREEN_BACKLIGHT         1
#define RG_SCREEN_WIDTH             424
#define RG_SCREEN_HEIGHT            240
#define RG_SCREEN_ROTATE            0
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 0}
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0}

// SD card (SDMMC slot 0, 4-bit)
#define RG_GPIO_SDSPI_D0            GPIO_NUM_39
#define RG_GPIO_SDSPI_D1            GPIO_NUM_40
#define RG_GPIO_SDSPI_D2            GPIO_NUM_41
#define RG_GPIO_SDSPI_D3            GPIO_NUM_42
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_43
#define RG_GPIO_SDSPI_CMD           GPIO_NUM_44

// External I2S codec (ES8388)
#define RG_GPIO_SND_I2S_BCK         27
#define RG_GPIO_SND_I2S_WS          29
#define RG_GPIO_SND_I2S_DATA        26
