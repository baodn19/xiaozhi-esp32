# Usage 

* [M5Stack Tab5 docs](https://docs.m5stack.com/zh_CN/core/Tab5)

## Quick Start

Go to [M5Burner](https://docs.m5stack.com/zh_CN/uiflow/m5burner/intro), select Tab5, search for Xiaozhi, and download the firmware.

## Basic Usage

* idf version: v5.5.2 or above (recommended: v6.0-dev)

* No dependency override needed — the project already specifies the correct `esp_video` and `esp_ipa` versions in `main/idf_component.yml`. Do NOT change the dependency versions unless you are also modifying the source code to match the older API.

For ESP32-P4 Rev <3.0 users:
Ensure your sdkconfig.defaults includes:

CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y

Otherwise flashing may fail with: `'bootloader/bootloader.bin' requires chip revision in range [v3.0 - v3.99] (this chip is revision v1.x)`

1. Build with release.py

```shell
python ./scripts/release.py m5stack-tab5
```

For manual builds, refer to `m5stack-tab5/config.json` and set the corresponding menuconfig options.

2. Build and flash

```shell
idf.py flash monitor
```

> [!NOTE]
> Enter download mode: press and hold the reset button (about 2 seconds) until the internal green LED blinks rapidly, then release.


## log

@2025/05/17 Test issues

1. listening... takes several seconds before voice input is available???
2. Brightness adjustment incorrect
3. Volume adjustment incorrect
