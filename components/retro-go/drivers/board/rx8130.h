#pragma once

// Epson RX8130CE real time clock (I2C 0x32), used by the M5Stack Tab5. This file only converts between time_t (UTC)
// and the chip's calendar registers so that it can be tested on a host, the I2C access is in m5stack_tab5.c.
// Datasheet: https://download.epsondevice.com/td/pdf/app/RX8130CE_en.pdf

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define RX8130_ADDR 0x32

#define RX8130_REG_SEC   0x10 // 7 registers: sec, min, hour, weekday, day, month, year (BCD)
#define RX8130_REG_FLAG  0x1D
#define RX8130_REG_CTRL0 0x1E
#define RX8130_REG_CTRL1 0x1F

#define RX8130_FLAG_VLF   (1 << 1) // Voltage low: the time is not reliable until it has been set again
#define RX8130_CTRL0_STOP (1 << 6) // Stops the clock, required while writing the calendar
#define RX8130_CTRL1_INIEN (1 << 4)
#define RX8130_CTRL1_CHGEN (1 << 5)

void rx8130_encode(time_t utc, uint8_t regs[7]);
bool rx8130_decode(const uint8_t regs[7], time_t *utc);
