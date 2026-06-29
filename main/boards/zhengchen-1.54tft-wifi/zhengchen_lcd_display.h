#ifndef ZHENGCHEN_LCD_DISPLAY_H
#define ZHENGCHEN_LCD_DISPLAY_H

#include "display/lcd_display.h"
#include "lvgl_theme.h"
#include <esp_lvgl_port.h>

class ZHENGCHEN_LcdDisplay : public SpiLcdDisplay {
protected:
    lv_obj_t* high_temp_popup_ = nullptr;  // High-temperature warning popup
    lv_obj_t* high_temp_label_ = nullptr;  // High-temperature warning label

public:
    // Inherit constructor
    using SpiLcdDisplay::SpiLcdDisplay;

    void SetupHighTempWarningPopup() {
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        auto text_font = lvgl_theme->text_font()->font();
        // Create high-temperature warning popup
        high_temp_popup_ = lv_obj_create(lv_screen_active());  // Use current screen
        lv_obj_set_scrollbar_mode(high_temp_popup_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_size(high_temp_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
        lv_obj_align(high_temp_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(high_temp_popup_, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_radius(high_temp_popup_, 10, 0);
        
        // Create warning label
        high_temp_label_ = lv_label_create(high_temp_popup_);
        lv_label_set_text(high_temp_label_, "Warning: temperature too high");
        lv_obj_set_style_text_color(high_temp_label_, lv_color_white(), 0);
        lv_obj_center(high_temp_label_);
        
        // Hidden by default
        lv_obj_add_flag(high_temp_popup_, LV_OBJ_FLAG_HIDDEN);
    }

    void UpdateHighTempWarning(float chip_temp, float threshold = 75.0f) {
        if (high_temp_popup_ == nullptr) {
            ESP_LOGW("ZHENGCHEN_LcdDisplay", "High temp popup not initialized!");
            return;
        }

        if (chip_temp >= threshold) {
            ShowHighTempWarning();
        } else {
            HideHighTempWarning();
        }
    }

    void ShowHighTempWarning() {
        if (high_temp_popup_ && lv_obj_has_flag(high_temp_popup_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_remove_flag(high_temp_popup_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void HideHighTempWarning() {
        if (high_temp_popup_ && !lv_obj_has_flag(high_temp_popup_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(high_temp_popup_, LV_OBJ_FLAG_HIDDEN);
        }
    }
};



#endif // ZHENGCHEN_LCD_DISPLAY_H