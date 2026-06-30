# RYMCU BigSmart

This directory contains the `RYMCU BigSmart` board adaptation with the following hardware mapping:

- MCU: ESP32-S3-WROOM-1-N16R8
- Display: ST7789 (320x240, SPI)
- Touch: GT911 (I2C)
- Audio: ES8311 + ES7210 (I2S + I2C)
- IO expander: PCA9557 (I2C address `0x19`)
- Camera: GC0308 (DVP)

Reference hardware documentation:

- https://github.com/rymcu/BigSmart-Open/blob/main/docs/rymcu-bigsmart-hardware.md

## Build

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

In the menu, select:

`Xiaozhi Assistant -> Board Type -> RYMCU BigSmart`

Then run:

```bash
idf.py build
```
