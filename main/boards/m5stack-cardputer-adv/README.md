# M5Stack Cardputer Adv

M5Stack Cardputer Adv is a card-sized computer based on the ESP32-S3FN8 (Stamp-S3A).

## Hardware Specifications

| Component | Spec |
|------|------|
| MCU | ESP32-S3FN8 @ 240MHz |
| Flash | 8MB |
| Display | ST7789V2 1.14" 240x135 |
| Audio codec | ES8311 |
| Amplifier | NS4150B |
| Microphone | MEMS |
| Keyboard | 56 keys (TCA8418) |
| IMU | BMI270 |
| Battery | 1750mAh (with ADC level monitoring) |

## Pin Definitions

### Display (ST7789V2)
| Function | GPIO |
|------|------|
| MOSI | GPIO35 |
| SCLK | GPIO36 |
| CS | GPIO37 |
| DC | GPIO34 |
| RST | GPIO33 |
| BL | GPIO38 |

### Audio (ES8311)
| Function | GPIO |
|------|------|
| I2C SDA | GPIO8 |
| I2C SCL | GPIO9 |
| I2S BCLK | GPIO41 |
| I2S LRCK | GPIO43 |
| I2S DOUT | GPIO46 |
| I2S DIN | GPIO42 |

### Battery Monitoring
| Function | GPIO | Notes |
|------|------|------|
| ADC | GPIO10 | 100KΩ / 100KΩ voltage divider |

## Usage

1. Press the BOOT button to enter WiFi configuration mode
2. Connect to WiFi to use the voice assistant

## Flash Parameters

Chip: ESP32-S3, Flash: 8MB, Mode: DIO, Frequency: 80MHz

| Address | File |
|------|------|
| 0x0 | bootloader/bootloader.bin |
| 0x8000 | partition_table/partition-table.bin |
| 0xd000 | ota_data_initial.bin |
| 0x20000 | xiaozhi.bin |
| 0x600000 | generated_assets.bin |

Flash command (build directory is `build-cardputer-adv`):

```bash
python -m esptool --chip esp32s3 -b 460800 -p PORT \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 build-cardputer-adv/bootloader/bootloader.bin \
  0x8000 build-cardputer-adv/partition_table/partition-table.bin \
  0xd000 build-cardputer-adv/ota_data_initial.bin \
  0x20000 build-cardputer-adv/xiaozhi.bin \
  0x600000 build-cardputer-adv/generated_assets.bin
```

Replace `PORT` with the actual serial device path (e.g. `/dev/cu.usbmodem21101`).

## References

- [M5Stack Cardputer Adv official documentation](https://docs.m5stack.com/en/core/Cardputer-Adv)
