#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lotusai_controller.h"
#include "led/single_led.h"
#include "medicine_reminder.h"
#include "fall_detection.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_xpt2046.h>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#define TAG "CompactWifiBoardLCDTouch"

// Global pointer so the touch poll callback can reach the LotusAiController.
static LotusAiController* g_lotusai = nullptr;
// Touch state handle and debounce flag used in the polling timer callback.
static esp_lcd_touch_handle_t s_touch_handle = nullptr;
static bool s_was_touched = false;

// -----------------------------------------------------------------------------
// Touch poll timer callback — runs in timer task context.
// Reads XPT2046 state and, on a fresh press, maps Y → recipe index.
// -----------------------------------------------------------------------------
static void TouchPollCallback(void* /*arg*/) {
    if (!g_lotusai || !s_touch_handle) return;

    // TEMP DEBUG: unconditional heartbeat so we can see the raw IRQ pin level
    // even when nothing is pressed. ~1x/sec at the 200ms poll period.
    // Remove once tap selection is confirmed working.
    static int s_debug_tick = 0;
    int irq_level = gpio_get_level(TOUCH_IRQ_PIN);
    if (++s_debug_tick >= 5) {
        s_debug_tick = 0;
        ESP_LOGD(TAG, "touch heartbeat: irq_level=%d", irq_level);
    }

    // PENIRQ is active-low: skip SPI when the panel is not touched to avoid
    // bus contention with the ILI9341 on the shared MOSI/SCK lines.
    if (irq_level != 0) {
        s_was_touched = false;
        return;
    }

    auto* display = Board::GetInstance().GetDisplay(); // Pointer to the display object (ILI9341)
    if (!display) return;

    DisplayLockGuard lock(display);
    esp_lcd_touch_read_data(s_touch_handle); // esp_lcd_touch_read_data defined in esp_lcd_touch.h

    esp_lcd_touch_point_data_t point[1] = {}; // Array of touch points (esp_lcd_touch.h)
    uint8_t count = 0; // Number of touch points
    esp_err_t err = esp_lcd_touch_get_data(s_touch_handle, point, &count, 1);
    bool touched = (err == ESP_OK && count > 0);

    // TEMP DEBUG: fires whenever IRQ is asserted, even if the SPI read didn't
    // resolve a valid point (helps distinguish gate 2 vs gate 3 failures).
    ESP_LOGI(TAG, "touch irq low: touched=%d count=%d x=%d y=%d",
             touched, count, point[0].x, point[0].y);

    if (touched) {
        if (!s_was_touched) {
            s_was_touched = true;
            int cx = static_cast<int>(point[0].x);
            int cy = static_cast<int>(point[0].y);
            int option_idx = g_lotusai->OptionFromPoint(cx, cy);
            ESP_LOGI(TAG, "tap x=%d y=%d idx=%d row_h=%d scroll_y=%d",
                     cx, cy, option_idx,
                     display->GetLotusRecipeRowHeight(),
                     display->GetLotusRecipeScrollY());
            if (option_idx >= 0) {
                // Schedule on the main application task to avoid concurrency issues
                Application::GetInstance().Schedule([option_idx]() {
                    if (!g_lotusai) return;
                    std::string result = g_lotusai->SelectByIndex(option_idx);
                    auto* display = Board::GetInstance().GetDisplay();
                    if (display) display->ShowNotification(result, 5000);
                });
            }
        }
    } else {
        s_was_touched = false;
    }
}

