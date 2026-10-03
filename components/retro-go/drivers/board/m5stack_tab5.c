#include "rg_system.h"
#include "rg_i2c.h"

#if defined(RG_TARGET_M5STACK_TAB5)

#include "m5stack_tab5.h"
#include "rx8130.h"
#include "ina226.h"

#include <driver/gpio.h>
#ifdef RG_ENABLE_NETWORKING
#include <esp_hosted.h>
#endif

// The two expanders are PI4IOE5V6408 (8 pins each) on the system I2C bus
#define EXPANDER1_ADDR 0x43 // address pin low
#define EXPANDER2_ADDR 0x44 // address pin high

#define REG_CHIP_RESET 0x01
#define REG_IO_DIR     0x03 // 1 = output
#define REG_OUT_SET    0x05
#define REG_OUT_H_IM   0x07 // 1 = high impedance
#define REG_IN_DEF_STA 0x09
#define REG_PULL_EN    0x0B
#define REG_PULL_SEL   0x0D // 1 = pull-up
#define REG_IN_STA     0x0F
#define REG_INT_MASK   0x11

// Expander 1 pins
#define EXP1_ANT_EXT_EN (1 << 0) // 1 = external antenna
#define EXP1_EXT_5V_EN  (1 << 2)
#define EXP1_LCD_RST    (1 << 4) // Open-drain like: driven low to reset, released by switching to input with pull-up
#define EXP1_TP_RST     (1 << 5)
#define EXP1_HP_DETECT  (1 << 7) // Input

// Expander 2 pins
#define EXP2_WLAN_PWR_EN (1 << 0) // Power of the ESP32-C6
#define EXP2_USB5V_EN    (1 << 3) // 5V on the USB-A host port
#define EXP2_PWROFF      (1 << 4)
#define EXP2_CHG_QC_EN   (1 << 5) // Active low
#define EXP2_USBC_DETECT (1 << 6) // Input
#define EXP2_CHG_EN      (1 << 7)

#define TP_INT_GPIO GPIO_NUM_23

static bool initialized = false;

static bool expander_write(uint8_t addr, uint8_t reg, uint8_t value)
{
    return rg_i2c_write_byte(addr, reg, value);
}

static bool expander_update(uint8_t addr, uint8_t reg, uint8_t clear_mask, uint8_t set_mask)
{
    int value = rg_i2c_read_byte(addr, reg);
    return value >= 0 && expander_write(addr, reg, (value & ~clear_mask) | set_mask);
}

static bool expander_set_output(uint8_t addr, uint8_t pin_mask, bool level)
{
    return expander_update(addr, REG_OUT_SET, level ? 0 : pin_mask, level ? pin_mask : 0);
}

void rg_tab5_init(void)
{
    if (initialized)
        return;

    if (!rg_i2c_init())
    {
        RG_LOGE("I2C init failed, the board cannot be initialized!\n");
        return;
    }

    // The sequences below are the ones from the official BSP (bsp_io_expander_pi4ioe_init).

    // Expander 1: after a reset the pins are hi-Z with pull-downs. LCD_RST has no 1.8V pull-up on the panel side and the
    // P4 cannot push 3.3V, so the reset is driven low as an output and released by switching the pin to an input with pull-up.
    expander_write(EXPANDER1_ADDR, REG_CHIP_RESET, 0xFF);
    rg_i2c_read_byte(EXPANDER1_ADDR, REG_CHIP_RESET); // Reading clears the reset flag
    expander_write(EXPANDER1_ADDR, REG_PULL_SEL, 0b01111111);
    expander_write(EXPANDER1_ADDR, REG_PULL_EN, 0b01111111);
    expander_write(EXPANDER1_ADDR, REG_OUT_SET, 0b01100110);
    expander_write(EXPANDER1_ADDR, REG_OUT_H_IM, 0b00000000);
    expander_write(EXPANDER1_ADDR, REG_IO_DIR, 0b01111111); // LCD_RST output low
    rg_task_delay(10);
    expander_write(EXPANDER1_ADDR, REG_IO_DIR, 0b01101111); // LCD_RST released
    rg_task_delay(50);

    // Expander 2
    expander_write(EXPANDER2_ADDR, REG_CHIP_RESET, 0xFF);
    rg_i2c_read_byte(EXPANDER2_ADDR, REG_CHIP_RESET);
    expander_write(EXPANDER2_ADDR, REG_IO_DIR, 0b10111001);
    expander_write(EXPANDER2_ADDR, REG_OUT_H_IM, 0b00000110);
    expander_write(EXPANDER2_ADDR, REG_PULL_SEL, 0b10111001);
    expander_write(EXPANDER2_ADDR, REG_PULL_EN, 0b11111001);
    expander_write(EXPANDER2_ADDR, REG_IN_DEF_STA, 0b01000000);
    expander_write(EXPANDER2_ADDR, REG_INT_MASK, 0b10111111);
    expander_write(EXPANDER2_ADDR, REG_OUT_SET, EXP2_WLAN_PWR_EN | EXP2_USB5V_EN);

    // Like the demo: enable charging after a short delay
    expander_set_output(EXPANDER2_ADDR, EXP2_CHG_QC_EN, false);
    rg_task_delay(50);
    expander_set_output(EXPANDER2_ADDR, EXP2_CHG_EN, true);

    initialized = true;
    RG_LOGI("Tab5 board initialized.\n");
}

