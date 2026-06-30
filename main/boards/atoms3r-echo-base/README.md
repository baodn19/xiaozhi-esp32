# M5Stack AtomS3R + Echo Base

## Quick Build

Recommended: use the release script to generate a full firmware package:

```bash
python scripts/release.py atoms3r-echo-base --name atoms3r-echo-base
```

The firmware archive is located at:

```text
releases/v2.2.6_atoms3r-echo-base.zip
```

## Manual Configuration

Set build target:

```bash
idf.py set-target esp32s3
```

Open configuration menu:

```bash
idf.py menuconfig
```

Select board:

```text
Xiaozhi Assistant -> Board Type -> AtomS3R + Echo Base
```

Configure Flash size:

```text
Serial flasher config -> Flash size -> 8 MB
```

Configure partition table:

```text
Partition Table -> Custom partition CSV file -> partitions/v2/8m.csv
```

Configure PSRAM:

```text
Component config -> ESP PSRAM -> SPI RAM config -> Mode (QUAD/OCT) -> Octal Mode PSRAM
```

Build:

```bash
idf.py build
```

## Merge Firmware

After a manual build, merge flash images with:

```bash
esptool.py --chip esp32s3 merge_bin \
    --flash_mode dio \
    --flash_freq 80m \
    --flash_size 8MB \
    0x0 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0xd000 build/ota_data_initial.bin \
    0x20000 build/xiaozhi.bin \
    0x600000 build/generated_assets.bin \
    -o AtomS3R-EchoBase-XiaoZhi-v2.2.6_0x00.bin
```

Flash the merged firmware:

```bash
esptool.py -b 1500000 write_flash -z 0 AtomS3R-EchoBase-XiaoZhi-v2.2.6_0x00.bin
```

## Usage

During normal operation, power Echo Base from the Echo Base USB-C port; the AtomS3R USB-C port is mainly for flashing.
