# Build Configuration Guide

This document describes how to configure and build firmware for **Movecall Moji2.0 (Xiaozhi AI Edition)**.

## 🛠 Requirements
*   **ESP-IDF Version**: v5.5
*   **Target Chip**: ESP32-C5

## 🔗 Open Hardware
This project is based on the following open hardware design:
*   **OSHWHub**: [https://oshwhub.com/movecall/moji2](https://oshwhub.com/movecall/moji2)

---

## 🚀 Build Steps

### 1. Set Build Target
Set the project target chip to ESP32-C5:
```bash
idf.py set-target esp32c5
```

### 2. Configure Board Type
Open the configuration menu to select the board:
```bash
idf.py menuconfig
```

**Navigate in the menu as follows:**
> **Xiaozhi Assistant** -> **Board Type** -> **Movecall Moji2.0 (Xiaozhi AI Edition)**

*Tip: After configuring, press **S** to save (Enter to confirm) and **Q** to exit.*

### 3. Build
Run the following command to build the project:
```bash
idf.py build
```

---

## 🔧 Useful Commands

**Clean build cache (recommended if you hit errors):**
```bash
idf.py fullclean
```

**Flash firmware:**
```bash
idf.py flash
```

**Monitor serial output:**
```bash
idf.py monitor
```
