# ESP-BOX-3

## Introduction

<div align="center">
    <a href="https://github.com/espressif/esp-box"><b> GitHub </b></a>
</div>

ESP-BOX-3 is Espressif's AIoT dev kit with ESP32-S3-WROOM-1, 2.4-inch 320x240 ILI9341 display, dual-mic array, offline wake word, and device-side AEC.

## Hardware

- **SoC**: ESP32-S3-WROOM-1 (16MB Flash, 8MB PSRAM)
- **Display**: 2.4-inch IPS LCD (320x240, ILI9341)
- **Audio**: ES8311 codec + ES7210 dual-mic ADC
- **Audio**: Device-side AEC supported
- **Buttons**: Boot (click/double-click)
- **Other**: USB-C power and data

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
- `Xiaozhi Assistant` → `Board Type` → `ESP BOX 3`

### UI style

ESP-BOX-3 supports multiple UI styles via menuconfig:

- `Xiaozhi Assistant` → `Select display style`

#### Options

##### Emote animation style (recommended)
- **Option**: `USE_EMOTE_MESSAGE_STYLE`
- **Features**: Custom `EmoteDisplay` with rich emoji, eye animation, status icons
- **Use case**: Assistant with expressive UI
- **Class**: `emote::EmoteDisplay`

**Important**: This style needs custom assets:
1. `Xiaozhi Assistant` → `Flash Assets` → `Flash Custom Assets`
2. `Xiaozhi Assistant` → `Custom Assets File` → asset URL

##### Default message style
- **Option**: `USE_DEFAULT_MESSAGE_STYLE` (default)
- **Features**: Standard message UI
- **Class**: `SpiLcdDisplay`

##### WeChat message style
- **Option**: `USE_WECHAT_MESSAGE_STYLE`
- **Features**: WeChat-like chat bubbles
- **Class**: `SpiLcdDisplay`

### Audio

#### Device-side AEC
- `Xiaozhi Assistant` → `Enable Device-Side AEC` → enable

ESP-BOX-3 hardware supports on-device AEC to reduce speaker bleed into the mic.

**Runtime toggle**: Double-click Boot to enable/disable AEC when idle.

> Device-side AEC needs a clean speaker reference path and good mic/speaker isolation. ESP-BOX-3 is designed for this.

### Wake word

- `Xiaozhi Assistant` → `Wake Word Implementation Type`

Recommended: **Wakenet model with AFE** (`USE_AFE_WAKE_WORD`).

Press `S` to save, `Q` to quit.

**Build**

```bash
idf.py build
```

**Flash**

Connect ESP-BOX-3 and run:

```bash
idf.py flash monitor
```

## Buttons

### Boot

#### Click
- **Provisioning**: Enter WiFi config mode
- **Idle**: Start conversation
- **In conversation**: Interrupt or stop

#### Double-click (with device AEC)
- **Idle**: Toggle AEC on/off

## FAQ

### Why device-side AEC?
It removes speaker echo locally so voice commands work during music or TTS playback.

### Emote style not showing?
Ensure custom assets URL is configured and the device can download them.

### Factory reset?
Hold Boot for 3+ seconds to clear settings and reboot.