// -----------------------------------------------------------------------------
// Board class
// -----------------------------------------------------------------------------
class CompactWifiBoardLcdTouch : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_ = nullptr;
    FallDetectionController* fall_detector_ = nullptr;

    void InitializeEyeUart() {
        uart_config_t uart_config = {
            .baud_rate = EYE_UART_BAUD_RATE,
            .data_bits = UART_DATA_8_BITS,
            .parity    = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        ESP_ERROR_CHECK(uart_param_config(EYE_UART_PORT, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(EYE_UART_PORT, EYE_UART_TX_PIN, EYE_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        ESP_ERROR_CHECK(uart_driver_install(EYE_UART_PORT, 256, 0, 0, NULL, 0));
        
        ESP_LOGI(TAG, "DualEye UART initialized on TX GPIO %d", EYE_UART_TX_PIN);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num   = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num   = TOUCH_MISO_PIN;   // MISO required by XPT2046
        buscfg.sclk_io_num   = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Install LCD panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num      = DISPLAY_CS_PIN;
        io_config.dc_gpio_num      = DISPLAY_DC_PIN;
        io_config.spi_mode         = DISPLAY_SPI_MODE;
        io_config.pclk_hz          = 26 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits     = 8;
        io_config.lcd_param_bits   = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install ILI9341 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num  = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order   = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel  = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new SpiLcdDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeTouchscreen() {
        // Share the SPI3 bus that the LCD already uses (both CS pins are different)
        esp_lcd_panel_io_handle_t touch_io = nullptr;
        esp_lcd_panel_io_spi_config_t touch_io_cfg = {};
        touch_io_cfg.cs_gpio_num       = TOUCH_CS_PIN;
        touch_io_cfg.dc_gpio_num       = GPIO_NUM_NC;   // XPT2046 uses SPI mode, not 9-bit D/C
        touch_io_cfg.spi_mode          = 0;
        touch_io_cfg.pclk_hz           = 2 * 1000 * 1000;  // XPT2046 max ~2.5 MHz
        touch_io_cfg.trans_queue_depth = 3;
        touch_io_cfg.lcd_cmd_bits      = 8;
        touch_io_cfg.lcd_param_bits    = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &touch_io_cfg, &touch_io));

        esp_lcd_touch_config_t touch_cfg = {};
        touch_cfg.x_max          = DISPLAY_WIDTH - 1; // Index is 0-based, so -1 is the last pixel.
        touch_cfg.y_max          = DISPLAY_HEIGHT - 1;
        touch_cfg.rst_gpio_num   = GPIO_NUM_NC;
        touch_cfg.int_gpio_num   = TOUCH_IRQ_PIN;
        touch_cfg.levels.interrupt = 0;  // active low
        touch_cfg.flags.swap_xy  = TOUCH_SWAP_XY  ? 1u : 0u;
        touch_cfg.flags.mirror_x = TOUCH_MIRROR_X ? 1u : 0u;
        touch_cfg.flags.mirror_y = TOUCH_MIRROR_Y ? 1u : 0u;

        ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(touch_io, &touch_cfg, &s_touch_handle));

        // LotusAI uses TouchPollCallback below; do not call lvgl_port_add_touch()
        // here — it would poll XPT2046 from the LVGL task and fight the display
        // for the shared SPI bus (white screen flicker).

        // Start a 200 ms timer for tap detection / recipe selection
        esp_timer_create_args_t timer_args = {};
        timer_args.callback = &TouchPollCallback;
        timer_args.arg      = nullptr;
        timer_args.name     = "lotusai_touch";
        esp_timer_handle_t touch_timer = nullptr;
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &touch_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(touch_timer, 200 * 1000 /* µs */));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeTools() {
        static LotusAiController lotusai;
        g_lotusai = &lotusai;

        static MedicineReminderController medicine_reminder;

        // Fall Detection: Initialize as static to ensure it lives for the app's lifetime
        static FallDetectionController fall_detector(UART_NUM_1, 11, 12);
        fall_detector_ = &fall_detector;
    }

public:
    CompactWifiBoardLcdTouch() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeEyeUart(); // Init serial TX to DualEye
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeTools();
        InitializeTouchscreen();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
    }

    // Called by Application::SetDeviceState via Board::GetInstance().SendEyeCommand()
    virtual void SendEyeCommand(uint8_t cmd) override {
        uart_write_bytes(EYE_UART_PORT, reinterpret_cast<const char*>(&cmd), 1);
        ESP_LOGI(TAG, "Sent Eye Command: 0x%02X", cmd);
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK,  AUDIO_I2S_MIC_GPIO_WS,   AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(
                DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(CompactWifiBoardLcdTouch);
