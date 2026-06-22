# Manual on-device checklist — LotusAI Controller

Perform with the board flashed (`bread-compact-wifi-lcd-touch`), connected via `idf.py monitor`, and XiaoZhi system prompt from [README.md](../../main/boards/bread-compact-wifi-lcd-touch/README.md).

**Prerequisites**

- [ ] WiFi connected; device reaches LotusAI backend (`CONFIG_LOTUSAI_BASE_URL`)
- [ ] XiaoZhi cloud system prompt pasted (includes `excluded_ingredients` guidance)
- [ ] Optional: `ESP_LOGI(LOTUSAI_TAG, "POST %s body: %s", ...)` in `DoRecommend` for serial inspection

---

## Touch selection

1. [ ] Say: "I have chicken and rice, suggest 3 recipes."
2. [ ] Confirm when XiaoZhi reads back the summary.
3. [ ] Verify recipe list on ILI9341 (3 numbered rows).
4. [ ] Tap row 1 (Y ≈ 80–159 px); verify QR code renders and notification shows `spoken_confirm`.
5. [ ] Repeat search; tap row 3 (Y ≈ 240–319 px); verify correct recipe QR.
6. [ ] Tap above Y=80; verify no selection fires.

---

## Voice selection

1. [ ] After recipe list is shown, say: "Select option 2."
2. [ ] Verify QR for recipe 2 appears; TTS speaks confirmation.
3. [ ] Say: "Select option 5" (when only 3 loaded); verify error message.

---

## Allergen-filtered search

1. [ ] Say: "I have chicken and rice, I'm allergic to peanuts, suggest 3 recipes."
2. [ ] Confirm summary includes allergen preference.
3. [ ] Verify returned recipes avoid obvious peanut-heavy titles.
4. [ ] Tap or voice-select an option; verify QR matches filtered result.

---

## Excluded-ingredient search

1. [ ] Say: "I have chicken and rice, but I don't want any mushrooms, suggest 3 recipes."
2. [ ] Confirm summary distinguishes allergy vs dislike (no allergen unless user said allergic).
3. [ ] Verify recipes omit obvious mushroom-containing titles.
4. [ ] Select via tap or voice; verify correct QR.

---

## Conflict sanity (optional)

1. [ ] Say: "I have chicken and cilantro, but exclude cilantro."
2. [ ] Assistant should catch contradiction before calling tool, or API returns 422.

---

## Sign-off


| Tester | Date | Board / firmware | Pass |
| ------ | ---- | ---------------- | ---- |
|        |      |                  |      |


