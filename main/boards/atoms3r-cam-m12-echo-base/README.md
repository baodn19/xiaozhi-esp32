# AtomS3R CAM/M12 + Echo Base

## Overview

<div align="center">
    <a href="https://docs.m5stack.com/zh_CN/core/AtomS3R%20Cam"><b> AtomS3R CAM Product Page </b></a>
    |
    <a href="https://docs.m5stack.com/zh_CN/core/AtomS3R-M12"><b> AtomS3R M12 Product Page </b></a>
    |
    <a href="https://docs.m5stack.com/zh_CN/atom/Atomic%20Echo%20Base"><b> Echo Base Product Page </b></a>
</div>

AtomS3R CAM and AtomS3R M12 are IoT programmable controllers from M5Stack based on the ESP32-S3-PICO-1-N8R8 with an onboard camera. Atomic Echo Base is a voice-recognition dock designed for the M5 Atom series, integrating an ES8311 mono audio codec, MEMS microphone, and NS4150B power amplifier.

Both boards have **no display and no extra buttons**, so voice wake word is required. Use `idf.py monitor` to inspect logs when needed to confirm runtime status.

## Configuration and Build

**Set target to ESP32S3**

```bash
idf.py set-target esp32s3
```

**Open menuconfig and configure**

```bash
idf.py menuconfig
```

Configure the following options:

- `Xiaozhi Assistant` → `Board Type` → select `AtomS3R CAM/M12 + Echo Base`
- `Xiaozhi Assistant` → `IoT Protocol` → select `MCP Protocol` to enable camera recognition
- `Partition Table` → `Custom partition CSV file` → clear the existing value and enter `partitions/v2/8m.csv`
- `Serial flasher config` → `Flash size` → select `8 MB`

Press `S` to save, then `Q` to exit.

**Build**

```bash
idf.py build
```

**Flash**

Connect the AtomS3R CAM/M12 to your computer and hold the side RESET button until the green LED below the RESET button blinks.

```bash
idf.py flash
```

After flashing, press RESET once to reboot.
