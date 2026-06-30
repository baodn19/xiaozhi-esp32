# M5Stack AtomS3 + Echo Base

## Quick Build

Recommended: use the release script to generate a full firmware package:

```bash
python scripts/release.py atoms3-echo-base --name atoms3-echo-base
```

The firmware archive is located at:

```text
releases/v2.2.6_atoms3-echo-base.zip
```

AtomS3 has no PSRAM. `config.json` disables external PSRAM with `CONFIG_SPIRAM=n` and uses the 8 MB Flash partition layout. Without PSRAM, AtomS3 + Echo Base does not support wake word detection; disable wake word and audio processing when configuring manually.

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
Xiaozhi Assistant -> Board Type -> AtomS3 + Echo Base
```

Disable wake word and audio processing:

```text
Xiaozhi Assistant -> Wake Word Implementation Type -> Disable wake word detection
Xiaozhi Assistant -> Enable Audio Noise Reduction -> Unselect
```

Configure Flash size:

```text
Serial flasher config -> Flash size -> 8 MB
```

Configure partition table:

```text
Partition Table -> Custom partition CSV file -> partitions/v2/8m.csv
```

Disable external PSRAM:

```text
Component config -> ESP PSRAM -> [ ] Support for external, SPI-connected RAM -> Unselect
```

Corresponding `sdkconfig` settings:

```text
CONFIG_SPIRAM=n
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/v2/8m.csv"
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
    -o AtomS3-EchoBase-XiaoZhi-v2.2.6_0x00.bin
```

Flash the merged firmware:

```bash
esptool.py -b 1500000 write_flash -z 0 AtomS3-EchoBase-XiaoZhi-v2.2.6_0x00.bin
```

## Usage

During normal operation, power Echo Base from the Echo Base USB-C port; the AtomS3 USB-C port is mainly for flashing.
