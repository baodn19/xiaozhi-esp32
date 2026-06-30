# DOIT S3 AI Companion Box

# Features
* PDM microphone
* Common-anode LED

## Button Configuration
* BUTTON3: Short press - interrupt/wake
* BUTTON1: Volume up
* BUTTON2: Volume down

## Build Configuration

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
Xiaozhi Assistant -> Board Type -> DOIT S3 AI Companion Box
```

**Adjust PSRAM configuration:**

```
Component config -> ESP PSRAM -> SPI RAM config -> Mode (QUAD/OCT) -> Octal Mode PSRAM
```

**Build:**

```bash
idf.py build
```