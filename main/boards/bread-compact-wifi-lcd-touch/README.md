# Bread Compact WiFi + ILI9341 LCD + XPT2046 Touch

This board profile targets the standard `bread-compact-wifi` ESP32-S3 audio
board paired with a **Hosyond / MSP3218 3.2" SPI ILI9341 320×240 TFT LCD**
module that includes an XPT2046 resistive touch controller.

It extends the stock `bread-compact-wifi-lcd` profile by:

- Enabling MISO on the SPI3 bus so the XPT2046 can return data.
- Initialising the XPT2046 touch controller and wiring it to LVGL.
- Registering the `LotusAiController` MCP tools (`lotusai.recommend` and
  `lotusai.select`) for voice + tap recipe selection.
- Omitting the demo `LampController` (not present on this hardware).

---

## Physical wiring

| Module pin | Signal              | ESP32-S3 GPIO | Macro                    |
|-----------|---------------------|---------------|--------------------------|
| 1         | VCC (3.3 V)         | 3V3           | —                        |
| 2         | GND                 | GND           | —                        |
| 3         | CS (display)        | GPIO 41       | `DISPLAY_CS_PIN`         |
| 4         | RESET               | GPIO 45       | `DISPLAY_RST_PIN`        |
| 5         | DC / RS             | GPIO 18       | `DISPLAY_DC_PIN`         |
| 6         | SDI / MOSI          | GPIO 17       | `DISPLAY_MOSI_PIN`       |
| 7         | SCK                 | GPIO 21       | `DISPLAY_CLK_PIN`        |
| 8         | LED (backlight)     | GPIO 42       | `DISPLAY_BACKLIGHT_PIN`  |
| 9 / 13    | SDO / T_DO (MISO)   | GPIO 38       | `TOUCH_MISO_PIN`         |
| 10        | T_CLK               | GPIO 21       | shared with `DISPLAY_CLK_PIN` |
| 11        | T_CS                | GPIO 47       | `TOUCH_CS_PIN`           |
| 12        | T_DIN               | GPIO 17       | shared with `DISPLAY_MOSI_PIN` |
| 14        | T_IRQ               | GPIO 2        | `TOUCH_IRQ_PIN`          |

> **ESP32-S3 with 8 MB octal PSRAM (N16R8 / R8 modules):** GPIO **33–37** are
> connected to internal PSRAM and must not be used. Do not wire T_CS to GPIO 37.

> T_CLK and T_DIN share the SPI3 bus lines with the ILI9341 display. Only
> the CS pins differ, which is sufficient for SPI bus sharing.

---

## Build & Flash

```bash
cd xiaozhi-esp32
idf.py set-target esp32s3
idf.py menuconfig
```

In menuconfig navigate to:

```
Xiaozhi Assistant →
  Board Type            → Bread Compact WiFi + LCD + Touch (面包板 + XPT2046)
  Display → LCD Type    → ILI9341 240*320
  LotusAI backend base URL  → https://lotusfoodasmedicine.com
                              (or http://192.168.x.x:8000 for local dev)
```

```bash
idf.py -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.esp32s3 build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## LotusAI MCP tools

| Tool name           | Trigger          | What it does                                              |
|---------------------|------------------|-----------------------------------------------------------|
| `lotusai.recommend` | Voice (XiaoZhi)  | POST `/api/xiaozhi/recommend`, display recipe list, return `spoken_menu` for TTS |
| `lotusai.select`    | Voice or tap     | POST `/api/xiaozhi/select`, decode `qr_base64` PNG, show QR on screen, return `spoken_confirm` for TTS |

Touch selection works by dividing the display height equally between the
returned recipe items. Tap the row of the recipe you want, and the QR code
for that recipe is fetched and displayed.

---

## XiaoZhi Cloud System Prompt

Paste the following into the XiaoZhi console as the device system prompt
(or include it in the `custom_instructions` field of the hello message):

```
You are a healthy-recipe assistant for LotusAI.

Before searching: When the user asks for recipe ideas, first summarize what you
understood — ingredients, health conditions, allergies, available cooking tools,
plant-based preference, meal, age group, cuisine, and how many options they want
(top_k, 3–12). Speak that summary aloud and ask: "Is that correct? Should I
search for recipes?" Do NOT call lotusai.recommend until the user confirms (e.g.
"yes", "correct", "go ahead") or corrects any detail. If they correct you,
repeat the updated summary and ask again.

After confirmation: Call lotusai.recommend with the confirmed fields. Pass
allergies as allergens (comma-separated, e.g. "peanuts,dairy"), cooking
equipment as cooking_tools (e.g. "stove,microwave"), and plant_based: true if
they want plant-based only. If the user asked for a specific number of options
(e.g. "six recipes"), pass that as top_k (3–12). If they did not specify a
count, omit top_k so the device uses its default.

Selection: When they pick an option (by number, name, or tap), call
lotusai.select with that option number.

Never describe recipes yourself — always call the tools.
```
