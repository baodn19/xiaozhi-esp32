# Vertical Scroll for Recipes
## 3.2inch SPI Module ILI9341 Touchscreen
- *Related pins*: T_IRQ (low when touch detected)

## Software
- LVGL Scrolling library

## Files
- `main/display/lcd_display.cc`:
    - Setup (~850–873): Enable vertical scroll on the container (e.g. LV_OBJ_FLAG_SCROLLABLE, LV_DIR_VER, scrollbar mode)
    - Use a fixed row height so content can overflow and scroll when top_k > ~3–4
- `main/boards/common/lotusai_utils.h` (95-104): use fixed row height + scroll Y (or map via LVGL child hit-test), not area_h / recipe_count. (`tests/lotusai/` was removed; do not reintroduce a mirrored `lotusai_utils_host.h` — keep hit-test math only in `lotusai_utils.h`.)
- `main/boards/bread-compact-wifi-lcd-touch/compact_wifi_board_lcd_touch.cc` (160): For LVGL drag-scrolling you either register XPT2046 with LVGL (lvgl_port_add_touch), or extend TouchPollCallback to distinguish drag vs tap and drive scroll yourself. Tap selection still flows through OptionFromPoint → SelectByIndex.
- `main/boards/common/lotusai_controller.h`: OptionFromPoint (~345–350) calls LotusAiOptionIndexFromPoint. It needs the scroll-aware mapping (pass scroll offset / fixed row height, or ask the display which row was hit). Recommend/select MCP tools themselves do not need scroll logic.

### `main/display/lcd_display.cc`
- `lotusai_panel_`: outer LotusAI region on the screen, child of `screen`
- `lotusai_rows_container_`: window for scrolling through recipes, child of `lotusai_panel_`
    - Keep fixed heights
    - Add border for each recipe items
    - Add 2 pixels of space between each recipe

## Implementation Plan
### Checkpoint 1: Enable vertical scroll on the `lotusai_rows_container_`
- *Add scroll flags*:
```
lv_obj_set_scroll_dir(lotusai_rows_container_, LV_DIR_VER);
lv_obj_set_scrollbar_mode(lotusai_rows_container_, LV_SCROLLBAR_MODE_ACTIVE);
```
- *Delete dynamic area_h / rows.size() computation in SetLotusRecipeList()*:
- *Derive the row height from font metrics*, computing vpad once from the theme and reusing it for both the formula and the row's actual padding:
```
inline int LotusAiRecipeRowHeight(int line_height, int vpad = 8, int border = 1) {
    return 2 * line_height + vpad + border;
}

// at call site:
const int pad_side = lvgl_theme->spacing(2);
const int row_h = LotusAiRecipeRowHeight(text_font->line_height, pad_side * 2);
...
lv_obj_set_style_pad_top(row, pad_side, 0);
lv_obj_set_style_pad_bottom(row, pad_side, 0);
```
- *Create enough line for 2 lines of text for each recipe*: switch the label from LV_LABEL_LONG_DOT to LV_LABEL_LONG_WRAP and give it height 2 * line_height
```
lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
lv_obj_set_height(label, 2 * text_font->line_height);
```
- *Allow overflow*: `lv_obj_set_size(row, LV_HOR_RES, row_h);`
- *Remove LV_SCROLLBAR_MODE_OFF*: at `lcd_display.cc:873`
- *Use lv_obj_set_style_pad_row(lotusai_rows_container_, 2, 0) for the 2px gap*

### Checkpoint 2: scroll-aware hit-test
#### Pre-commit:
- *Rewrite math logic to fit dynamic row height*:
```
// Conceptual CP2 shape — replace area_h / recipe_count
inline int LotusAiOptionIndexFromPoint(y, recipe_count, geometry) {
    if (recipe_count <= 0 || row_h <= 0) return -1;
    if (y < content_y_offset || y >= display_height) return -1;
    const int local_y = (y - content_y_offset) + scroll_y;
    if (local_y < 0) return -1;
    const int stride = row_h + pad_row;
    const int idx = local_y / stride;
    if (idx < 0 || idx >= recipe_count) return -1;
    if ((local_y % stride) >= row_h) return -1;  // pad_row gap
    return idx;
}
```
- *Create a getter to cache row_h*: GetLotusRecipeRowHeight() → int; declare in `lcd_display.h` and implement in `lcd_display.cc`
    - In `SetLotusRecipeList`, after computing row_h: `lotusai_recipe_row_h_.store(row_h);`
    - `int GetLotusRecipeRowHeight() const { return lotusai_recipe_row_h_.load(); }`
