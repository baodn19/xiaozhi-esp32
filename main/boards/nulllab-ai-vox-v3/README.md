# NULLLAB-AI-VOX3

## Overview

AI-VOX3 is an upgraded AI VOX board—a high-performance embedded development platform for AI voice interaction. It uses the ESP32-S3-R8 with 16MB onboard Flash, rich hardware resources, and flexible expansion. Five-in-one features (AI chat / weather clock / wireless intercom / MP3 player / internet radio) plus local wake word, command recognition, and TTS make it suitable for smart home, education, and IoT. The PCB fits LEGO studs and mounts on brick set C for DIY builds. AI-VOX3 expansion and MD40 motor driver boards help developers prototype quickly and extend via many interfaces.

## Features

- ESP32-S3R8 Xtensa 32-bit LX7 dual-core up to 240MHz
- 2.4 GHz Wi-Fi (802.11 b/g/n) and Bluetooth 5 (LE) with onboard antenna
- 512 KB SRAM, 384 KB ROM, 8MB PSRAM on-chip; 16 MB Flash onboard
- Type-C for download, power, and Li-ion charging; works with mainstream dev environments
- Combined power/reset button: short press power-on or reset, long press power-off
- 1.54" 240×240 SPI LCD (ST7789) for graphical UI
- LCD FPC and OLED socket—choose OLED or color LCD
- ES8311 codec and 3W NS4150B amplifier; external speaker required
- Dual mic: onboard analog mic plus external analog mic; single-mic barge-in
- SD card slot for large storage
- BOOT button, 2 keys (GPIO46/45), WS2812B RGB for debug and status
- 8-pin GPIO header (GPIO43/44/42/48/4/3/2/1) for peripherals
- 4-pin PH2.0 for power or host communication
- AI-VOX3 expansion board for more I/O
- MD40 motor driver board for multiple motors
- Charge/boost 5V 2.4A circuit; Li-ion power with IO18 ADC battery monitoring
- ESP-IDF, Arduino IDE, AilyBlockly

## Power Button

AI-VOX3 removes the traditional reset button. Short press Power for on/reset; long press Power for off.
