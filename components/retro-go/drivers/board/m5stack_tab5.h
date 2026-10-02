#pragma once

#include <stdbool.h>
#include <time.h>

// Board support for the M5Stack Tab5 (ESP32-P4). The pin meanings come from M5Stack's official BSP
// (https://github.com/m5stack/M5Tab5-UserDemo, platforms/tab5/components/m5stack_tab5).

// Initializes the I2C bus, the two PI4IOE5V6408 IO expanders and enables charging.
// Called very early by rg_system_init() through RG_TARGET_INIT().
void rg_tab5_init(void);

// Resets the LCD and the touch controller (LCD_RST and TP_RST are driven by the first expander).
void rg_tab5_reset_lcd_and_touch(void);

// Power rails and misc outputs of the expanders
void rg_tab5_set_wifi_power(bool enable);

// Powers the ESP32-C6 and connects to its ESP-Hosted firmware over SDIO. Called once by rg_network_init(), returns
// false (without retrying) if the co-processor doesn't answer.
bool rg_tab5_wifi_prepare(void);
void rg_tab5_set_usb_5v(bool enable);
void rg_tab5_set_charging(bool enable);
bool rg_tab5_headphones_detected(void);

// RX8130 real time clock, stores UTC. Read fails if the chip is missing or lost power (time invalid).
bool rg_tab5_rtc_read(time_t *utc);
bool rg_tab5_rtc_write(time_t utc);

// Battery state from the INA226 power monitor. Fails if the chip doesn't answer.
bool rg_tab5_battery_read(float *level, float *volts, bool *charging);
