# The original microphone is discontinued. For Taiji Pi (JC3636W518) units built after July 2025, with updated mic and screen glass, if your batch number on the label is greater than 2528, select I2S Type PDM.

# Dual-Channel Configuration

# Build Configuration

**Set target to ESP32-S3:**

```bash
idf.py set-target esp32s3
```

**Open menuconfig:**

```bash
idf.py menuconfig
```

**Select board:**

```
Xiaozhi Assistant -> Board Type -> Taiji Pi ESP32-S3

Xiaozhi Assistant -> TAIJIPAI_S3_CONFIG -> taiji-pi-S3 I2S Type -> I2S Type PDM
```

**For dual-channel:**

```

Xiaozhi Assistant -> TAIJIPAI_S3_CONFIG -> Enabel use 2 slot
```


**PSRAM configuration:**

```
component config -> ESP PSRAM -> SPI RAM config -> Try to allocate memories of WiFi and LWIP in SPIRAM firstly. If failed, allocate internal memory

```

**Build:**

```bash
idf.py build
```
