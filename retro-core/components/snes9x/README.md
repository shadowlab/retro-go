# Snes9x port for Retro-Go

## Based on:

[Snes9x 2005](https://github.com/libretro/snes9x2005) (the `source/` directory), the libretro port of Snes9x 1.43 optimized
for low-power devices (see its README for the full lineage: NDSSFC, BAGSFC, CATSFC).

Last compared with upstream on 2026-10-02 (snes9x2005 at `a79dfe9`). The last functional upstream change to the emulation
source dates from 2022-04-01, and the upstream fixes checked at that time were present here (CPU emulation fixes, FF6 audio
pitch fix, Jurassic Park transparency fix, ExHiROM SRAM mapping, audio sample pacing, DSP-1 radius fix and `clz`
normalisation, Snes9x 1.60 colour operations).

## Modifications:

- Removed coprocessors and features that upstream has: SuperFX, SA-1, S-DD1, SPC7110, SETA (ST010/ST011/ST018) and cheats.
  Games that need these chips will not run. Upstream's `memmap.c` is about twice the size because of them.
- Kept: DSP-1 to DSP-4 (merged into `dsp.c`/`dsp.h` instead of separate files), C4, OBC1 and S-RTC.
- `snapshot.c`/`snapshot.h` implement save states for Retro-Go (upstream has no equivalent, libretro does it in the frontend).
- The frontend (video, audio, input, menus) is `retro-core/main/main_snes.c` instead of upstream's `libretro.c`. It allocates the
  screen and Z buffers itself.
- `spc700.c` is refactored: the direct page accessors are local functions and the file is built with `#pragma GCC optimize("O3")`.
- Only the Snes9x 1.60 colour operations are kept, upstream's optional `USE_OLD_COLOUR_OPS` path was removed.
- `apu_blargg.c` is kept but not built: `USE_BLARGG_APU` is not defined, so the SPC700 core in `apu.c`/`spc700.c` is used. If you
  enable it with ESP-IDF 6.x (GCC 15) it currently fails to build with `-Werror=shift-negative-value` in `apu_blargg.c`.
- Build flags are in `CMakeLists.txt` (`-DRIGHTSHIFT_IS_SAR -DFAST_LSB_WORD_ACCESS -DNO_ZERO_LUT -O2`).
