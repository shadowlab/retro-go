#pragma once

// TI INA226 power monitor (I2C 0x41 on the M5Stack Tab5) and the battery estimates built on it. Only the register
// conversions live here so that they can be tested on a host, the I2C access is in m5stack_tab5.c.
// Datasheet: https://www.ti.com/lit/ds/symlink/ina226.pdf

#include <stdbool.h>
#include <stdint.h>

#define INA226_ADDR 0x41

#define INA226_REG_CONFIG       0x00
#define INA226_REG_SHUNTVOLTAGE 0x01
#define INA226_REG_BUSVOLTAGE   0x02

// Averaging 16, 1.1 ms conversions of both channels, continuous shunt and bus (M5Stack's settings, plus the
// fixed bit 14 that the chip reads back as 1)
#define INA226_CONFIG_VALUE (0x4000 | (2 << 9) | (4 << 6) | (4 << 3) | 7)

// The Tab5's shunt resistor
#define TAB5_SHUNT_OHMS 0.005f

// Register values are signed 16 bit
float ina226_bus_volts(int16_t raw);                   // 1.25 mV per bit
float ina226_shunt_amps(int16_t raw, float shunt_ohms); // 2.5 uV per bit across the shunt

// Rough state of charge of a 2S Li-ion pack (the Tab5's NP-F550) from its voltage, 0-100.
float tab5_battery_percent(float pack_volts);
