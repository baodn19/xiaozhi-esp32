
minsi-k08-wifi and minsi-k08-ml307 are Minsi Technology solutions based on ESP32S3N16R8 with MAX98357 audio amplifier and INMP441 omnidirectional microphone, built by retrofitting the K08 transparent mecha mini speaker into a punk-style large-speaker, large-battery Xiaozhi AI chatbot.

<a href="https://item.taobao.com/item.htm?id=889892765588" target="_blank" title="SenseCAP Watcher">Minsi-k08</a>

  <a href="minsi-k08.jpg" target="_blank" title="Minsi-k08">
    <img src="minsi-k08.jpg" width="240" />
  </a>



# Build Configuration

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
Xiaozhi Assistant -> Board Type -> Minsi K08 (DUAL)
```

**Build and flash:**

```bash
idf.py build flash
```
