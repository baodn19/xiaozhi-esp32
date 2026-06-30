# Product Links

```http
https://e.tb.cn/h.6Gl2LC7rsrswQZp?tk=qFuaV9hzh0k CZ356
```

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
Xiaozhi Assistant -> Board Type -> zhengchen-1.54tft-wifi
```

```

**Build:**

bash
idf.py build
```

**Flash:**
idf.py build flash monitor

Flash and show serial logs


**Generate merged firmware:**

```bash
idf.py merge-bin
```