- *Create a getter to retrieve scroll_y*: GetLotusRecipeScrollY() → int; declare in `lcd_display.h` and implement in `lcd_display.cc`
- *Modify OptionFromPoint to match with new math logic*:
```
int OptionFromPoint(int /*x*/, int y) const {
    int count = static_cast<int>(pdf_keys_.size());
    auto* display = Board::GetInstance().GetDisplay();
    int display_h = display ? display->height() : 320;
    int row_h = display ? display->GetLotusRecipeRowHeight() : 0;
    int scroll_y = display ? display->GetLotusRecipeScrollY() : 0;
    LotusAiHitTestGeometry hit_geometry{.row_h = row_h, .scroll_y = scroll_y, .display_height = display_h};
    return LotusAiOptionIndexFromPoint(y, count, hit_geometry);
}
```
- *Create a constant for pad-row gap*: Create LOTUSAI_ROW_PAD_Y in lotusai_utils.h, alongside LOTUSAI_CONTENT_Y_OFFSET; replace it in lcd_display.cc:875
    - LotusAiOptionIndexFromPoint
    - OptionFromPoint
- *Create a struct to replace the growing positional parameter list*:
    - Struct: `struct LotusAiHitTestGeometry { int row_h; int scroll_y = 0; int pad_row = LOTUSAI_ROW_PAD_Y; int content_y_offset = LOTUSAI_CONTENT_Y_OFFSET; int display_height; };`
    - Construct with **designated initializers** at every call site (see `OptionFromPoint` below) — positional aggregate init (`{row_h, scroll_y, ...}`) would silently miscompile if the struct's field order ever changes, and it also re-specifies `pad_row`/`content_y_offset` instead of letting the in-class defaults do their job.
- *Lock note*: `SetLotusRecipeList` (`lcd_display.cc:1150`) already opens a `DisplayLockGuard lock(this)` as its second line — already locked, no change needed here.
- *Cache `scroll_y` via `LV_EVENT_SCROLL` instead of a live `lv_obj_get_scroll_y` read in `OptionFromPoint`* — writer is the LVGL task (event callback), reader (`GetLotusRecipeScrollY()`) is just an int read, so the esp_timer touch-poll task never touches live LVGL objects and never needs `DisplayLockGuard` for scroll:
1. Add cache member in `lcd_display.h` next to `lotusai_recipe_row_h_`:
```cpp
std::atomic<int> lotusai_recipe_row_h_{0};  // CP2 row-height cache (<atomic> already included)
std::atomic<int> lotusai_scroll_y_{0};      // CP2 scroll cache
```
2. Add a static event callback (private static in `LcdDisplay`, or free function in `lcd_display.cc`) that runs on the LVGL thread:
```cpp
static void OnLotusRowsScroll(lv_event_t* e) {
    auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
    if (self == nullptr) return;
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    self->lotusai_scroll_y_ = lv_obj_get_scroll_y(target);
}
```
3. Register it once, next to CP1's scroll flags in `SetupUI` (`lcd_display.cc` ~863–876), not inside `SetLotusRecipeList`:
```cpp
lv_obj_set_scroll_dir(lotusai_rows_container_, LV_DIR_VER);
lv_obj_set_scrollbar_mode(lotusai_rows_container_, LV_SCROLLBAR_MODE_ACTIVE);
lv_obj_set_style_pad_row(lotusai_rows_container_, LOTUSAI_ROW_PAD_Y, 0);
lv_obj_add_event_cb(lotusai_rows_container_, &LcdDisplay::OnLotusRowsScroll,
                        LV_EVENT_SCROLL, this);
lotusai_scroll_y_ = 0;
```
4. Reset the caches whenever the recipe list is rebuilt or emptied in `SetLotusRecipeList` (already under `DisplayLockGuard`), so a later tap never reuses a stale offset/height from a previous search. `SetLotusRecipeList` has two exit points; rather than duplicating the reset snippet at both, add one private helper and call it from both:
    - Call `ResetLotusRecipeHitTestCache()` before the early `return` on the empty-`rows` path (`lcd_display.cc:1160-1166`, right after `ClearLotusRecipeRows()`/hiding the container). This also covers the empty-list `row_h` reset — no separate fix needed there.
    - Call `ResetLotusRecipeHitTestCache()` again on the populate path (once `lotusai_rows_container_` is unhidden again), before `SetLotusRecipeList` re-stores the real `row_h` via `lotusai_recipe_row_h_.store(row_h);` (see getter bullet above).
    - Declare void ResetLotusRecipeHitTestCache(); in lcd_display.h next to ClearLotusRecipeRows()
