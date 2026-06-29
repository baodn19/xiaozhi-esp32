# DFRobot ESP32-S3 AI Camera Module

## Introduction
ESP32-S3 AI CAM is a smart camera module based on the ESP32-S3, designed for video/image processing and voice interaction. It suits video monitoring, edge vision, and voice AI projects.
![](https://ws.dfrobot.com.cn/FsTrGbrX2NZAwzWS8OSQGOGikuYA)

[View detailed introduction](https://wiki.dfrobot.com.cn/SKU_DFR1154_ESP32_S3_AI_CAM)

[View vision demo video](https://www.bilibili.com/video/BV1ktjSzNEUU/)

# Features
* PDM microphone
* On-board OV3660 camera

## Button Configuration
* BOOT: Short press - interrupt/wake

## Build Configuration

**Set build target to ESP32S3:**

```bash
idf.py set-target esp32s3
```

**Open menuconfig:**

```bash
idf.py menuconfig
```

**Select board:**

```
Xiaozhi Assistant -> Board Type -> DFRobot ESP32-S3 AI Camera Module
```

**Adjust PSRAM configuration:**

```
Component config -> ESP PSRAM -> SPI RAM config -> Mode (QUAD/OCT) -> Octal Mode PSRAM
```

**Set WiFi TX power to 10:**

```
Component config -> PHY -> (10)Max WiFi TX power (dBm)
```

**Configure camera:**

* **OV3660**
```
Component config -> Espressif Camera Sensors Configurations -> Camera Sensor Configuration -> Select and Set Camera Sensor -> OV3660 ->  Auto detect OV3660

```

```
Component config -> Espressif Camera Sensors Configurations -> Camera Sensor Configuration -> Select and Set Camera Sensor -> OV3660 ->  Select default output format for DVP interface (YUV422 240x240 24fps, DVP 8-bit, 20M input)
```

* **OV2640**
```
Component config -> Espressif Camera Sensors Configurations -> Camera Sensor Configuration -> Select and Set Camera Sensor -> OV2640 ->  Auto detect OV2640

```

```
Component config -> Espressif Camera Sensors Configurations -> Camera Sensor Configuration -> Select and Set Camera Sensor -> OV2640 ->  Select default output format for DVP interface (YUV422 240x240 25fps, DVP 8-bit, 20M input)
```

**Build:**

```bash
idf.py build
```