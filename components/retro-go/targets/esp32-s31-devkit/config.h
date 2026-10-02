// Target definition
#define RG_TARGET_NAME             "ESP32-S31-DEVKIT"

// Storage
#define RG_STORAGE_ROOT             "/sd"
// #define RG_STORAGE_SDSPI_HOST       SPI3_HOST
// #define RG_STORAGE_SDSPI_SPEED      SDMMC_FREQ_DEFAULT
#define RG_STORAGE_SDMMC_HOST       SDMMC_HOST_SLOT_1
#define RG_STORAGE_SDMMC_SPEED      SDMMC_FREQ_DEFAULT
#define RG_STORAGE_SDMMC_WIDTH      4
// #define RG_STORAGE_FLASH_PARTITION  "vfs"

// Audio
#define RG_AUDIO_USE_INT_DAC        0   // 0 = Disable, 1 = GPIO25, 2 = GPIO26, 3 = Both
#define RG_AUDIO_USE_EXT_DAC        1   // 0 = Disable, 1 = Enable

// Video
#define RG_SCREEN_DRIVER            0   // 0 = ILI9341/ST7789
#define RG_SCREEN_HOST              SPI2_HOST
#define RG_SCREEN_SPEED             SPI_MASTER_FREQ_40M // SPI_MASTER_FREQ_80M
#define RG_SCREEN_BACKLIGHT         1
#define RG_SCREEN_WIDTH             320
#define RG_SCREEN_HEIGHT            240
#define RG_SCREEN_ROTATE            0
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 0}
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0}
// ST7789, 2" 320x240. The driver sets COLMOD and MADCTL from RG_SCREEN_ROTATION/RG_SCREEN_RGB_BGR,
// if the image is mirrored/rotated try other rotation values (0-7), if colors are swapped toggle RGB_BGR.
#define RG_SCREEN_ROTATION          1
#define RG_SCREEN_RGB_BGR           0
#define RG_SCREEN_INIT()                                                                                   \
    ILI9341_CMD(0xB2, 0x0c, 0x0c, 0x00, 0x33, 0x33);                                                       \
    ILI9341_CMD(0xB7, 0x35);                                                                               \
    ILI9341_CMD(0xBB, 0x24);                                                                               \
    ILI9341_CMD(0xC0, 0x2C);                                                                               \
    ILI9341_CMD(0xC2, 0x01, 0xFF);                                                                         \
    ILI9341_CMD(0xC3, 0x11);                                                                               \
    ILI9341_CMD(0xC4, 0x20);                                                                               \
    ILI9341_CMD(0xC6, 0x0f);                                                                               \
    ILI9341_CMD(0xD0, 0xA4, 0xA1);                                                                         \
    ILI9341_CMD(0xE0, 0xD0, 0x00, 0x03, 0x09, 0x13, 0x1C, 0x3A, 0x55, 0x48, 0x18, 0x12, 0x0E, 0x19, 0x1E); \
    ILI9341_CMD(0xE1, 0xD0, 0x00, 0x03, 0x09, 0x05, 0x25, 0x3A, 0x55, 0x50, 0x3D, 0x1C, 0x1D, 0x1D, 0x1E); \
    ILI9341_CMD(0x21); /* Invert colors, needed by most IPS ST7789 panels; remove if colors look inverted */


// Input
// Refer to rg_input.h to see all available RG_KEY_* and RG_GAMEPAD_*_MAP types
#define RG_GAMEPAD_GPIO_MAP {\
    {RG_KEY_UP,     .num = GPIO_NUM_1,  .pullup = 1, .level = 0},\
    {RG_KEY_DOWN,   .num = GPIO_NUM_2,  .pullup = 1, .level = 0},\
    {RG_KEY_LEFT,   .num = GPIO_NUM_4,  .pullup = 1, .level = 0},\
    {RG_KEY_RIGHT,  .num = GPIO_NUM_6,  .pullup = 1, .level = 0},\
    {RG_KEY_A,      .num = GPIO_NUM_15, .pullup = 1, .level = 0},\
    {RG_KEY_B,      .num = GPIO_NUM_7,  .pullup = 1, .level = 0},\
    {RG_KEY_START,  .num = GPIO_NUM_17, .pullup = 1, .level = 0},\
    {RG_KEY_SELECT, .num = GPIO_NUM_16, .pullup = 1, .level = 0},\
    {RG_KEY_MENU,   .num = GPIO_NUM_18, .pullup = 1, .level = 0},\
    {RG_KEY_OPTION, .num = GPIO_NUM_8,  .pullup = 1, .level = 0},\
}

// Battery: no sensing, charging is handled by the BQ24075 (PGOOD/CHG LEDs, no ESP IOs)
// Status LED
#define RG_GPIO_LED                 GPIO_NUM_38

// SPI Display
#define RG_GPIO_LCD_MISO            GPIO_NUM_NC
#define RG_GPIO_LCD_MOSI            GPIO_NUM_12
#define RG_GPIO_LCD_CLK             GPIO_NUM_48
#define RG_GPIO_LCD_CS              GPIO_NUM_14
#define RG_GPIO_LCD_DC              GPIO_NUM_47
#define RG_GPIO_LCD_BCKL            GPIO_NUM_39
#define RG_GPIO_LCD_RST             GPIO_NUM_9

// SD card (SDMMC slot 1, 4-bit)
#define RG_GPIO_SDSPI_D0            GPIO_NUM_20
#define RG_GPIO_SDSPI_D1            GPIO_NUM_21
#define RG_GPIO_SDSPI_D2            GPIO_NUM_22
#define RG_GPIO_SDSPI_D3            GPIO_NUM_23
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_24
#define RG_GPIO_SDSPI_CMD           GPIO_NUM_25

// External I2S DAC/amp (MAX98357A, SD_MODE tied high)
#define RG_GPIO_SND_I2S_BCK         43
#define RG_GPIO_SND_I2S_WS          42
#define RG_GPIO_SND_I2S_DATA        40
