#pragma once

#include "mcp_server.h"
#include "board.h"
#include "application.h"
#include "display/lvgl_display/lvgl_display.h"
#include "assets/lang_config.h"

#include <sscma_client.h> // Native ESP-IDF SenseCraft NPU client
#include <driver/uart.h>  // Native ESP-IDF UART drivers
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string>

#define FALL_DET_TAG "FallDetection"
#define FALL_DET_ALERT_COOLDOWN_MS 5000

#define SENSECRAFT_CLASS_STANDING 0
#define SENSECRAFT_CLASS_FALL     1
#define SENSECRAFT_CLASS_SITTING  2

class FallDetectionController {
private:
    SscmaClient grove_ai_; // Swapped to native ESP-IDF client class
    int confidence_threshold_; 
    bool alert_active_;
    uint32_t last_alert_time_;
    SemaphoreHandle_t state_mutex_ = nullptr;
    
    static void AlarmSoundTask(void* pvParameters) {
        auto* self = static_cast<FallDetectionController*>(pvParameters);
        auto& app = Application::GetInstance();
        
        ESP_LOGW(FALL_DET_TAG, "Alarm thread locked. Freezing voice assistant interactions.");

        while (self->IsAlertActive()) {
            if (app.GetDeviceState() == kDeviceStateSpeaking ||
                app.GetDeviceState() == kDeviceStateListening) {
                app.AbortSpeaking(kAbortReasonWakeWordDetected);
            }

            app.PlaySound(Lang::Sounds::OGG_LOW_BATTERY); 
            vTaskDelay(pdMS_TO_TICKS(1500)); 
        }

        ESP_LOGI(FALL_DET_TAG, "Alarm cleared. Restoring speech states.");
        vTaskDelete(NULL);
    }
    
    void TriggerFallAlert() {
        uint32_t trigger_time = esp_log_timestamp();

        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        alert_active_ = true;
        last_alert_time_ = trigger_time;
        xSemaphoreGive(state_mutex_);
        
        auto& app = Application::GetInstance();
        
        if (app.GetDeviceState() == kDeviceStateSpeaking ||
            app.GetDeviceState() == kDeviceStateListening) {
            app.AbortSpeaking(kAbortReasonWakeWordDetected);
        }

        xTaskCreate(AlarmSoundTask, "FallAlarmSoundTask", 3072, this, 5, NULL);
        
        ESP_LOGW(FALL_DET_TAG, "🚨 CRITICAL FALL DETECTED via SenseCraft NPU inference!");
        
        auto* display = Board::GetInstance().GetDisplay();
        if (display) {
            display->SetChatMessage("system", "🚨 EMERGENCY: Fall detected! Speech frozen.");
        }
        
        std::string payload = 
            "{"
            "\"event\":\"fall_detection\","
            "\"status\":\"panic\","
            "\"message\":\"CRITICAL: Fall detected by vision system. Device interface locked.\""
            "}";
            
        app.SendMcpMessage(payload);
    }
    
    static void DetectionTask(void* pvParameters) {
        auto* self = static_cast<FallDetectionController*>(pvParameters);
        
        for (;;) {
            // Native client polling syntax
            if (self->grove_ai_.beginInference() == ESP_OK) { 
                
                xSemaphoreTake(self->state_mutex_, portMAX_DELAY);
                int target_thresh = self->confidence_threshold_;
                bool is_alert_active = self->alert_active_;
                uint32_t last_alert = self->last_alert_time_;
                xSemaphoreGive(self->state_mutex_);

                // Native API variant reading bounding boxes
                auto boxes = self->grove_ai_.getBoxes();
                for (size_t i = 0; i < boxes.size(); i++) {
                    int class_id   = boxes[i].target; 
                    int confidence = boxes[i].score;  

                    if (class_id == SENSECRAFT_CLASS_FALL && confidence >= target_thresh) {
                        if (!is_alert_active && (esp_log_timestamp() - last_alert > FALL_DET_ALERT_COOLDOWN_MS)) {
                            self->TriggerFallAlert();
                            break; 
                        }
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100)); 
        }
    }

public:
    // Accepts an ESP-IDF native UART port number (e.g. UART_NUM_1 or UART_NUM_2)
    FallDetectionController(uart_port_t uart_bus) 
        : confidence_threshold_(70), 
          alert_active_(false),
          last_alert_time_(0) {
        
        state_mutex_ = xSemaphoreCreateMutex();
        
        // Native initialization hook directly targeting the hardware port
        grove_ai_.init(uart_bus);
        
        McpServer::GetInstance().AddTool(
            "self.fall_detection.set_sensitivity",
            "Adjust target fall detection confidence limits (1-100, lower matches rough postures easily).",
            PropertyList({
                Property("sensitivity", kPropertyTypeInteger, 70, 1, 100),
            }),
            [this](const PropertyList& props) -> ReturnValue {
                xSemaphoreTake(state_mutex_, portMAX_DELAY);
                confidence_threshold_ = props["sensitivity"].value<int>();
                int current_val = confidence_threshold_;
                xSemaphoreGive(state_mutex_);
                
                ESP_LOGI(FALL_DET_TAG, "SenseCraft classification matching threshold pushed to: %d%%", current_val);
                return "Confidence threshold successfully changed.";
            }
        );
        
        McpServer::GetInstance().AddTool(
            "self.fall_detection.clear_alert",
            "Clear active hardware panic triggers and restore speech mechanics.",
            PropertyList({}),
            [this](const PropertyList&) -> ReturnValue {
                xSemaphoreTake(state_mutex_, portMAX_DELAY);
                alert_active_ = false;
                xSemaphoreGive(state_mutex_);
                
                auto* display = Board::GetInstance().GetDisplay();
                if (display) {
                    display->SetChatMessage("system", "Fall alert cleared.");
                }
                return "Alert state cleared.";
            }
        );
        
        xTaskCreatePinnedToCore(
            DetectionTask,
            "GroveFallDetTask",
            4096,
            this,
            1,
            nullptr,
            1
        );
        
        ESP_LOGI(FALL_DET_TAG, "SenseCraft Vision NPU integration active (Native).");
    }
    
    ~FallDetectionController() {
        if (state_mutex_) vSemaphoreDelete(state_mutex_);
    }
    
    bool IsAlertActive() { 
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        bool active = alert_active_;
        xSemaphoreGive(state_mutex_);
        return active; 
    }
};