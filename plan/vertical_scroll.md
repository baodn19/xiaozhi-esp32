# Vertical Scroll for Recipes
## 3.2inch SPI Module ILI9341 Touchscreen
- *Related pins*: T_IRQ (low when touch detected)

## Software
- LVGL Scrolling library

## Files
- `main/display/lcd_display.cc`:
    - Setup (~850–873): Enable vertical scroll on the container (e.g. LV_OBJ_FLAG_SCROLLABLE, LV_DIR_VER, scrollbar mode)
    - Use a fixed row height so content can overflow and scroll when top_k > ~3–4
- `main/boards/common/lotusai_utils.h` (95-104): use fixed row height + scroll Y (or map via LVGL child hit-test), not area_h / recipe_count. Mirror the same logic in tests/lotusai/lotusai_utils_host.h and update tests/lotusai/test_selection.cpp.
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
### Checkpoint 3: drag-aware poll callback