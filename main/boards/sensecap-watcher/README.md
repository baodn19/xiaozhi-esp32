# Build Commands

## One-Click Build

```bash
python scripts/release.py sensecap-watcher
```

## Manual Build Configuration

```bash
idf.py set-target esp32s3
```

**Configuration**

```bash
idf.py menuconfig
```

Select board

```
Xiaozhi Assistant -> Board Type -> SenseCAP Watcher
```

Additional Watcher options to set in menuconfig:

```
CONFIG_BOARD_TYPE_SEEED_STUDIO_SENSECAP_WATCHER=y
CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/v2/32m.csv"
CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH=y
CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT=n
CONFIG_IDF_EXPERIMENTAL_FEATURES=y
```

## Build and Flash

```bash
idf.py -DBOARD_NAME=sensecap-watcher build flash
```

Note: If the device shipped with SenseCAP firmware (not Xiaozhi), be careful with flash partition addresses to avoid erasing SenseCAP Watcher device info (EUI, etc.). Recovery to SenseCAP firmware may then fail to connect to SenseCraft. Back up required device information before flashing!

You can back up factory information with:

```bash
# firstly backup the factory information partition which contains the credentials for connecting the SenseCraft server
esptool.py --chip esp32s3 --baud 2000000 --before default_reset --after hard_reset --no-stub read_flash 0x9000 204800 nvsfactory.bin

```
