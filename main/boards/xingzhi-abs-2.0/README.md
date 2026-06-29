# Wuming Tech Xingzhi ABS 2.0

## Overview
Wuming Tech Xingzhi ABS 2.0 is a cost-effective AI voice interaction dev board. It has a 1.54-inch LCD, dedicated physical buttons, and an **ML307R 4G modem** so you can talk to LLMs without Wi-Fi.

## Key Features
- Dual network: Wi-Fi and ML307R Cat.1 4G, switchable for different scenarios
- Display: 1.54-inch 240×240 LCD with a custom UI layout tuned for the square screen
- Physical buttons: Boot, volume up, volume down; single/double/long/five-click gestures
- Expansion: Micro SD slot; optional vibration motor for haptic feedback
- Power management: battery ADC, charging detect, auto light/deep sleep
- Ecosystem: Xiaozhi ESP32 firmware, Qwen/DeepSeek models, MCP device control
- UI note: bottom emoji and text positions differ slightly from other boards due to hardware layout

## vs Aluminum Version (XINGZHI_METAL_1_54_WIFI)
| Feature | xingzhi-abs-2.0 | Aluminum version |
|---------|-----------------|------------------|
| Input | Physical buttons (Boot / Vol+ / Vol-) | CST816 touch |
| Enclosure | ABS plastic | Aluminum |

>### Button Operations
>- **Power on**: From off, hold power button 3 seconds
>- **Power off**: From on, hold power button 5 seconds
>- **Wake / interrupt**: Single-click Boot while idle or in conversation
>- **Re-provision Wi-Fi**: Within 1 s after boot, single-click Boot to reboot into provisioning
>- **Switch network**: Double-click Boot to toggle Wi-Fi / 4G
>- **SD card status**: Five-click Boot to show SD mount status on screen
>- **Volume up**: Single-click Vol+ (+10%); long-press Vol+ 2 s to max (100%)
>- **Volume down**: Single-click Vol- (-10%); long-press Vol- 2 s to mute (0%)

>### Sleep Operations
>- **Light sleep**: After 60 s idle, light sleep (screen ~1% brightness)
>- **Deep sleep**: After 300 s idle, auto power off
>- **Wake**: In light sleep, any button press wakes the device

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
- `Xiaozhi Assistant` → `Board Type` → `Wuming Tech Xingzhi ABS 2.0`
```

**Build**

```ba
idf.py build
```

**Flash and open serial monitor**

```bash
idf.py build flash monitor
```
