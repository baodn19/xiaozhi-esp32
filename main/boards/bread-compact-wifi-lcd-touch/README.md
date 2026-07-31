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


| Module pin | Signal            | ESP32-S3 GPIO | Macro                          |
| ---------- | ----------------- | ------------- | ------------------------------ |
| 1          | VCC (3.3 V)       | 3V3           | —                              |
| 2          | GND               | GND           | —                              |
| 3          | CS (display)      | GPIO 41       | `DISPLAY_CS_PIN`               |
| 4          | RESET             | GPIO 45       | `DISPLAY_RST_PIN`              |
| 5          | DC / RS           | GPIO 18       | `DISPLAY_DC_PIN`               |
| 6          | SDI / MOSI        | GPIO 17       | `DISPLAY_MOSI_PIN`             |
| 7          | SCK               | GPIO 21       | `DISPLAY_CLK_PIN`              |
| 8          | LED (backlight)   | GPIO 42       | `DISPLAY_BACKLIGHT_PIN`        |
| 9 / 13     | SDO / T_DO (MISO) | GPIO 38       | `TOUCH_MISO_PIN`               |
| 10         | T_CLK             | GPIO 21       | shared with `DISPLAY_CLK_PIN`  |
| 11         | T_CS              | GPIO 47       | `TOUCH_CS_PIN`                 |
| 12         | T_DIN             | GPIO 17       | shared with `DISPLAY_MOSI_PIN` |
| 14         | T_IRQ             | GPIO 2        | `TOUCH_IRQ_PIN`                |


> **ESP32-S3 with 8 MB octal PSRAM (N16R8 / R8 modules):** GPIO **33–37** are
> connected to internal PSRAM and must not be used. Do not wire T_CS to GPIO 37.

> T_CLK and T_DIN share the SPI3 bus lines with the ILI9341 display. Only
> the CS pins differ, which is sufficient for SPI bus sharing.
>
> **Wiring tip:** Join module pin 6 (SDI/MOSI) and pin 12 (T_DIN) at the
> same ESP32 GPIO (17). Add a **10 kΩ pull-up on T_CS (GPIO 47)** to 3.3 V
> so the touch chip stays deselected while the display is drawing. Also add a
> **10 kΩ pull-up on T_IRQ (GPIO 2)** to 3.3 V — same topology as T_CS
> (`T_IRQ — wire — GPIO 2 — 10 kΩ — 3.3 V`). PENIRQ is open-drain-ish and the
> firmware only reads touch when that line is low; without the pull-up GPIO 2
> can float and taps never register.

