#pragma once

#include "mcp_server.h"
#include "board.h"
#include "application.h"
#include "assets/lang_config.h"
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h> 
#include <stdlib.h> 
#include <stdio.h>

#define FALL_TAG "FallDetection"
#define UART_BUF_SIZE (512) 
#define TARGET_PERSON_ID 0           // '0' is the target ID for Person in YOLO
#define FALL_RATIO_THRESHOLD 1.2f    // If width/height > 1.2, trigger fall alert

class FallDetectionController {
private:
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

    void TriggerFallAlert() {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        if (alert_active_) {
            xSemaphoreGive(state_mutex_);
            return;
        }
        alert_active_ = true;
        xSemaphoreGive(state_mutex_);

        auto& app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateSpeaking || app.GetDeviceState() == kDeviceStateListening) {
            app.AbortSpeaking(kAbortReasonWakeWordDetected);
        }

        ESP_LOGW(FALL_TAG, "🚨 CRITICAL FALL DETECTED based on Bounding Box ratio!");

        // ---- ADD THE EYE COMMAND TRANSITION HERE ----
        // 1. Fetch your custom board using the global Board instance
        auto* board = static_cast<Board*>(&Board::GetInstance());
        if (board) {
            board->SendEyeCommand(0x04); // 0x04 = WARNING / FALL DETECTED
        }
        // ----------------------------------------------

        xTaskCreate(AlarmSoundTask, "FallAlarmSoundTask", 3072, NULL, 5, NULL);

        auto* display = Board::GetInstance().GetDisplay();
        if (display) display->SetChatMessage("system", "🚨 EMERGENCY: Fall detected!");

        app.SendMcpMessage("{\"event\":\"fall_detection\",\"status\":\"panic\",\"message\":\"Critical fall detected\"}");

        // Auto-reset after a delay
        vTaskDelay(pdMS_TO_TICKS(15000));
        
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        alert_active_ = false;
        xSemaphoreGive(state_mutex_);
    }

    void ProcessDetectionLine(const char* line) {
        // Find the "boxes" array in the JSON string
        const char* ptr = strstr(line, "\"boxes\"");
        if (!ptr) return;

        // Find the start of the arrays
        ptr = strchr(ptr, '[');
        if (!ptr) return;
        ptr++; // Skip the outer array bracket

        // Iterate through any inner arrays (e.g., [[x,y,w,h,s,t], [x,y,w,h,s,t]])
        while ((ptr = strchr(ptr, '[')) != nullptr) {
            int x, y, w, h, score, target;
            
            // Parse the 6 values from the array block
            if (sscanf(ptr, "[%d,%d,%d,%d,%d,%d]", &x, &y, &w, &h, &score, &target) == 6 ||
                sscanf(ptr, "[%d, %d, %d, %d, %d, %d]", &x, &y, &w, &h, &score, &target) == 6) {

                if (target == TARGET_PERSON_ID && w > 0 && h > 0) {
                    float ratio = (float)w / (float)h;
                    
                    // You can uncomment this next line to watch the live ratio in your serial monitor
                    // ESP_LOGI(FALL_TAG, "Person seen - w:%d h:%d ratio:%.2f conf:%d", w, h, ratio, score);
                    
                    if (ratio > FALL_RATIO_THRESHOLD) {
                        ESP_LOGI(FALL_TAG, "Fall criteria met! w=%d, h=%d, ratio=%.2f", w, h, ratio);
                        TriggerFallAlert();
                        return; // Alert triggered, skip parsing any remaining boxes
                    }
                }
            }
            ptr++; // Move past the current '[' to search for the next person
        }
    }

    static void DetectionTask(void* pvParameters) {
        auto* self = static_cast<FallDetectionController*>(pvParameters);
        uint8_t* data = (uint8_t*)malloc(UART_BUF_SIZE);
        
        char line_buffer[UART_BUF_SIZE];
        int line_pos = 0;
        
        char last_received_data[UART_BUF_SIZE] = "No data yet";
        int heartbeat_counter = 0;
        TickType_t last_poll_time = xTaskGetTickCount();

        for (;;) {
            // --- POLLING WORKAROUND ADDED HERE ---
            // Ping the sensor for a new frame every 100ms
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
            if (heartbeat_counter >= 300) { // Will now trigger roughly every 15 seconds based on 50ms ticks
                ESP_LOGI(FALL_TAG, "Heartbeat | Last observed state: %s", last_received_data);
                heartbeat_counter = 0;
            }

            vTaskDelay(pdMS_TO_TICKS(10)); // Reduced delay since uart_read_bytes has a timeout
        }
        free(data);
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