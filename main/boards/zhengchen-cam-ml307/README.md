# Product Links
# Zhengchen Tech AI Camera + 4G

## Overview
Zhengchen Tech AI Camera is a heavily customized Xiaozhi AI fork with many innovations and optimizations.

## Merged Edition
The merged edition is maintained in the main Xiaozhi AI repo and tracks upstream releases, so users and third-party firmware can extend it easily. Supports wake word, barge-in, OTA, etc.

## Fork Edition
The fork edition has large low-level changes and is maintained separately, with periodic merges from upstream.

https://e.tb.cn/h.6Gl2LC7rsrswQZp?tk=qFuaV9hzh0k CZ356
```
[Taobao] Xiaozhi AI camera board with object recognition, dual mic barge-in, ESP32S3N16R8, emoji display
https://e.tb.cn/h.hBc8Gcx9cUluJJO?tk=YW5C4LPixKg



## Build & Flash

This board needs many sdkconfig options; use the release script when possible.

**Build**

```bash
python ./scripts/release.py zhengchen-cam-ml307
```

For manual builds, see `zhengchen-cam-ml307/config.json` and set the matching menuconfig options.

**Flash**

```bash
idf.py flash


```

MCP Tools:
self.get_device_status
self.audio_speaker.set_volume
self.screen.set_brightness
self.screen.set_theme
self.gif.set_gif_mode
self.display.set_mode
self.camera.take_photo       
self.AEC.set_mode
self.AEC.get_mode
self.res.esp_restart
