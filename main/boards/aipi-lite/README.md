# Build Commands

## One-Click Build

```bash
python scripts/release.py aipi-lite
```

## Manual Build Configuration

```bash
idf.py set-target esp32s3
```

**Configure**

```bash
idf.py menuconfig
```

Select board

```
Xiaozhi Assistant -> Board Type -> AIPI-Lite
```

## Build and Flash

```bash
idf.py -DBOARD_NAME=aipi-lite build flash
```

Note: If the device shipped with AiPi-Lite firmware (non-Xiaozhi version), be careful with flash partition addresses to avoid erasing AiPi-Lite device info (EUI, etc.). Without that data, the device may fail to connect even after restoring Xorigin firmware. Record necessary device information before flashing.

You can back up factory information with the following command

```bash
# firstly backup the factory information partition which contains the credentials for connecting the SenseCraft server
esptool.py --chip esp32s3 --baud 2000000 --before default_reset --after hard_reset --no-stub read_flash 0x9000 16384 nvsfactory.bin

```