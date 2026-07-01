#pragma once

#include "mcp_server.h"
#include "board.h"
#include "application.h"
#include "display/lvgl_display/lvgl_display.h"
#include "camera.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <vector>
#include <string>

#define FALL_DET_TAG "FallDetection"

// -------------------------------------------------------------------
// Configuration: Adjust these to tune detection speed and behavior
// -------------------------------------------------------------------
#define FALL_DET_FRAME_WIDTH 160           // Downsampled width for CPU-efficient processing
#define FALL_DET_FRAME_HEIGHT 120          // Downsampled height for CPU-efficient processing
#define FALL_DET_SAMPLE_INTERVAL_MS 1000   // Time between frame analyses (1 second)
#define FALL_DET_MOTION_THRESHOLD 25       // Baseline pixel variance required to flag motion (0-255)
#define FALL_DET_ALERT_COOLDOWN_MS 5000    // Minimum duration between sequential alerts to stop flood logs

// -------------------------------------------------------------------
// Minimal grayscale frame buffer (8 bits per pixel)
// -------------------------------------------------------------------
struct GrayscaleFrame {
    uint8_t data[FALL_DET_FRAME_WIDTH * FALL_DET_FRAME_HEIGHT];
    
    // Bounds-checked pixel accessor to prevent memory corruption during calculation
    uint8_t& at(int x, int y) {
        if (x < 0 || x >= FALL_DET_FRAME_WIDTH || y < 0 || y >= FALL_DET_FRAME_HEIGHT)
            return data[0]; // out of bounds safety fallback
        return data[y * FALL_DET_FRAME_WIDTH + x];
    }
};

// -------------------------------------------------------------------
// FallDetectionController
// 
// An isolated, hardware-accelerated module running on Core 1 that 
// downsamples real-time RGB565 frames to grayscale, filters out global 
// noise, and tracks quadrant motion dynamics to identify sudden falls.
// -------------------------------------------------------------------
class FallDetectionController {
private:
    Camera* camera_;
    GrayscaleFrame current_frame_;
    GrayscaleFrame previous_frame_;
    int motion_threshold_;
    bool alert_active_;
    uint32_t last_alert_time_;
    
    /**
     * @brief Computes the average global frame change.
     * Evaluates the absolute delta of each corresponding pixel between the current 
     * frame and the last historical frame to create an aggregate motion score.
     */
    int ComputeMotionScore() {
        int changes = 0;
        int pixel_count = FALL_DET_FRAME_WIDTH * FALL_DET_FRAME_HEIGHT;
        
        for (int i = 0; i < pixel_count; i++) {
            int diff = abs((int)current_frame_.data[i] - (int)previous_frame_.data[i]);
            changes += diff;
        }
        
        return changes / pixel_count; // average change per pixel
    }
    
    /**
     * @brief Discriminates local signatures from global noise (e.g. ambient light changes).
     * Splits the low-res frame matrix into 4 logical quadrants. If the localized changes
     * in a single quadrant cross the scaled sensitivity threshold, it signifies a 
     * concentrated motion vector typical of a body falling.
     */
    bool HasConcentratedMotion() {
        int qw = FALL_DET_FRAME_WIDTH / 2;
        int qh = FALL_DET_FRAME_HEIGHT / 2;
        
        int q_motion[4] = {0, 0, 0, 0};
        
        for (int y = 0; y < FALL_DET_FRAME_HEIGHT; y++) {
            for (int x = 0; x < FALL_DET_FRAME_WIDTH; x++) {
                int diff = abs((int)current_frame_.at(x, y) - (int)previous_frame_.at(x, y));
                int qx = x < qw ? 0 : 1;
                int qy = y < qh ? 0 : 1;
                q_motion[qy * 2 + qx] += diff;
            }
        }
        
        // Find the quadrant experiencing the most intense motion activity
        int max_motion = 0;
        for (int i = 0; i < 4; i++) {
            if (q_motion[i] > max_motion) {
                max_motion = q_motion[i];
            }
        }
        
        // Return true if the most active single quadrant crosses the delta threshold
        int threshold = (FALL_DET_FRAME_WIDTH * FALL_DET_FRAME_HEIGHT * motion_threshold_) / 4;
        return max_motion > threshold;
    }
    
    /**
     * @brief Converts raw RGB to standard Grayscale using CCIR 601 luminance coefficients.
     * Fixed-point bit-shifting implementation avoids expensive hardware floating-point operations.
     */
    static uint8_t RgbToGrayscale(uint8_t r, uint8_t g, uint8_t b) {
        // Standard luminance formula: 0.299*R + 0.587*G + 0.114*B
        return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
    }
    
