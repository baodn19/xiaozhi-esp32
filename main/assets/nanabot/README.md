# NanaBot custom wake word assets

This directory holds the custom MultiNet6 wake word pack (`assets.bin`) used by NanaBot. Flash it to the `assets` SPIFFS partition (see project flashing notes; on 16 MB flash the offset is `0x800000`).

## How to say the wake word

The model was trained on **"NA NA BOT"** — three distinct syllables with spaces between them. MultiNet6 does phoneme-level matching, not whole-word matching.

Say it like:

> **"NAH — NAH — BOT"** — slow, clear syllables, with about **0.5 s pause** between each word.

**Do not** say **"nanabot"** run together as one word. That pronunciation does not match the phoneme sequence the model expects.

Saying it **slow and deliberate** makes detection more consistent, especially in noisy environments.

## Configuration in `assets.bin`

Typical settings (see `index.json` inside the SPIFFS image):

| Field | Example | Notes |
|-------|---------|--------|
| `command` | `na na bot` | Space-separated syllables for MultiNet6 G2P |
| `text` | `NanaBot` | Display / server greeting label |
| `threshold` | `0.3`–`0.5` | Lower = more sensitive (more false triggers) |
| `duration` | `3000` ms | Shorter windows reduce accumulated background noise |

Regenerate `assets.bin` on the [Xiaozhi assets generator](https://github.com/78/xiaozhi-assets-generator) when changing command text, threshold, or duration.

## Troubleshooting

- Use **Acoustic Check** (`scripts/acoustic_check/`) to verify mic levels and noise floor before tuning threshold.
- High RMS during silence (e.g. 4000+) usually means desk/PC noise, not a broken mic — move the board away from fans and speak 15–20 cm from the INMP441.
- INMP441 **L/R** pin: tie to **GND** for left channel (board codec reads the left I2S slot).
