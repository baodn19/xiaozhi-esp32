# ESP-VoCat

## Introduction

<div align="center">
    <a href="https://oshwhub.com/esp-college/echoear"><b> LCSC Open Source </b></a>
</div>

ESP-VoCat is an AI dev kit with ESP32-S3-WROOM-1, 1.85-inch QSPI round touch display, dual-mic array, offline wake word, and sound-source localization. See the [LCSC project](https://oshwhub.com/esp-college/echoear).

## Configuration and build

**Set target to ESP32S3**

```bash
idf.py set-target esp32s3
```

**Open menuconfig**

```bash
idf.py menuconfig
```

Configure:

### Basic
- `Xiaozhi Assistant` → `Board Type` → `Espressif ESP-VoCat`

### UI style

ESP-VoCat supports multiple UI styles:

- `Xiaozhi Assistant` → `Select display style`

#### Options

##### Emote animation style (recommended)
- **Option**: `USE_EMOTE_MESSAGE_STYLE`
- **Features**: Custom `EmoteDisplay`
- **Class**: `emote::EmoteDisplay`

**Important**: Custom assets required:
1. `Xiaozhi Assistant` → `Flash Assets` → `Flash Custom Assets`
2. `Xiaozhi Assistant` → `Custom Assets File` → asset URL

##### Default message style
- **Option**: `USE_DEFAULT_MESSAGE_STYLE` (default)
- **Class**: `SpiLcdDisplay`

##### WeChat message style
- **Option**: `USE_WECHAT_MESSAGE_STYLE`
- **Class**: `SpiLcdDisplay`

> ESP-VoCat uses 16MB Flash; use the dedicated partition table for app, OTA, and assets.

Press `S` to save, `Q` to quit.

**Build**

```bash
idf.py build
```

**Flash**

Connect ESP-VoCat (**power on**) and run:

```bash
idf.py flash monitor
```
