# Xiaozhi Yunliao S3

## Overview

Xiaozhi Yunliao S3 is a customized Xiaozhi AI product—the first mass-produced unit with a 2.8-inch eye-care display, large fonts, and a 2000 mAh battery, with many innovations and optimizations.

## Official Edition

The official edition is maintained in the main Xiaozhi AI repo and tracks upstream releases. Supports wake word, barge-in, OTA, and free 4G/Wi-Fi switching.

> ### Button Operations
>
> - **Power on**: From off, hold 1 second and release.
> - **Power off**: From on, hold 1 second and release; title bar shows "Please wait", then shuts down after 2 seconds.
> - **Wake / interrupt**: Single-click during normal conversation.
> - **Switch 4G / Wi-Fi**: Double-click within 1 s during boot or provisioning (4G module required).
> - **Toggle AEC barge-in**: After normal boot, double-click within 1 s while idle to cycle barge-in mode.
> - **Re-provision Wi-Fi**: Triple-click within 1 s while powered on to reboot into provisioning.

> ### Voice Commands
>
> - **Enable / disable AEC barge-in**: Turn off barge-in while playing music to avoid interrupting playback.
> - **Switch IPS display mode**: New Yunliao S3 units use an upgraded IPS panel; toggle display mode until the image looks correct.

## Fork Edition

The fork edition has large low-level changes and is maintained separately, with periodic merges from upstream.

> ### Why a Fork
>
> - First WeChat QR code provisioning.
> - First single-phone provisioning.
> - First QR code access to the console.
> - First Traditional Chinese, Japanese, and English UI.
> - First full voice-control mode.
> - Exclusive one-click flash scripts and other flash methods.

## Edition Comparison

> | Feature              | Official | Fork |
> | -------------------- | -------- | ---- |
> | Voice barge-in       | ✓        | ✓    |
> | 4G                   | ✓        | ✓    |
> | Auto firmware update | ✓        | X    |
> | Third-party firmware | ✓        | X    |
> | Weather standby UI   | X        | ✓    |
> | Alarm reminders      | X        | ✓    |
> | Online music         | X        | ✓    |
> | WeChat QR provisioning | X      | ✓    |
> | Single-phone provisioning | X   | ✓    |
> | QR console access    | X        | ✓    |
> | Traditional/Japanese/English UI | X | ✓ |
> | Multi-language       | Build yourself | ✓ |
> | External BT speaker/headset | ✓   | ✓    |

# Build Configuration

**Clone the project**

```bash
git clone https://github.com/78/xiaozhi-esp32.git
```

**Enter the project**

```bash
cd xiaozhi-esp32
```

**Set target to ESP32-S3**

```bash
idf.py set-target esp32s3
```

**Open menuconfig**

```bash
idf.py menuconfig
```

**Select board**

```bash
- `Xiaozhi Assistant` → `Board Type` → `Xiaozhi Yunliao-S3` → `Enable Device-Side AEC`
```

**Build**

```ba
idf.py build
```

**Flash and open serial monitor**

```bash
idf.py build flash monitor
```
