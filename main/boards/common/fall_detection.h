#pragma once

#include "mcp_server.h"
#include "board.h"
#include "application.h"
#include "assets/lang_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h> 
#include <stdlib.h> 
#include <stdio.h>
#include <vector>
#include <cmath>


#define FALL_TAG "FallDetection"
#define UART_BUF_SIZE (512) 
#define TARGET_PERSON_ID 0           // '0' is the target ID for Person in YOLO
#define FALL_RATIO_THRESHOLD 1.2f    // If width/height > 1.2, trigger fall alert
#define MAX_TRACKED_PEOPLE 5
#define DISTANCE_THRESHOLD 60.0    // Max pixels a person can move between 200ms frames
#define SUSTAINED_FALL_MS 1500     // How long (in ms) a person must remain fallen
#define MAX_MISSING_FRAMES 3       // Allow a person to vanish for ~600ms before deleting them
#define VELOCITY_Y_THRESHOLD  40   // Pixels dropped per second (adjust for camera distance)
#define RATIO_SHIFT_THRESHOLD 0.6f // Minimum spike in width/height ratio between frames

struct TrackedPerson {
    int id;
    int x;   int y;
    int w;   int h;
    int last_y;            // Tracks previous frame's Y position
    float last_ratio;      // Tracks previous frame's aspect ratio
    uint32_t last_time;    // Tracks exact tick timestamp
    int missing_frames;
    bool active;
    float ratio;
}; // keep track of people in scene

class FallDetectionController {
public:
    // Retrieve the stored count (defaults to 0 if key does not exist)
    uint32_t GetLocalFallCount() {
        nvs_handle_t nvs_handle;
        uint32_t count = 0;
        if (nvs_open("fall_tracker", NVS_READONLY, &nvs_handle) == ESP_OK) {
            nvs_get_u32(nvs_handle, "total_falls", &count);
            nvs_close(nvs_handle);
        }
        return count;
    }
private:
    TrackedPerson tracked_people_[MAX_TRACKED_PEOPLE] = {0};
    int next_track_id_ = 1;

    uart_port_t uart_num_;
    bool alert_active_ = false;
    SemaphoreHandle_t state_mutex_ = nullptr;

    static void AlarmSoundTask(void* pvParameters) {
        auto& app = Application::GetInstance();
        TickType_t start_time = xTaskGetTickCount();
        const TickType_t duration = pdMS_TO_TICKS(20000); // 20 seconds

        while ((xTaskGetTickCount() - start_time) < duration) {
            app.PlaySound(Lang::Sounds::OGG_SUCCESS); 
            vTaskDelay(pdMS_TO_TICKS(2000)); 
        }
        vTaskDelete(NULL);
    }

    // Increment and save the new count to flash
    uint32_t IncrementLocalFallCount() {
        nvs_handle_t nvs_handle;
        uint32_t count = GetLocalFallCount() + 1;
        
        if (nvs_open("fall_tracker", NVS_READWRITE, &nvs_handle) == ESP_OK) {
            nvs_set_u32(nvs_handle, "total_falls", count);
            nvs_commit(nvs_handle); // Write change to flash
            nvs_close(nvs_handle);
        }
        return count;
    }

    void TriggerFallAlert() {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        if (alert_active_) {
            xSemaphoreGive(state_mutex_);
            return;
        }
        alert_active_ = true;
        xSemaphoreGive(state_mutex_);

        uint32_t total_falls = IncrementLocalFallCount(); // increment fall count

        auto& app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateSpeaking || app.GetDeviceState() == kDeviceStateListening) {
            app.AbortSpeaking(kAbortReasonWakeWordDetected);
        }

        ESP_LOGW(FALL_TAG, "🚨 CRITICAL FALL DETECTED based on Bounding Box ratio!");

        auto* board = static_cast<Board*>(&Board::GetInstance());
        if (board) {
            board->SendEyeCommand(0x04); // 0x04 = WARNING / FALL DETECTED
        }

        xTaskCreate(AlarmSoundTask, "FallAlarmSoundTask", 3072, &Application::GetInstance(), 5, NULL);

        auto* display = Board::GetInstance().GetDisplay();
        if (display) display->SetChatMessage("system", "🚨 EMERGENCY: Fall detected!");

        std::string mcp_payload = "{"
            "\"jsonrpc\":\"2.0\","
            "\"method\":\"notifications/event\","
            "\"params\":{"
                "\"event\":\"fall_detection\","
                "\"status\":\"panic\","
                "\"message\":\"Critical fall detected\","
                "\"total_falls\":" + std::to_string(total_falls) +
            "}"
        "}";