```cpp
void LcdDisplay::ResetLotusRecipeHitTestCache() {
    lotusai_recipe_row_h_.store(0);
    if (lotusai_rows_container_) {
        // LV_ANIM_OFF => synchronous; fires LV_EVENT_SCROLL (if offset != 0),
        // which zeroes lotusai_scroll_y_ via OnLotusRowsScroll.
        lv_obj_scroll_to_y(lotusai_rows_container_, 0, LV_ANIM_OFF);
    }
}
```
5. Getter becomes a plain, lock-free read:
```cpp
int LcdDisplay::GetLotusRecipeScrollY() const {
    return lotusai_scroll_y_.load();
}
```
- Elastic overscroll can make the cached value briefly negative — already handled by `local_y < 0 → -1` in `LotusAiOptionIndexFromPoint`.
- Pitfalls: register the callback once at container creation (not per `SetLotusRecipeList` call, or callbacks stack); attach to `lotusai_rows_container_` itself (the `LV_DIR_VER` scrollable), not per-row; keep the getter LVGL-API-free or the whole point of caching is defeated; base `Display::GetLotusRecipeScrollY()` stub still just returns `0`. ResetLotusRecipeHitTestCache() must only be called while already locked.
- Trade-off vs. locking a live `lv_obj_get_scroll_y` read in `OptionFromPoint`: cached value is only as fresh as the last `LV_EVENT_SCROLL` firing (fine for 200 ms tap polling) but removes the need for `DisplayLockGuard` on every tap's scroll read.
- Footnote (out of CP2 scope — do not block on this): Hit-test combines `pdf_keys_.size()` (controller) with display caches `row_h` / `scroll_y`. CP2 does not lock those together or enforce that `SetLotusRecipeList` and the matching `pdf_keys_` update commit as one snapshot for the touch-poll task. Today they are sequenced by call order (`DoRecommend` / recommend-complete → `ShowRecipeMenu`), with windows such as cleared keys + `"Searching..."` on screen. Usually fine if taps while `count == 0` return `-1`; a real bug only if UI rows and keys can drift (list redrawn without keys, or keys cleared while old rows remain). Out of band for stride/`scroll_y` math.
- Add a **10 kΩ pull-up on T_CS (GPIO 47)** to 3.3 V so the touch chip stays deselected while the display is drawing. Also add a **10 kΩ pull-up on T_IRQ (GPIO 2)** to 3.3 V — same topology as T_CS (`T_IRQ — wire — GPIO 2 — 10 kΩ — 3.3 V`). PENIRQ is open-drain-ish and the firmware only reads touch when that line is low; without the pull-up GPIO 2 can float and taps never register. (allow touch debug log)

- **Kconfig — `CONFIG_XPT2046_INTERRUPT_MODE` must be enabled:** the XPT2046 driver (`managed_components/atanisoft__esp_lcd_touch_xpt2046`) only leaves PENIRQ able to re-assert on a fresh touch if this option is on ("Full Power Mode" in `menuconfig` under `Component config → XPT2046 → Enable Interrupt (PENIRQ) output"`). With it **off** (the driver default), the *first* tap after boot/reset works, but every subsequent tap silently fails to register — the poll loop in `compact_wifi_board_lcd_touch.cc` sees `T_IRQ` stuck high forever, even while pressing the screen. This is a firmware config issue, not a wiring issue — do not chase it as a hardware fault. It's already
enabled by default for this board via `CONFIG_XPT2046_INTERRUPT_MODE=y` in `sdkconfig.defaults` / `sdkconfig.defaults.esp32s3`; if you hand-edit `sdkconfig` or run `idf.py menuconfig` and it gets toggled off (or you delete `sdkconfig` and regenerate from a checkout missing the defaults above), re-enable it and rebuild. (allow individual taps)

- **Decouple touch orientation from LCD panel mirrors:** Do **not** derive `esp_lcd_touch_config_t.flags.{swap_xy,mirror_x,mirror_y}` from `DISPLAY_SWAP_XY` / `DISPLAY_MIRROR_X` / `DISPLAY_MIRROR_Y`. Those macros only set the ILI9341 `MADCTL` pixel-scanout orientation; the XPT2046 overlay's ADC→axis wiring is independent. Empirically (tap top/bottom/left/right and read `tap x=… y=…` logs), this module needed `TOUCH_MIRROR_X=false`, `TOUCH_MIRROR_Y=true`, `TOUCH_SWAP_XY=false`. With the old wiring (`DISPLAY_MIRROR_X=true`, `DISPLAY_MIRROR_Y=false`), both axes were inverted relative to LVGL/screen space — a tap near the bottom (e.g. recipe row 5) reported `y≈50` and hit-test returned `idx=-1` via `y < LOTUSAI_CONTENT_Y_OFFSET` (80), even though stride/`row_h`/`scroll_y` math was fine.
+ In `config.h`, define dedicated macros next to the touch GPIO pins:
```cpp
// Touch panel's independent axis convention — determined empirically by
// physically tapping known screen edges (top/bottom/left/right) and
// checking logged x/y; do NOT assume it matches DISPLAY_MIRROR_X/Y, which
// only controls the LCD panel's pixel scanout register.
#define TOUCH_MIRROR_X  false
#define TOUCH_MIRROR_Y  true
#define TOUCH_SWAP_XY   false
```
+ In `InitializeTouchscreen()` (`compact_wifi_board_lcd_touch.cc`):
```cpp
touch_cfg.flags.swap_xy  = TOUCH_SWAP_XY  ? 1u : 0u;
touch_cfg.flags.mirror_x = TOUCH_MIRROR_X ? 1u : 0u;
touch_cfg.flags.mirror_y = TOUCH_MIRROR_Y ? 1u : 0u;
```
+ Verify after flash: top → small `y`, bottom → large `y`; left → small `x`, right → large `x`. Then row taps should resolve positive `idx` when `row_h > 0`.

#### Post-commit
- 

### Checkpoint 3: drag-aware poll callback