    /**
     * @brief Manages hardware interaction and pixel format manipulation.
     * Cycles the history buffer, dynamically fetches a real-time high-resolution 
     * RGB565 frame from the offline driver instance, and downsamples it via nearest-neighbor
     * strides directly into the local 160x120 grayscale map.
     */
    void CaptureAndProcess() {
        std::memcpy(previous_frame_.data, current_frame_.data, sizeof(current_frame_.data));
        
        // 1. Fetch generic camera interface from the global board state
        auto* base_cam = Board::GetInstance().GetCamera();
        if (!base_cam) {
            ESP_LOGW(FALL_DET_TAG, "No camera available");
            return;
        }
        
        // 2. Cast down to the specialized real-time fall detection camera frame variant
        auto* cam = dynamic_cast<Esp32CameraFall*>(base_cam);
        if (!cam) {
            ESP_LOGE(FALL_DET_TAG, "Active camera is not an Esp32CameraFall instance!");
            return;
        }
        
        // 3. Extract camera hardware frame buffer and process bit-packed pixel arrays
        camera_fb_t* fb = cam->GetFrame();
        if (fb && fb->format == PIXFORMAT_RGB565) {
            uint16_t* rgb565 = (uint16_t*)fb->buf;
            
            // Calculate step size based on how much larger the incoming frame is
            int stride_x = fb->width / FALL_DET_FRAME_WIDTH;
            int stride_y = fb->height / FALL_DET_FRAME_HEIGHT;

            for (int y = 0; y < FALL_DET_FRAME_HEIGHT; y++) {
                for (int x = 0; x < FALL_DET_FRAME_WIDTH; x++) {
                    int src_x = x * stride_x;
                    int src_y = y * stride_y;
                    uint16_t pixel = rgb565[src_y * fb->width + src_x];
                    
                    // Decode standard 5-6-5 packed bit layouts into discrete color spaces
                    uint8_t r = (pixel >> 11) & 0x1F;
                    uint8_t g = (pixel >> 5) & 0x3F;
                    uint8_t b = pixel & 0x1F;
                    
                    // Rescale color channels up to consistent 8-bit widths (0-255)
                    r = (r << 3) | (r >> 2);
                    g = (g << 2) | (g >> 4);
                    b = (b << 3) | (b >> 2);
                    
                    current_frame_.at(x, y) = RgbToGrayscale(r, g, b);
                }
            }
        }
        
        // 4. Return hardware resources back to driver pool to avoid memory deadlock
        cam->ReturnFrame(fb);
    }
    
    /**
     * @brief Evaluates computed frames against algorithm rules.
     * Fires a warning trigger if high global motion matches localized quadrant concentration 
     * and the safety tracking cooldown period has expired.
     */
    void AnalyzeFrame() {
        int motion = ComputeMotionScore();
        bool concentrated = HasConcentratedMotion();
        
        ESP_LOGD(FALL_DET_TAG, "Motion score: %d, Concentrated: %d", motion, concentrated);
        
        if (motion > motion_threshold_ && concentrated && !alert_active_) {
            uint32_t now = esp_log_timestamp();
            if (now - last_alert_time_ > FALL_DET_ALERT_COOLDOWN_MS) {
                TriggerFallAlert();
            }
        }
    }
    
    /**
     * @brief Dispatches localized UI state transformations.
     * Asserts the critical panic banner to the LVGL chat panel layout and pushes hardware warning logs.
     */
    void TriggerFallAlert() {
        alert_active_ = true;
        last_alert_time_ = esp_log_timestamp();
        
        auto* display = Board::GetInstance().GetDisplay();
        if (display) {
            display->SetChatMessage("system", "⚠️ FALL DETECTED: Check on user immediately!");
        }
        
        ESP_LOGW(FALL_DET_TAG, "FALL ALERT triggered at %lu ms", last_alert_time_);
    }
    
    /**
     * @brief FreeRTOS Task entry loop.
     * Runs continuously in the background on its assigned core, driving the 
     * sequential capture, transformation, and evaluation pass.
     */
    static void DetectionTask(void* pvParameters) {
        auto* self = static_cast<FallDetectionController*>(pvParameters);
        
        for (;;) {
            self->CaptureAndProcess();
            self->AnalyzeFrame();
            
            vTaskDelay(pdMS_TO_TICKS(FALL_DET_SAMPLE_INTERVAL_MS));
        }
    }

public:
    /**
     * @brief Constructor - Prepares memory boundaries and wires remote agent hooks.
     * Sets defaults, clears matrix allocations, pins execution strictly to Core 1, 
     * and maps runtime RPC configurations to the Model Context Protocol (MCP) server.
     */
    FallDetectionController() 
        : camera_(nullptr),
          motion_threshold_(FALL_DET_MOTION_THRESHOLD),
          alert_active_(false),
          last_alert_time_(0) {
        
        std::memset(current_frame_.data, 0, sizeof(current_frame_.data));
        std::memset(previous_frame_.data, 0, sizeof(previous_frame_.data));
        
        // Expose sensitivity tracking configuration straight to the remote MCP engine
        McpServer::GetInstance().AddTool(
            "self.fall_detection.set_sensitivity",
            "Adjust fall detection sensitivity (1-100, higher = more sensitive).",
            PropertyList({
                Property("sensitivity", kPropertyTypeInteger, 50, 1, 100),
            }),
            [this](const PropertyList& props) -> ReturnValue {
                int sensitivity = props["sensitivity"].value<int>();
                // Invert scale: higher sensitivity user input yields lower pixel delta thresholds
                motion_threshold_ = (255 * (101 - sensitivity)) / 100; 
                ESP_LOGI(FALL_DET_TAG, "Sensitivity set to %d (threshold: %d)",
                         sensitivity, motion_threshold_);
                return "Sensitivity updated.";
            }
        );
        
        // Expose a clear-down endpoint over the network to reset active hardware triggers
        McpServer::GetInstance().AddTool(
            "self.fall_detection.clear_alert",
            "Clear the active fall alert.",
            PropertyList({}),
            [this](const PropertyList&) -> ReturnValue {
                alert_active_ = false;
                auto* display = Board::GetInstance().GetDisplay();
                if (display) {
                    display->SetChatMessage("system", "Fall alert cleared.");
                }
                return "Alert cleared.";
            }
        );
        
        // Spawn asynchronous supervisor loop on Core 1 (leaving Core 0 for rendering)
        xTaskCreatePinnedToCore(
            DetectionTask,
            "FallDetTask",
            4096,  // stack size allocations
            this,
            1,     // priority mapping
            nullptr,
            1      // Pinned to Core 1
        );
        
        ESP_LOGI(FALL_DET_TAG, "Fall detection initialized (sensitivity: %d)",
                 motion_threshold_);
    }
    
    bool IsAlertActive() const { return alert_active_; }
    int GetMotionThreshold() const { return motion_threshold_; }
};