> **Kconfig — `CONFIG_XPT2046_INTERRUPT_MODE` must be enabled:** the XPT2046
> driver (`managed_components/atanisoft__esp_lcd_touch_xpt2046`) only leaves
> PENIRQ able to re-assert on a fresh touch if this option is on ("Full Power
> Mode" in `menuconfig` under `Component config → XPT2046 → Enable Interrupt
> (PENIRQ) output"`). With it **off** (the driver default), the *first* tap
> after boot/reset works, but every subsequent tap silently fails to register
> — the poll loop in `compact_wifi_board_lcd_touch.cc` sees `T_IRQ` stuck high
> forever, even while pressing the screen. This is a firmware config issue,
> not a wiring issue — do not chase it as a hardware fault. It's already
> enabled by default for this board via `CONFIG_XPT2046_INTERRUPT_MODE=y` in
> `sdkconfig.defaults` / `sdkconfig.defaults.esp32s3`; if you hand-edit
> `sdkconfig` or run `idf.py menuconfig` and it gets toggled off (or you
> delete `sdkconfig` and regenerate from a checkout missing the defaults
> above), re-enable it and rebuild.

> **Touch orientation — do not reuse `DISPLAY_MIRROR_*`:** The ILI9341
> `DISPLAY_MIRROR_X` / `DISPLAY_MIRROR_Y` / `DISPLAY_SWAP_XY` flags only
> control LCD pixel scanout. The XPT2046 overlay needs its own axis
> convention, set in `config.h` as `TOUCH_MIRROR_X` / `TOUCH_MIRROR_Y` /
> `TOUCH_SWAP_XY` and applied in `InitializeTouchscreen()`. For this
> Hosyond / MSP3218 module the calibrated values are
> `TOUCH_MIRROR_X=false`, `TOUCH_MIRROR_Y=true`, `TOUCH_SWAP_XY=false`
> (verified by tapping top/bottom/left/right and checking serial `tap x=` /
> `y=` logs). If you previously wired touch flags from `DISPLAY_*`, taps
> near the bottom of the recipe list can report a small `y` and return
> `idx=-1` even when hit-test math and `row_h` are correct — that is an
> axis-mirror mismatch, not a stride bug.

---

## NanaBot custom wake word

NanaBot uses a custom MultiNet6 pack in `main/assets/nanabot/assets.bin`. See
`[main/assets/nanabot/README.md](../../assets/nanabot/README.md)` for flashing and tuning.

**Pronunciation:** The model expects three separate syllables, not one blended word.
Say **"NAH — NAH — BOT"** with ~0.5 s pauses between words. Do **not** say
"nanabot" as a single word. Slow, deliberate speech improves consistency.

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
  Board Type            → Bread Compact WiFi + LCD + Touch (breadboard + XPT2046)
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


| Tool name           | Trigger         | What it does                                                                                           |
| ------------------- | --------------- | ------------------------------------------------------------------------------------------------------ |
| `lotusai.recommend` | Voice (XiaoZhi) | POST `/api/xiaozhi/recommend`, display recipe list, return `spoken_menu` for TTS                       |
| `lotusai.select`    | Voice or tap    | POST `/api/xiaozhi/select`, decode `qr_base64` PNG, show QR on screen, return `spoken_confirm` for TTS |


Touch selection maps tap `y` (after touch-axis mirrors) through
`LotusAiOptionIndexFromPoint` using fixed row height + optional vertical
scroll offset. Tap the row of the recipe you want, and the QR code for that
recipe is fetched and displayed.

---

## XiaoZhi Cloud System Prompt

Paste the following into the XiaoZhi console as the device system prompt
(or include it in the `custom_instructions` field of the hello message):

```
# Role: Reliable Health Guardian, "NanaBot" is your name; you are not a doctor; for diagnosis/dosage changes, suggest consulting a clinician; still help with scheduling and recipes.

## Persona Labels

Vocal Image: Steady, warm, and clear voice. Speaks at a deliberate, unhurried pace to ensure audibility and comprehension for older users.
Core Personality: Patient, observant, dependable, and highly practical.
Interpersonal Positioning: A respectful caregiver and vigilant companion focused entirely on the user's physical safety and daily health management.

## Interaction Style

Interaction Tendency: Helps with health routines when asked. Suggests diet-aware recipes via LotusAI tools. When the device alerts for medication or a fall, respond calmly and assist.
Emotional Response: Remains calm, decisive, and reassuring during emergencies such as a fall. Exhibits consistent patience and gentle encouragement during daily interactions to promote adherence to health routines.
Brevity for voice: Prefer 1–2 short sentences per turn.

## Language Style

Reference Expressions: "It is time for your medication.", “Here are options from LotusAI”, “I can search for recipes that fit your needs”, “Are you hurt? Help is being alerted on the device”, "Take your time.", "Let me know if you need assistance."
Speaking Style: Direct, respectful, and articulate. Uses short, clear sentences. Avoids complex jargon, ensuring all health instructions and safety alerts are easily understood.

## Policy
Recipes: Confirm once with one confirmation question before calling lotusai.recommend; only include fields the user stated; never invent allergies/conditions or recipes yourself; after calling, wait for the tool/screen — don’t apologize for “timeout” while loading; use lotusai.select when they pick one; on QR scanned, call lotusai.confirm_qr.
Medicine: Use the self.medicine.* tools to add/list/delete/clear reminders and to clear an active alert (confirm) — don’t fake a schedule in chat. Confirm name/time/days in one short question, then call the tool, then report success only after the tool returns; when user says they took it / “done,” call self.medicine.confirm; when they ask what’s scheduled, call self.medicine.list first.
Alarm: When the device alerts for medication or a fall, respond calmly and help the user.
User's name: Ask for user's name if it isn't in memory, spell the name back once, and confirm before saving to memory. If a name already exists, use it to refer to the user.
Do not explain tool names or bitmask math to the user.
```

