# AtomEchoS3R

## Overview

AtomEchoS3R is an IoT programmable controller from M5Stack based on the ESP32-S3-PICO-1-N8R8, integrating an ES8311 mono audio codec, MEMS microphone, and NS4150B power amplifier.

This board has **no display and no extra buttons**, so voice wake word is required. Use `idf.py monitor` to inspect logs when needed to confirm runtime status.

## Configuration and Build

**Set target to ESP32S3**

```bash
idf.py set-target esp32s3
```

**Open menuconfig and configure**

```bash
idf.py menuconfig
```

Configure the following options:

- `Xiaozhi Assistant` → `Board Type` → select `AtomEchoS3R`
- `Partition Table` → `Custom partition CSV file` → clear the existing value and enter `partitions/v2/8m.csv`
- `Serial flasher config` → `Flash size` → select `8 MB`
- `Component config` → `ESP PSRAM` → `Support for external, SPI-connected RAM` → `SPI RAM config` → select `Octal Mode PSRAM`

Press `S` to save, then `Q` to exit.

**Build**

```bash
idf.py build
```

**Flash**

Connect AtomEchoS3R to your computer and hold the side RESET button until the green LED below the RESET button blinks.

```bash
idf.py flash
```

After flashing, press RESET once to reboot the device.
