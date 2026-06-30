[Product Overview]
[] ESP32-S3 rocking-horse dev board, 1.54" LCD, Xiaozhi MUMA AI voice chatbot, N16R8
[Features]
[] Cute rocking-horse form factor; weather clock, SD video playback, AI chat; open-source firmware for kids' coding and custom features.
Xiaozhi supports wake word. Touch variant adds touch wake and interrupt.
Display: 1.54" ST7789 240x240
Product link:
https://spotpear.cn/shop/ESP32-S3-AI-1.54-inch-LCD-Display-TouchScreen-N16R8-muma-DeepSeek/sp-esp32-s3-1.54-muma-W-Bat.html

# Build Configuration

**Set build target to ESP32S3:**

```bash
idf.py set-target esp32s3
```

**Open menuconfig:**

```bash
idf.py menuconfig
```

**Select board:**

```
Xiaozhi Assistant -> Board Type -> Spotpear ESP32-S3-LCD-1.54-MUMA
```

**Build:**

```bash
idf.py build
```
