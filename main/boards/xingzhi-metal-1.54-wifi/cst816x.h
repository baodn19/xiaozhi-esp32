#ifndef _CST816X_H_
#define _CST816X_H_

#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c.h"
#include "i2c_device.h"
#include <driver/i2c_master.h>
#include <sys/time.h>
#include <array>

#define ES8311_VOL_MIN 0
#define ES8311_VOL_MAX 100

enum class TouchEventType {
    SINGLE_CLICK,    // Single click event
    DOUBLE_CLICK,    // Double click event
    LONG_PRESS_START,// Long press start event
    LONG_PRESS_END   // Long press end event
};

struct TouchEvent {
    TouchEventType type;  
    int x;                
    int y;               
};

class Cst816x : public I2cDevice {
private:
    struct TouchPoint_t {
        int num = 0; 
        int x = -1;   
        int y = -1;   
    };

    struct TouchThresholdConfig {
        int x;                          // Target X coordinate
        int y;                          // Target Y coordinate
        int64_t single_click_thresh_us; // Max single-click duration (us)
        int64_t double_click_window_us; // Double-click window (us)
        int64_t long_press_thresh_us;   // Long press threshold (us)
    };

    const TouchThresholdConfig DEFAULT_THRESHOLD = {
        .x = -1, .y = -1,                  
        .single_click_thresh_us = 120000,  // 150ms
        .double_click_window_us = 240000,  // 150ms
        .long_press_thresh_us = 4000000    // 4000ms
    };

    const std::array<TouchThresholdConfig, 3> TOUCH_THRESHOLD_TABLE = {
        {
            {20, 600, 200000, 240000, 2000000}, // Volume+
            {40, 600, 200000, 240000, 4000000}, // Boot button
            {60, 600, 200000, 240000, 2000000}  // Volume-
        }
    };

    const TouchThresholdConfig& getThresholdConfig(int x, int y);

    uint8_t* read_buffer_ = nullptr;  
    TouchPoint_t tp_;                 

    bool is_touching_ = false;              
    int64_t touch_start_time_ = 0;          // Touch start time (us)
    int64_t last_release_time_ = 0;         // Last release time (us)
    int click_count_ = 0;                   // Click count (for double-click detection)
    bool long_press_started_ = false;       // Whether long press has been triggered

    bool is_volume_long_pressing_ = false;   // Whether volume is being adjusted via long press
    int volume_long_press_dir_ = 0;          // Adjustment direction: 1=increase, -1=decrease
    int64_t last_volume_adjust_time_ = 0;    // Last volume adjustment time (us)
    const int64_t VOL_ADJ_INTERVAL_US = 200000; // Volume adjustment interval (100ms)
    const int VOL_ADJ_STEP = 5;                // Step size per adjustment

    int64_t getCurrentTimeUs();

public:
    Cst816x(i2c_master_bus_handle_t i2c_bus, uint8_t addr);
    ~Cst816x();

    void InitCst816d();
    void UpdateTouchPoint();
    void resetTouchCounters();
    static void touchpad_daemon(void* param);
    
    const TouchPoint_t& GetTouchPoint() { return tp_; }
};

#endif