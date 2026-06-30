# Mixgo Nova Development Board

<img src="https://mixly.cn/public/icon/2024/6/09705006c1c643beb96338791ee1dea0_m.png" alt="Mixgo_Nova" width="200"/>

**[Mixgo_Nova](https://mixly.cn/fredqian/mixgo_nova)** is a multi-function development board for IoT, education, and maker projects. It integrates rich sensors and wireless modules, supports graphical programming (Mixly) and offline voice interaction, and is suited for rapid prototyping and teaching.

---

## 🛠️  Build Configuration

**ES8374 CODE MIC capture issue:**

```
managed_components\espressif__esp_codec_dev\device\es8374

static int es8374_config_adc_input(audio_codec_es8374_t *codec, es_adc_input_t input)
{
    int ret = 0;
    int reg = 0;
    ret |= es8374_read_reg(codec, 0x21, &reg);
    if (ret == 0) {
        reg = (reg & 0xcf) | 0x24;
        ret |= es8374_write_reg(codec, 0x21, reg);
    }
    return ret;
}

PS: At L386 change reg = (reg & 0xcf) | 0x14; to reg = (reg & 0xcf) | 0x24;
```

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
Xiaozhi Assistant -> Board Type -> Mixgo Nova
```

**Configure PSRAM:**

```
Component config -> ESP PSRAM -> SPI RAM config -> Mode (QUAD/OCT) -> QUAD Mode PSRAM
```

**Configure Flash:**

```
Serial flasher config -> Flash size -> 8 MB
Partition Table -> Custom partition CSV file -> partitions/v2/8m.csv
```

**Build:**

```bash
idf.py build
```

**Merge BIN:**

```bash
idf.py merge-bin -o xiaozhi-nova.bin -f raw
```
