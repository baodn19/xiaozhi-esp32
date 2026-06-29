# ESP-SensairShuttle

## Introduction

<div align="center">
    <a href="https://docs.espressif.com/projects/espressif-esp-dev-kits/en/latest/esp32c5/esp-sensairshuttle/index.html">
        <b> Dev board docs </b>
    </a>
    |
    <a href="#sensor--shuttleboard-support">
        <b> Sensor & <i>ShuttleBoard</i> docs </b>
    </a>
</div>

ESP-SensairShuttle is a dev board from Espressif and Bosch Sensortec for **motion sensing** and **LLM human–machine interaction**.

It uses the ESP32-C5-WROOM-1-N16R8 module with dual-band 2.4 & 5 GHz Wi-Fi 6 (802.11ax), Bluetooth® 5 (LE), Zigbee, and Thread (802.15.4).

## Sensor & _ShuttleBoard_ support

Coming soon.

## Configuration and build

Many sdkconfig options are required; use the release script.

**Build**

```bash
python ./scripts/release.py esp-sensairshuttle
```

For manual builds, see `main/boards/esp-sensairshuttle/config.json`.

**Flash**

```bash
idf.py flash
```
