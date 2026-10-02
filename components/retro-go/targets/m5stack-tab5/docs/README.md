# M5Stack Tab5

Port for the [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5): ESP32-P4 (dual core RISC-V, 360 MHz), 32 MB PSRAM,
16 MB flash, 5" 720x1280 MIPI-DSI display, Wi-Fi through the on-board ESP32-C6.

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
| Wi-Fi | ESP32-C6 co-processor through ESP-Hosted (SDIO, SDMMC slot 1, reset on GPIO15), see below |
| RTC | RX8130 (I2C 0x32), holds UTC. Read at boot by `rg_system_load_time()` (overrides the saved clock file when valid), written by `rg_system_save_time()`, which the system calls when the time jumps, for example after an NTP sync. Invalid after a power loss (VLF flag) until set again. The backup battery charging is enabled like in M5Stack's firmware. `drivers/board/rx8130.c` |
| Battery | INA226 power monitor (I2C 0x41, 5 mOhm shunt) read every 2 s by the input task (`RG_BATTERY_DRIVER 3`). The bus voltage is taken as the voltage of the 2S Li-ion pack and mapped to a percentage with a per-cell curve (estimate only, no coulomb counting); positive shunt current (more than 50 mA) means charging, the sign M5Stack's demo uses. `drivers/board/ina226.c` |
| Storage | SD card, SDMMC slot 0, 4-bit, powered from on-chip LDO channel 4 |
| IO expanders | Two PI4IOE5V6408 (0x43, 0x44): LCD/touch reset, speaker amp, USB 5 V, charging, Wi-Fi power. `drivers/board/m5stack_tab5.c` |

Pin assignments come from M5Stack's [M5Tab5-UserDemo](https://github.com/m5stack/M5Tab5-UserDemo) BSP (Apache-2.0).
The Tab5 has no hardware buttons, so input is USB or touch only. Plug a gamepad in to play.

### USB gamepad button layout

Button 1-4 (west, south, east, north on most pads) map to Y, B, A, X, 5/6 and 7/8 to L/R, 9 to Select, 10 to Start,
13 to Menu, 14 to Option. This matches DualShock 4, DualSense and most DInput pads. There is no remapping UI yet.

### Pads without a standard HID report (`usb_raw_pad.c`)

A second USB host client drives controllers that the HID driver can't, with the report layouts of the Linux `xpad` and
`hid-nintendo` drivers (`gamepad_protocols.c`, host-tested with synthetic reports). Buttons are placed by their position
on the pad like above, so A/B follow the Nintendo layout on every pad.

- **Xbox 360 wired** class controllers (vendor interface 0xFF/0x5D/1, including most third party XInput pads).
- **Xbox One / Series** controllers (0xFF/0x47/0xD0): the power-on packet is sent after the interface is claimed.
  The Guide button is not read and the acknowledgements some models want for it are not sent.
- **Switch Pro Controller** (057E:2009 over USB): the USB handshake (0x80 0x02, 0x03, 0x02, 0x04, each waiting for the
  controller's echo and repeated if it doesn't answer) and the full report mode request are sent, then the 0x30
  reports are decoded. Left stick centering uses a fixed 2048 +-700 instead of the calibration in the controller's flash;
  rumble is off.

Not handled: Xbox 360 wireless receivers, Bluetooth controllers, PlayStation 3 pads, Joy-Cons and Switch controllers from
other makers that don't use the Pro Controller's IDs. Left stick and d-pad both drive the d-pad, triggers act as L/R.
Everything here is untested on hardware.

### Bluetooth LE controllers (`ble_hid_pad.c`)

Bluetooth goes through the ESP32-C6 as well: a NimBLE host on top of ESP-Hosted's HCI transport, and `esp_hid`'s BLE HID host
for the GATT side. Controllers that expose HID over GATT are read with the same report descriptor parser as the USB ones
(Xbox One S / Series controllers in Bluetooth mode are the usual example).

- **The C6 only does Bluetooth LE.** Controllers that use Bluetooth Classic (DualShock 4, DualSense, Switch Pro, Wii
  remotes) can't be connected this way at all, use USB for those.
