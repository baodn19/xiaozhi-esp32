# quandong-s3-dev vs bread-compact-wifi Hardware Differences

Both boards use **ESP32-S3 + Wi-Fi**, inherit `WifiBoard`, and share the same main flow: boot → provisioning → Xiaozhi backend → voice chat. Differences are in **audio path**, **display**, and **human interaction**.

---

## 1. Overview

| Dimension | quandong-s3-dev | bread-compact-wifi |
|---|---|---|
| Chip | ESP32-S3 | ESP32-S3 |
| Base class | `WifiBoard` | `WifiBoard` |
| Audio codec | **ES8311 hardware codec** (I2C + I2S duplex) | **No codec**: MEMS I2S mic + I2S digital amp (`NoAudioCodec`) |
| Display | **ILI9341 240×320 SPI color LCD** | **SSD1306 / SH1106 OLED 128×32 or 128×64** (I2C mono) |
| Buttons | 1 (BOOT) | 4 (BOOT, Touch, Vol+, Vol-) |
| On-board LED | None | GPIO48 single-color |
| MCP peripheral demo | None | `LampController` (GPIO18) |
| Backlight | PWM on GPIO45 | OLED (self-lit, no backlight) |
| Font / emoji assets | `font_noto_basic_20_4` + `font_awesome_20_4` + `noto-emoji_128` | `font_puhui_basic_14_1` + `font_awesome_14_1` (no emoji) |

---

## 2. Audio Subsystem

### quandong-s3-dev

| Item | Value |
|---|---|
| Codec chip | **ES8311** (I2C addr `ES8311_CODEC_DEFAULT_ADDR`) |
| I2C bus | I2C0, SDA = **GPIO16**, SCL = **GPIO15** |
| I2S mode | **Duplex** (one I2S bus shared) |
| I2S pins | MCLK = **GPIO4**, BCLK = **GPIO5**, WS = **GPIO7**, DIN = **GPIO6**, DOUT = **GPIO8** |
| Sample rate | Input / output = **24000 / 24000** |
| PA control | **GPIO1 active low** (via `InitializeAudioPaEnable()`) |
| Volume | ES8311 registers (`Es8311AudioCodec::SetOutputVolume`) |

### bread-compact-wifi

| Item | Value |
|---|---|
| Codec chip | **None** (`NoAudioCodecSimplex`) |
| I2S mode | **Simplex** (separate I2S for mic and speaker) |
| Mic I2S | WS = GPIO4, SCK = GPIO5, DIN = GPIO6 |
| Speaker I2S | DOUT = GPIO7, BCLK = GPIO15, LRCK = GPIO16 |
| Sample rate | Input / output = **16000 / 24000** |
| Volume | I2S digital gain (software) |

### Key Differences

- **Hardware codec vs raw I2S**: ES8311 gives stable ADC/DAC, hardware volume, and pop suppression; bread-compact-wifi relies on software.
- **Sample rate**: quandong captures at 24 kHz; bread-compact-wifi at 16 kHz. Both are resampled by the audio pipeline to the protocol rate.
- **I2C usage**: quandong uses I2C0 for ES8311; bread-compact-wifi uses I2C0 for SSD1306. Different purpose and pins on each board.

---

## 3. Display Subsystem

### quandong-s3-dev

| Item | Value |
|---|---|
| Panel | **ILI9341** |
| Interface | **SPI** (SPI2, 40 MHz) |
| Resolution | **240 × 320**, landscape (`SWAP_XY=true` → effective 320×240) |
| Pins | MOSI = GPIO11, SCK = GPIO12, CS = GPIO10, DC = GPIO46 |
| Backlight | **PWM**, GPIO45, non-inverted |
| Display class | `SpiLcdDisplay` (LVGL 9, RGB565) |
| Init sequence | Vendor `ili9341_vendor_specific_init` (gamma / power timing) |

### bread-compact-wifi