        app.SendMcpMessage(mcp_payload);

        // Auto-reset after a delay
        vTaskDelay(pdMS_TO_TICKS(15000));
        
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        alert_active_ = false;
        xSemaphoreGive(state_mutex_);
    }

    // Helper function to calculate Euclidean distance between two bounding box centers
    float GetBoxDistance(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
        float cx1 = x1 + (w1 / 2.0f);
        float cy1 = y1 + (h1 / 2.0f);
        float cx2 = x2 + (w2 / 2.0f);
        float cy2 = y2 + (h2 / 2.0f);
        return std::sqrt(std::pow(cx1 - cx2, 2) + std::pow(cy1 - cy2, 2));
    }

    void ProcessDetectionLine(const char* line) {
        const char* ptr = strstr(line, "\"boxes\"");
        if (!ptr) return;

        ptr = strchr(ptr, '[');
        if (!ptr) return;
        ptr++; // Skip outer array bracket

        // Temporary storage for boxes found strictly in THIS current frame
        struct RawBox { int x, y, w, h, score; float ratio; };
        std::vector<RawBox> current_frame_boxes;

        // Phase 1: Parse all boxes out of the JSON string
        while ((ptr = strchr(ptr, '[')) != nullptr) {
            int x = 0, y = 0, w = 0, h = 0, score = 0, target = 0;

            if (sscanf(ptr, "[%d,%d,%d,%d,%d,%d]", &x, &y, &w, &h, &score, &target) == 6 ||
                sscanf(ptr, "[%d, %d, %d, %d, %d, %d]", &x, &y, &w, &h, &score, &target) == 6) {
                
                if (target == TARGET_PERSON_ID && w > 0 && h > 0) {
                    float ratio = (float)w / (float)h;
                    current_frame_boxes.push_back({x, y, w, h, score, ratio});
                }
            }

            const char* close_bracket = strchr(ptr, ']');
            if (close_bracket) {
                ptr = close_bracket + 1;
            } else {
                ptr++;
            }
        }

        // Phase 2: Centroid Tracking Match
        // 1. Move 'now' to function scope so Phase 2 and the cleanup loop can both access it
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        for (const auto& box : current_frame_boxes) {
            int best_index = -1;
            float min_distance = DISTANCE_THRESHOLD;

            for (int i = 0; i < MAX_TRACKED_PEOPLE; i++) {
                if (!tracked_people_[i].active) continue;

                float dist = GetBoxDistance(box.x, box.y, box.w, box.h, 
                                            tracked_people_[i].x, tracked_people_[i].y, 
                                            tracked_people_[i].w, tracked_people_[i].h);
                if (dist < min_distance) {
                    min_distance = dist;
                    best_index = i;
                }
            }

            if (best_index != -1) {
                // --- CALCULATE ABRUPT TRANSITION ---
                TrackedPerson& p = tracked_people_[best_index];
                float delta_time = (now - p.last_time) / 1000.0f; // Convert ms to seconds
                
                if (delta_time > 0.01f) {
                    // 1. Calculate vertical velocity (y increases as you move down the screen)
                    float velocity_y = (box.y - p.last_y) / delta_time; 
                    
                    // 2. Calculate how fast the box flipped from vertical to horizontal
                    float current_ratio = (float)box.w / (float)box.h;
                    float ratio_velocity = (current_ratio - p.last_ratio) / delta_time;

                    // Debug logger to tune your environment thresholds
                    ESP_LOGD(FALL_TAG, "ID %d | Y-Vel: %.1f | Ratio-Vel: %.2f | Ratio: %.2f", 
                            p.id, velocity_y, ratio_velocity, current_ratio);

                    // CRITERIA: High downward speed + fast horizontal flattening + is currently flat
                    if (velocity_y > VELOCITY_Y_THRESHOLD && 
                        ratio_velocity > RATIO_SHIFT_THRESHOLD && 
                        current_ratio > FALL_RATIO_THRESHOLD) {
                        
                        ESP_LOGE(FALL_TAG, "ABRUPT FALL DETECTED! ID %d dropped at %.1f px/s", p.id, velocity_y);
                        TriggerFallAlert();
                    }
                }

                // Save current states as historical markers for the next frame
                p.last_y = p.y;
                p.last_ratio = p.ratio;
                p.last_time = now;

                // Store new positions
                p.x = box.x; p.y = box.y; p.w = box.w; p.h = box.h;
                p.ratio = (float)box.w / (float)box.h;
                p.missing_frames = 0;

            } else {
                // Register brand new person track initialization
                for (int i = 0; i < MAX_TRACKED_PEOPLE; i++) {
                    if (!tracked_people_[i].active) {
                        tracked_people_[i].id = next_track_id_++;
                        tracked_people_[i].x = box.x;
                        tracked_people_[i].y = box.y;
                        tracked_people_[i].w = box.w;
                        tracked_people_[i].h = box.h;
                        tracked_people_[i].ratio = box.ratio;
                        tracked_people_[i].last_y = box.y;
                        tracked_people_[i].last_ratio = box.ratio;
                        tracked_people_[i].last_time = now;
                        tracked_people_[i].missing_frames = 0;
                        tracked_people_[i].active = true;
                        break;
                    }
                }
            }
        }

        // Mark un-updated tracks as missing
        for (int i = 0; i < MAX_TRACKED_PEOPLE; i++) {
            if (tracked_people_[i].active && tracked_people_[i].last_time != now) {
                tracked_people_[i].missing_frames++;
                if (tracked_people_[i].missing_frames > MAX_MISSING_FRAMES) {
                    tracked_people_[i].active = false;
                }
            }
        }
    }


    static void DetectionTask(void* pvParameters) {
        auto* self = static_cast<FallDetectionController*>(pvParameters);
        uint8_t data[UART_BUF_SIZE];
        
        char line_buffer[UART_BUF_SIZE];
        int line_pos = 0;
        
        char last_received_data[UART_BUF_SIZE] = "No data yet";
        int heartbeat_counter = 0;
        TickType_t last_poll_time = xTaskGetTickCount();

        for (;;) {
            // Ping the sensor for a new frame every 200ms
            if ((xTaskGetTickCount() - last_poll_time) > pdMS_TO_TICKS(200)) {
                const char* poll_cmd = "AT+INVOKE=1,0,1\r"; // Single-shot JSON request
                uart_write_bytes(self->uart_num_, poll_cmd, strlen(poll_cmd));
                last_poll_time = xTaskGetTickCount();
            }

            int len = uart_read_bytes(self->uart_num_, data, UART_BUF_SIZE - 1, pdMS_TO_TICKS(50));
            
            if (len > 0) {
                for (int i = 0; i < len; i++) {
                    char c = data[i];
                    if (c == '\n' || c == '\r') {
                        if (line_pos > 0) {
                            line_buffer[line_pos] = '\0';
                            
                            strncpy(last_received_data, line_buffer, UART_BUF_SIZE - 1);
                            self->ProcessDetectionLine(line_buffer);
                            line_pos = 0;
                        }
                    } else if (line_pos < (sizeof(line_buffer) - 1)) {
                        line_buffer[line_pos++] = c;
                    }
                }
            }

            heartbeat_counter++;
            if (heartbeat_counter >= 300) { // Triggers roughly every 15 seconds
                ESP_LOGI(FALL_TAG, "Heartbeat | Last observed state: %s", last_received_data);
                heartbeat_counter = 0;
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }
        // REMOVED free(data) - data is a stack-allocated array, calling free on it crashes FreeRTOS!
    }

public:
    FallDetectionController(uart_port_t uart_bus, int tx, int rx) : uart_num_(uart_bus) {
        state_mutex_ = xSemaphoreCreateMutex();

        uart_config_t uart_config = {
            .baud_rate = 921600,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
        };
        uart_param_config(uart_num_, &uart_config);
        uart_set_pin(uart_num_, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        
        uart_driver_install(uart_num_, UART_BUF_SIZE * 2, 0, 0, NULL, 0);

        vTaskDelay(pdMS_TO_TICKS(2000)); 

        const char* query_cmd = "AT+MODELS?\r";
        uart_write_bytes(uart_num_, query_cmd, strlen(query_cmd));

        vTaskDelay(pdMS_TO_TICKS(1000)); 

        xTaskCreatePinnedToCore(DetectionTask, "FallDetTask", 4096, this, 1, nullptr, 1);
        ESP_LOGI(FALL_TAG, "Fall Detection Initialized via UART (YOLO Array Mode).");
    }

    ~FallDetectionController() {
        if (state_mutex_) vSemaphoreDelete(state_mutex_);
    }
};