void rg_tab5_reset_lcd_and_touch(void)
{
    if (!initialized)
        return;

    // Same sequence as bsp_reset_tp(): hold both resets low, release TP_RST first, then release LCD_RST.
    gpio_reset_pin(TP_INT_GPIO);

    expander_set_output(EXPANDER1_ADDR, EXP1_LCD_RST | EXP1_TP_RST, false);
    expander_update(EXPANDER1_ADDR, REG_IO_DIR, 0, EXP1_LCD_RST); // LCD_RST output (low)
    rg_task_delay(100);

    expander_set_output(EXPANDER1_ADDR, EXP1_TP_RST, true);
    expander_update(EXPANDER1_ADDR, REG_IO_DIR, EXP1_LCD_RST, 0); // LCD_RST input with pull-up (released)
    rg_task_delay(100);
}

void rg_tab5_set_wifi_power(bool enable)
{
    expander_set_output(EXPANDER2_ADDR, EXP2_WLAN_PWR_EN, enable);
}

void rg_tab5_set_usb_5v(bool enable)
{
    expander_set_output(EXPANDER2_ADDR, EXP2_USB5V_EN, enable);
}

void rg_tab5_set_charging(bool enable)
{
    expander_set_output(EXPANDER2_ADDR, EXP2_CHG_EN, enable);
}

bool rg_tab5_headphones_detected(void)
{
    int value = rg_i2c_read_byte(EXPANDER1_ADDR, REG_IN_STA);
    return value >= 0 && (value & EXP1_HP_DETECT);
}

bool rg_tab5_rtc_read(time_t *utc)
{
    static bool battery_init = false;
    if (!battery_init)
    {
        // Charge the RTC backup battery, as M5Stack's own firmware does
        int ctrl1 = rg_i2c_read_byte(RX8130_ADDR, RX8130_REG_CTRL1);
        if (ctrl1 >= 0)
            rg_i2c_write_byte(RX8130_ADDR, RX8130_REG_CTRL1, ctrl1 | RX8130_CTRL1_INIEN | RX8130_CTRL1_CHGEN);
        battery_init = true;
    }

    int flag = rg_i2c_read_byte(RX8130_ADDR, RX8130_REG_FLAG);
    uint8_t regs[7];
    if (flag < 0 || !rg_i2c_read(RX8130_ADDR, RX8130_REG_SEC, regs, sizeof(regs)))
        return false; // Not there
    if (flag & RX8130_FLAG_VLF)
    {
        RG_LOGW("RTC lost power, its time is invalid");
        return false;
    }
    return rx8130_decode(regs, utc);
}

bool rg_tab5_rtc_write(time_t utc)
{
    uint8_t regs[7];
    rx8130_encode(utc, regs);

    // The clock has to be stopped while the calendar is written
    int ctrl0 = rg_i2c_read_byte(RX8130_ADDR, RX8130_REG_CTRL0);
    int flag = rg_i2c_read_byte(RX8130_ADDR, RX8130_REG_FLAG);
    if (ctrl0 < 0 || flag < 0)
        return false;
    bool ok = rg_i2c_write_byte(RX8130_ADDR, RX8130_REG_CTRL0, ctrl0 | RX8130_CTRL0_STOP)
              && rg_i2c_write(RX8130_ADDR, RX8130_REG_SEC, regs, sizeof(regs));
    ok = rg_i2c_write_byte(RX8130_ADDR, RX8130_REG_CTRL0, ctrl0 & ~RX8130_CTRL0_STOP) && ok;
    if (ok && (flag & RX8130_FLAG_VLF))
        rg_i2c_write_byte(RX8130_ADDR, RX8130_REG_FLAG, flag & ~RX8130_FLAG_VLF); // The time is valid again
    return ok;
}

static bool ina226_read(uint8_t reg, int16_t *out)
{
    uint8_t data[2];
    if (!rg_i2c_read(INA226_ADDR, reg, data, sizeof(data)))
        return false;
    *out = (int16_t)((data[0] << 8) | data[1]); // MSB first
    return true;
}

bool rg_tab5_battery_read(float *level, float *volts, bool *charging)
{
    static bool configured = false;
    if (!configured)
    {
        const uint8_t config[2] = {INA226_CONFIG_VALUE >> 8, INA226_CONFIG_VALUE & 0xFF};
        if (!rg_i2c_write(INA226_ADDR, INA226_REG_CONFIG, config, sizeof(config)))
            return false;
        configured = true;
    }

    int16_t bus_raw, shunt_raw;
    if (!ina226_read(INA226_REG_BUSVOLTAGE, &bus_raw) || !ina226_read(INA226_REG_SHUNTVOLTAGE, &shunt_raw))
        return false;

    // The bus voltage is taken as the voltage of the 2S battery pack. Current flowing into the pack is positive,
    // this is the sign M5Stack's demo shows as charging.
    *volts = ina226_bus_volts(bus_raw);
    *level = tab5_battery_percent(*volts);
    *charging = ina226_shunt_amps(shunt_raw, TAB5_SHUNT_OHMS) > 0.05f;
    return true;
}

bool rg_tab5_wifi_prepare(void)
{
#ifdef RG_ENABLE_NETWORKING
    static int state = 0; // 0 = not tried, 1 = ready, -1 = failed
    if (state == 0)
    {
        // The C6 sits behind IO expander 2, rg_tab5_init() already powered it. ESP-Hosted resets it through its
        // reset GPIO, waits for it to boot and then brings up the SDIO link.
        rg_tab5_set_wifi_power(true);
        int err = esp_hosted_init();
        if (err == 0)
            err = esp_hosted_connect_to_slave();
        if (err != 0)
            RG_LOGE("ESP32-C6 did not respond (%d)", err);
        state = err == 0 ? 1 : -1;
    }
    return state == 1;
#else
    return false;
#endif
}

#endif // RG_TARGET_M5STACK_TAB5
