# Wuming Tech Xingzhi 1.54 METAL (Wi-Fi)

## Overview
Wuming Tech Xingzhi 1.54 METAL (Wi-Fi) is an upgraded version of the Xingzhi 1.54 molded design. It features a 1.54-inch LCD and a CST816 touch controller. Touch replaces physical buttons, the enclosure is aluminum, and both interaction and build quality are improved.

>### Button / Touch Operations
>- **Power on**: From off, hold the power button for 3 seconds (legacy hardware: 1 second)
>- **Power off**: From on, hold the power button for 5 seconds (legacy hardware: no auto shutdown when USB is connected)
>- **Wake / interrupt**: During normal conversation, tap the center touch zone
>- **Re-provision Wi-Fi**: Within 1 second after boot, tap the center touch zone to reboot into provisioning
>- **Volume up**: Tap the right touch zone to increase volume; hold 2 s for continuous increase
>- **Volume down**: Tap the left touch zone to decrease volume; hold 2 s for continuous decrease

>### Sleep Operations
>- **Light sleep**: After 60 s idle, enter light sleep (screen brightness ~1%)
>- **Deep sleep**: After 300 s idle, enter deep sleep (auto power off)
>- **Wake**: In light sleep, tap the center touch zone to wake (brightness restored)

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
- `Xiaozhi Assistant` → `Board Type` → `Wuming Tech Xingzhi 1.54 METAL (Wi-Fi)`
```

**Build**

```ba
idf.py build
```

**Flash and open serial monitor**

```bash
idf.py build flash monitor
```
