Hardware is based on the ESP32-S3-CAM development board; firmware is derived from `bread-compact-wifi-lcd`.
The camera module is OV2640.
Note: because the camera uses many GPIOs, it occupies ESP32-S3 USB pins 19 and 20.
See pin definitions in `config.h` for wiring.


# Build configuration

**Set target to ESP32S3:**

```bash
idf.py set-target esp32s3
```

**Open menuconfig:**

```bash
idf.py menuconfig
```

**Select board:**

```bash
Xiaozhi Assistant -> Board Type -> Bread Compact WiFi + LCD + Camera (breadboard)
```

**Build and flash:**

```bash
idf.py build flash
```