| Item | Value |
|---|---|
| Panel | **SSD1306** (default) or **SH1106** (Kconfig `DISPLAY_OLED_TYPE`) |
| Interface | **I2C** (400 kHz), address `0x3C` |
| Resolution | **128 × 32** or **128 × 64** (Kconfig) |
| Pins | SDA = GPIO41, SCL = GPIO42 |
| Backlight | None (OLED self-lit) |
| Display class | `OledDisplay` (LVGL 9, 1-bit mono) |

### Key Differences

- **Color vs mono**: quandong can show emoji, AI logo, large text; bread-compact-wifi fits ~1–2 lines plus simple icons.
- **Bandwidth**: SPI 40 MHz vs I2C 400 kHz—two orders of magnitude apart, affecting refresh smoothness.
- **Driver stack**: quandong uses `esp_lcd_ili9341` (1.2.0) + `lvgl_port`; bread-compact-wifi uses `esp_lcd_panel_ssd1306` / `esp_lcd_panel_sh1106`.

---

## 4. Input and Status Indicators

### Buttons

| Button | quandong | bread-compact-wifi | Behavior |
|---|---|---|---|
| BOOT (GPIO0) | ✅ | ✅ | Provisioning during boot; `ToggleChatState` at runtime |
| Touch (GPIO47) | ❌ | ✅ | **Push-to-talk**: `OnPressDown→StartListening`, `OnPressUp→StopListening` |
| Vol+ (GPIO40) | ❌ | ✅ | Click +10 volume; long-press max 100 |
| Vol- (GPIO39) | ❌ | ✅ | Click -10 volume; long-press mute |

→ quandong has only BOOT—**no push-to-talk**, **no physical volume keys**. Use MCP or voice for volume.

### LED

| Board | LED | Purpose |
|---|---|---|
| quandong | `BUILTIN_LED_GPIO = NC` | None |
| bread-compact-wifi | GPIO48, `SingleLed` | Status (idle / listening / speaking) |

### MCP Tools

bread-compact-wifi registers `LampController(GPIO18)` as an MCP demo for remote on/off via Xiaozhi. quandong has no such peripheral example.

---

## 5. Boot Flow Differences

Main flow is the same (see `application.cc:61` / `wifi_board.cc:52`); UI is started by `Application::Initialize()`. Only **board constructor** steps differ:

### quandong-s3-dev constructor

```
InitializeI2c()             // I2C0 for ES8311
InitializeSpi()             // SPI2 for ILI9341
InitializeAudioPaEnable()   // Pull GPIO1 low to enable PA
InitializeIli9341Display()  // Custom init cmds + SpiLcdDisplay
InitializeButtons()         // Single BOOT button
GetBacklight()->SetBrightness(100)
```

### bread-compact-wifi constructor

```
InitializeDisplayI2c()      // I2C0 for SSD1306
InitializeSsd1306Display()  // OledDisplay
InitializeButtons()         // BOOT + Touch + Vol+ + Vol-
InitializeTools()           // Register LampController (MCP peripheral)
```

---

## 6. Assets and Build Config

Font and emoji settings in `main/CMakeLists.txt`:

| Board | text font | icon font | emoji collection |
|---|---|---|---|
| quandong-s3-dev | `font_noto_basic_20_4` | `font_awesome_20_4` | `noto-emoji_128` |
| bread-compact-wifi | `font_puhui_basic_14_1` | `font_awesome_14_1` | (none) |

→ quandong uses the same asset tier as `lichuang-dev`, `esp-box-3`, and other 240×320 LCD boards; OLED boards use 14 px bitmap fonts and skip emoji.

### Other Dependencies

- quandong needs `espressif/esp_lcd_ili9341 ==1.2.0` (already in `main/idf_component.yml`)
- bread-compact-wifi needs no extra components; `esp_lcd_panel_ssd1306` is in ESP-IDF

---

## 7. One-Line Summary

> **quandong-s3-dev** ≈ color LCD + hardware codec + single button dev board—strong UI, simpler input.
> **bread-compact-wifi** ≈ OLED + soft I2S codec + multi-button + on-board lamp breadboard reference—good for MCP peripherals.
> Application features (provisioning, voice chat, MCP, wake word) come from `WifiBoard` and behave the same on both.
