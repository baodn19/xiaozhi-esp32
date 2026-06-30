# Product links

[Waveshare ESP32-C6-Touch-AMOLED-2.16](https://www.waveshare.net/shop/ESP32-C6-Touch-AMOLED-2.16.htm)

# Build configuration

**Clone the project**

```bash
git clone https://github.com/78/xiaozhi-esp32.git
```

**Enter the project directory**

```bash
cd xiaozhi-esp32
```

**Set build target to ESP32C6**

```bash
idf.py set-target esp32c6
```

**Open menuconfig**

```bash
idf.py menuconfig
```

**Select board**

```bash
Xiaozhi Assistant -> Board Type -> Waveshare ESP32-C6-Touch-AMOLED-2.16
```

**Build**

```ba
idf.py build
```

**Flash and open serial monitor**

```bash
idf.py build flash monitor
```

