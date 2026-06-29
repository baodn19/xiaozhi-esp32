# ESP-Spot

## Introduction

<div align="center">
    <a href="https://oshwhub.com/esp-college/esp-spot"><b> LCSC Open Source </b></a>
</div>

ESP-Spot is a smart voice interaction box from ESP Friends with microphone, speaker, and IMU; it can run on battery. There is no screen; it has an RGB indicator and two buttons. See the [LCSC project](https://oshwhub.com/esp-college/esp-spot) for hardware details.

The reference design uses ESP32-S3-WROOM-1-N16R8 or ESP32-C5-WROOM-1-N8R8. If you use a different Flash size, update the corresponding parameters.

## Configuration and build

**Set build target**

```bash
idf.py set-target esp32s3
# or
idf.py set-target esp32c5
```

**Open menuconfig**

```bash
idf.py menuconfig
```

Configure:

- `Xiaozhi Assistant` → `Board Type` → `ESP-Spot-S3` / `ESP-Spot-C5`

Press `S` to save, `Q` to quit.

**Build**

```bash
idf.py build
```

**Flash**

```bash
idf.py flash
```

> **If the PC never finds the ESP-Spot serial port:**
> 1. Open the front cover;
> 2. Remove the PCB with the module;
> 3. Hold <kbd>BOOT</kbd> while reinserting the PCB (do not reverse polarity);
>
> ESP-Spot should enter download mode. You may need to reseat the PCB after flashing.

## Low power

ESP-Spot supports Deep Sleep. After 10 minutes idle, it enters Deep Sleep; press the key or shake the device to wake.
