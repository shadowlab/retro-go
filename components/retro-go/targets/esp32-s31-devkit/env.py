# This file is injected late into rg_tool.py, you can run arbitrary python code here
# For example override python variables or set environment variables with os.putenv

# Espressif chip in the device
IDF_TARGET = "esp32s31"
# ESP32-S31 is a preview target in esp-idf 6.1, idf.py needs --preview
IDF_PREVIEW = True
# .fw file format, if supported by the device
FW_FORMAT = "none"
