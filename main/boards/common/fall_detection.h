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
#include <string>

#define FALL_TAG "FallDetection"
#define UART_BUF_SIZE (256)
#define TARGET_FALLEN_ID 3

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
            app.PlaySound(Lang::Sounds::OGG_LOW_BATTERY); 
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

        ESP_LOGW(FALL_TAG, "🚨 CRITICAL FALL DETECTED!");
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

    static void DetectionTask(void* pvParameters) {
        auto* self = static_cast<FallDetectionController*>(pvParameters);
        uint8_t* data = (uint8_t*)malloc(UART_BUF_SIZE);

        for (;;) {
            int len = uart_read_bytes(self->uart_num_, data, UART_BUF_SIZE - 1, pdMS_TO_TICKS(100));
            if (len > 0) {
                data[len] = '\0';
                // Replace with your specific JSON parsing or byte-check logic
                if (strstr((char*)data, "\"id\":2") != NULL) {
                    self->TriggerFallAlert();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        free(data);
    }

public:
    FallDetectionController(uart_port_t uart_bus, int tx, int rx) : uart_num_(uart_bus) {
        state_mutex_ = xSemaphoreCreateMutex();

        uart_config_t uart_config = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
        };
        uart_param_config(uart_num_, &uart_config);
        uart_set_pin(uart_num_, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        uart_driver_install(uart_num_, UART_BUF_SIZE * 2, 0, 0, NULL, 0);

        xTaskCreatePinnedToCore(DetectionTask, "FallDetTask", 4096, this, 1, nullptr, 1);
        ESP_LOGI(FALL_TAG, "Fall Detection Initialized via UART.");
    }

    ~FallDetectionController() {
        if (state_mutex_) vSemaphoreDelete(state_mutex_);
    }
};