# This file is injected late into rg_tool.py, you can run arbitrary python code here
# For example override python variables or set environment variables with os.putenv

# Espressif chip in the device
IDF_TARGET = "esp32p4"
# .fw file format, if supported by the device
FW_FORMAT = "none"
# App partition sizes (type, subtype, size). The default sizes in rg_tool.py are too small for the RISC-V builds
# with esp-idf 6.x, mkfw would grow them anyway (with a warning). This leaves ~64K of headroom.
PROJECT_APPS = {
  'launcher':     [0, 16, 0x150000],
  'retro-core':   [0, 16, 0x130000],
  'prboom-go':    [0, 16, 0x100000],
  'gwenesis':     [0, 16, 0x120000],
  'fmsx':         [0, 16, 0x0D0000],
}
