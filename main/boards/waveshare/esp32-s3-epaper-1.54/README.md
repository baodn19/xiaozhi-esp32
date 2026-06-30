# Product links

[Waveshare ESP32-S3-ePaper-1.54](https://www.waveshare.net/shop/ESP32-S3-ePaper-1.54.htm)

```bash
esptool.py flash_id
V1: 4MB Flash, 2MB PSRAM
V2: 8MB Flash, 8MB PSRAM
```

# Build configuration

**Clone the project**

```bash
git clone https://github.com/78/xiaozhi-esp32.git
```

**Enter the project directory**

```bash
cd xiaozhi-esp32
```

**Set build target to ESP32S3**

```bash
idf.py set-target esp32s3
```

**Open menuconfig**

```bash
idf.py menuconfig
```

**Select board**

```bash
Xiaozhi Assistant -> Board Type -> Waveshare ESP32-S3-ePaper-1.54_v2
```

**Build**

```bash
python ./scripts/release.py --name esp32-s3-epaper-1.54-v1 waveshare/esp32-s3-epaper-1.54
python ./scripts/release.py --name esp32-s3-epaper-1.54-v2 waveshare/esp32-s3-epaper-1.54
```

**Flash and open serial monitor**

```bash
idf.py flash monitor
```

