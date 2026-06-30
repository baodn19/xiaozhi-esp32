# Main board open source:

- GitHub: https://github.com/WMnologo/xiaozhi-esp32
- More info: [wdmomo.fun](https://www.wdmomo.fun:81/doc/index.html?file=001_%E8%AE%BE%E8%AE%A1%E9%A1%B9%E7%9B%AE/0001_%E5%B0%8F%E6%99%BAAI/002_ESP32-CGC%E5%BC%80%E5%8F%91%E6%9D%BF%E5%B0%8F%E6%99%BAAI)

# Build configuration

**Set target to ESP32:**

```bash
idf.py set-target esp32
```

**Open menuconfig:**

```bash
idf.py menuconfig
```

**Select board:**

```
Xiaozhi Assistant -> Board Type -> ESP32 CGC
```

**Select display type:**

```
Xiaozhi Assistant -> LCD Type -> "ST7735, 128*128"
```

**Build:**

```bash
idf.py build
```
