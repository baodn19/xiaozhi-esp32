# ESP-Hi

## Introduction

<div align="center">
    <a href="https://oshwhub.com/esp-college/esp-hi"><b> LCSC Open Source </b></a>
    |
    <a href="https://www.bilibili.com/video/BV1BHJtz6E2S"><b> Bilibili </b></a>
</div>

ESP-Hi is an ultra-**low-cost** AI chat robot from ESP Friends, based on ESP32-C3. It includes a 0.96-inch color display for expressions; the **robot dog supports dozens of motions**. By making full use of ESP32-C3 peripherals, minimal board hardware enables capture and playback. Software is optimized to reduce RAM and Flash use while supporting **wake word detection** and multiple peripherals in a resource-constrained design. See the [LCSC open source project](https://oshwhub.com/esp-college/esp-hi) for hardware details.

## WebUI

ESP-Hi x Xiaozhi includes a WebUI to control body motion. Connect your phone and ESP-Hi to the same Wi-Fi, then visit `http://esp-hi.local/`.

To disable, unset `ESP_HI_WEB_CONTROL_ENABLED`, i.e. uncheck `Component config` → `Servo Dog Configuration` → `Web Control` → `Enable ESP-HI Web Control`.

## Configuration and build

ESP-Hi requires many sdkconfig options; the release script is recommended.

**Build**

```bash
python ./scripts/release.py esp-hi
```

For manual builds, see `esp-hi/config.json` for menuconfig options.

**Flash**

```bash
idf.py flash
```


> [!TIP]
>
> **Servo control uses ESP-Hi's USB Type-C port**, so the PC may not connect (no flash/logs). If that happens:
>
> **Flash**
>
> 1. Disconnect power; use the head only, not the body.
> 2. Hold the ESP-Hi button while connecting to the PC.
> 
> ESP-Hi (ESP32-C3) should enter download mode. You may need to power-cycle after flashing.
>
> **View logs**
>
> Set `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`: `Component config` → `ESP System Settings` → `Channel for console output` → `USB Serial/JTAG Controller`. This disables servo control.
