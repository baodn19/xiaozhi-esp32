# Usage 

* [ESP32-S3-Touch-LCD-4.3C docs](https://www.waveshare.com/esp32-s3-touch-lcd-4.3c.htm)

## Quick start

Download prebuilt [firmware](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3C/ESP32-S3-Touch-LCD-4.3C-Xiaozhi.bin) 

```shell
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x00 ESP32-S3-Touch-LCD-4.3C-Xiaozhi.bin 
```

## Basic usage

* idf version: v5.5-dev

1. Set build target to esp32s3

```shell
idf.py set-target esp32s3
```

2. Change configuration 

```shell
cp main/boards/esp32-s3-touch-lcd-4.3c/sdkconfig.4_3c sdkconfig
```

3. Build and flash

```shell
idf.py build flash monitor
```

## log

@2025/05/17 Test notes

1. This partition must exist when returning to the app UI, otherwise it fails
2. 
3. 
 
## TODO
