# References:
- [Wired version](https://www.wdmomo.fun:81/doc/index.html?file=001_%E8%AE%BE%E8%AE%A1%E9%A1%B9%E7%9B%AE/0001_%E5%B0%8F%E6%99%BAAI/003_ESP32-CGC-144%E6%8F%92%E7%BA%BF%E7%89%88%E5%B0%8F%E6%99%BAAI)

- [Battery version](https://www.wdmomo.fun:81/doc/index.html?file=001_%E8%AE%BE%E8%AE%A1%E9%A1%B9%E7%9B%AE/0001_%E5%B0%8F%E6%99%BAAI/004_ESP32-CGC-144%E7%94%B5%E6%B1%A0%E7%89%88%E5%B0%8F%E6%99%BAAI)

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
Xiaozhi Assistant -> Board Type -> ESP32 CGC 144
```

**Build:**

```bash
idf.py build
```
