# M5Stack Tab5

Port for the [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5): ESP32-P4 (dual core RISC-V, 360 MHz), 32 MB PSRAM,
16 MB flash, 5" 720x1280 MIPI-DSI display. The ESP32-C6 radio co-processor is not used yet.

**Status: compiles with ESP-IDF 6.1 (all ten emulator cores and apps). It has not been run on hardware.**
Everything below describes intent and what is known to be unverified.

## Building

```
python rg_tool.py --target m5stack-tab5 build-img
```

Requirements and caveats:

- ESP-IDF **6.1**. Registry components (`espressif/esp_lcd_st7121`, `esp_lcd_touch_st7123`, `esp_codec_dev`,
  `usb_host_hid`) are fetched automatically by the component manager. ESP-IDF 5.4.2 is the fallback if 6.1 turns
  out to have problems on the device.
- The sdkconfig selects ESP32-P4 **revision below v3** (`CONFIG_ESP32P4_SELECTS_REV_LESS_V3`). Chip revisions below
  and above v3 are mutually exclusive in ESP-IDF 6.1: a bootloader built for one refuses to boot on the other.
  Check the chip revision printed by the bootloader and change the option if yours is v3.
- **PSRAM voltage:** ESP-IDF 6.1 powers the PSRAM at a fixed 1.8 V, ESP-IDF 5.4.2 used 1.9 V.
  M5Stack's own demo is built with 5.4.2. If the board fails to boot or PSRAM is unstable, try 5.4.2 first.

## Hardware used

| Function | Implementation |
|---|---|
| Display | ST7121 (ST7123 is not tested) over 2-lane MIPI-DSI, 70 MHz pixel clock, double buffered RGB565. `drivers/display/mipi_dsi.h` |
| Scaling | Emulators draw to a 424x240 canvas, the PPA scales it 3x and rotates it to landscape (270 degrees CCW, an assumption to verify) |
| Touch | ST7123/ST7121 touch controller (I2C 0x55). Gestures drive the menus: drag = d-pad, tap = A, long press = B. In game only the top-left corner (MENU) reacts. `drivers/input/touch_st712x.c` |
| USB gamepads | USB-A host (5 V from the IO expander). HID gamepads/joysticks and boot keyboards, hot-plug, up to 4 devices merged. `drivers/input/usb_gamepad.c` |
| Audio | ES8388 codec over I2S (MCLK on GPIO30), configured through `esp_codec_dev`. Volume is done in software |
| Storage | SD card, SDMMC slot 0, 4-bit, powered from on-chip LDO channel 4 |
| IO expanders | Two PI4IOE5V6408 (0x43, 0x44): LCD/touch reset, speaker amp, USB 5 V, charging, Wi-Fi power. `drivers/board/m5stack_tab5.c` |

Pin assignments come from M5Stack's [M5Tab5-UserDemo](https://github.com/m5stack/M5Tab5-UserDemo) BSP (Apache-2.0).
The Tab5 has no hardware buttons, so input is USB or touch only. Plug a gamepad in to play.

### USB gamepad button layout

Button 1-4 (west, south, east, north on most pads) map to Y, B, A, X, 5/6 and 7/8 to L/R, 9 to Select, 10 to Start,
13 to Menu, 14 to Option. This matches DualShock 4, DualSense and most DInput pads. Not handled: Xbox pads (not HID
class), and the Switch Pro Controller needs a USB handshake that isn't implemented. There is no remapping UI yet.

## Emulators

All cores in `retro-core` (nofrendo, gnuboy, smsplus, pce-go, handy, snes9x, gw-emulator) plus prboom-go, gwenesis
and fmsx build for the ESP32-P4. Review notes, none of which could be measured without the device:

- The cores contain no chip specific code. The only Xtensa-specific parts of Retro-Go (overclocking, internal DAC)
  are compiled out on the P4.
- Auto frameskip starts at 1 (no frames skipped) and only rises if the emulator falls below 96% speed, which the
  360 MHz cores are expected to avoid. Profile before touching it.
- **snes9x uses unaligned 16/32-bit loads** (`FAST_LSB_WORD_ACCESS`). A separate PR replaces them with `memcpy`
  based helpers. It should land before relying on snes9x on RISC-V, where misaligned accesses can trap or be slow.
- gwenesis (68k RAM/ROM macros, VDP pattern fetch), gnuboy and smsplus also cast byte pointers to wider types. They
  already run on other targets, but if misaligned accesses prove to be a problem on the P4 these are the places to look.
- Display path: the CPU scaler in `rg_display.c` produces the 424x240 canvas and the PPA upscales it. Feeding the
  emulator surface straight to the PPA (skipping the CPU scaler) and integer scaling are the obvious next steps.

## Not done

Battery gauge (INA226), RTC (RX8130), microphone (ES7210), Wi-Fi/Bluetooth through the C6, touch in the on-screen
keyboard, and a tap-on-row menu selection.