- **Opt-in.** Launcher > Options > *Bluetooth pads* (Off / On / Pairing). Starting the radio means resetting and booting the
  C6, so nothing is started unless the setting is on. When it is, every app starts the radio in a background task
  (it doesn't delay the game starting) and reconnects to the controllers that were paired before.
- **Pairing**: choose *Pairing*, put the controller in pairing mode, and within a minute the first device that advertises a gamepad
  appearance or the HID service is connected, bonded (just works) and stored in NVS. Switching the controller on later reconnects
  it while the setting is *On*. Turning the setting off disconnects but doesn't tear the stack down until the next boot.
- Up to 2 controllers. Untested on hardware, including how well the shared C6 transport copes with Wi-Fi and Bluetooth at once.

## Wi-Fi

`esp_wifi` forwards to the ESP32-C6 on esp-idf 6.x (`espressif/esp_hosted`, pulled by the component manager). The
sdkconfig selects ESP-Hosted's `ESP32P4_TAB5_C6_BOARD` preset, which matches M5Stack's BSP: SDIO 4-bit at 40 MHz on
CLK 12, CMD 13, D0-D3 11-8, C6 reset on GPIO15, powered by IO expander 2. ESP-Hosted is not started before `app_main`
(the C6 isn't powered yet at that point). `rg_network_init()` calls `rg_tab5_wifi_prepare()`, which resets the C6 and
connects to it. If it doesn't answer, `rg_network_init()` returns false and the launcher carries on without Wi-Fi. The SD card (slot 0)
and the C6 (slot 1) share the SDMMC controller as in ESP-Hosted's own `mcu_hosted_sdio_sdmmc_combined` example.

- **Firmware match:** the C6 ships with ESP-Hosted *slave* firmware (M5Stack's demo includes
  `ESP32C6-WiFi-SDIO-Interface-V1.4.1`) while this build uses the ESP-Hosted *host* 3.0.x. Host and slave versions
  are expected to match, so the C6 will probably need to be flashed with an ESP-Hosted 3.x SDIO slave (the `slave`
  example of esp-hosted, built for esp32c6). Not verified, and the C6 flashing procedure isn't covered here.
- **Time:** Retro-Go starts an SNTP client (`pool.ntp.org`) whenever the station gets an IP address. Each successful
  sync now saves the time right away (clock file and RTC) through `rg_system_save_time()`.
- Untested on hardware. Bluetooth through the C6 is not set up.

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

### Memory (32 MB PSRAM)

PSRAM is part of the heap (`CONFIG_SPIRAM_USE_MALLOC`): allocations above 32 KB (`SPIRAM_MALLOC_ALWAYSINTERNAL`) land
there automatically, small ones in the 768 KB of internal RAM. Nothing in the cores had to change for that:

- ROMs, save state buffers and the other large `malloc` blocks of every core end up in PSRAM. gwenesis and the
  Game & Watch core read the whole ROM into memory, the other cores load theirs the same way through `malloc`.
- DOOM's zone allocator is `malloc` backed and only purges its lump cache when an allocation fails, so with 32 MB it
  keeps far more of the WAD cached than on the ESP32 targets.
- Small hot buffers that the cores explicitly place with `MEM_FAST` (frame surfaces, the Genesis VRAM) stay in internal RAM.

Left as is: the 32 KB internal-RAM threshold and the 64-byte L2 cache line (M5Stack's demo uses 128 bytes). Both could be
tuned, but only with measurements.

### CPU cores

The main task of every emulator runs on core 0. The display task (CPU scaler, PPA, vsync), the input task and, in
pce-go, fmsx and prboom-go, the audio task are pinned to core 1. The USB host tasks are on core 1 as well so that
core 0 is left to the emulator. No core splits the emulation itself across both cores (for example the Genesis sound
chips or the SNES renderer on core 1): that needs profiling on the device and careful synchronization, and is the
most promising change if a core turns out to be too slow, most likely snes9x.

## Not done

Microphone (ES7210), Bluetooth Classic pads (not possible with the C6), touch in the on-screen
keyboard, and a tap-on-row menu selection.
