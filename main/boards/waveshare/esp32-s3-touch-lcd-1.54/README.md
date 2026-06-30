# Product links

[Waveshare ESP32-S3-Touch-LCD-1.54](https://www.waveshare.net/shop/ESP32-S3-Touch-LCD-1.54.htm)
[Waveshare ESP32-S3-LCD-1.54](https://www.waveshare.net/shop/ESP32-S3-LCD-1.54.htm)

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
Xiaozhi Assistant -> Board Type -> Waveshare ESP32-S3-Touch-LCD-1.54
```

**Build**

```ba
idf.py build
```

**Flash and open serial monitor**

```bash
idf.py build flash monitor
```

