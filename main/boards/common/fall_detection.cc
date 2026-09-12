#include "fall_detection.h"

#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "display.h"

#include <nvs.h>
#include <nvs_flash.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define TAG "FallDetection"
#define UART_BUF_SIZE (1024) // 15 people detection, ~50 bytes/ person, total ~750 bytes
#define TARGET_PERSON_ID 0           // '0' is the target ID for Person in YOLO

// Boxes below this confidence are dropped before association, so low-confidence jitter never
// reaches the tracker. Mirrors the detect_threshold gate in sensecap-watcher/sscma_camera.cc:118.
// NOTE: the score scale is not yet confirmed for this model -- SSCMA reports 0..100, but that must
// be verified against a real FDLOG line before this number means anything.
#define MIN_BOX_SCORE 40

// How long the alert stays suppressed after firing, so one fall cannot re-trigger per frame.
#define ALERT_COOLDOWN_MS 15000

// Dumps every raw UART line (and poll send time) as FDLOG/FDPOLL entries for
// offline replay. Disable for normal field operation to cut UART log noise.
#define FD_CAPTURE_MODE 1

// DetectionTask holds two UART_BUF_SIZE buffers on its stack (2 KB at 1024) and
// calls into sscanf / ESP_LOGI, each of which needs ~1 KB of frame on top.
static constexpr uint32_t kDetectionTaskStackSize = 6144;

uint32_t FallDetectionController::GetLocalFallCount() {
    nvs_handle_t nvs_handle;
    uint32_t count = 0;
    if (nvs_open("fall_tracker", NVS_READONLY, &nvs_handle) == ESP_OK) {
        nvs_get_u32(nvs_handle, "total_falls", &count);
        nvs_close(nvs_handle);
    }
    return count;
}

void FallDetectionController::AlarmSoundTask(void* /*pvParameters*/) {
    auto& app = Application::GetInstance();
    TickType_t start_time = xTaskGetTickCount();
    const TickType_t duration = pdMS_TO_TICKS(20000); // 20 seconds

    while ((xTaskGetTickCount() - start_time) < duration) {
        app.PlaySound(Lang::Sounds::OGG_SUCCESS);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    vTaskDelete(NULL);
}

uint32_t FallDetectionController::IncrementLocalFallCount() {
    nvs_handle_t nvs_handle;
    uint32_t count = GetLocalFallCount() + 1;

    if (nvs_open("fall_tracker", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u32(nvs_handle, "total_falls", count);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    return count;
}

void FallDetectionController::TriggerFallAlert(uint32_t now_ms, const TrackedPerson& track, const char* reason) {
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    if (alert_cooldown_until_ms_ != 0 && now_ms < alert_cooldown_until_ms_) {
        xSemaphoreGive(state_mutex_);
        return;
    }
    alert_cooldown_until_ms_ = now_ms + ALERT_COOLDOWN_MS;
    xSemaphoreGive(state_mutex_);

    uint32_t total_falls = IncrementLocalFallCount();

    auto& app = Application::GetInstance();
    if (app.GetDeviceState() == kDeviceStateSpeaking || app.GetDeviceState() == kDeviceStateListening) {
        app.AbortSpeaking(kAbortReasonWakeWordDetected);
    }

    ESP_LOGW(TAG, "🚨 CRITICAL FALL DETECTED (track %d, %s)", track.id, reason);

    auto* board = static_cast<Board*>(&Board::GetInstance());
    if (board) {
        board->SendEyeCommand(0x04); // 0x04 = WARNING / FALL DETECTED
    }

    xTaskCreate(AlarmSoundTask, "FallAlarmSoundTask", 3072, &Application::GetInstance(), 5, NULL);

    auto* display = Board::GetInstance().GetDisplay();
    if (display) display->SetChatMessage("system", "🚨 EMERGENCY: Fall detected!");

    float ground_duty = track.window_frames > 0
        ? static_cast<float>(track.ground_frames) / track.window_frames
        : 0.0f;

    char evidence[192];
    snprintf(evidence, sizeof(evidence),
             "\"track_id\":%d,\"reason\":\"%s\",\"peak_norm_vel\":%.2f,\"drop_n\":%.2f,\"ground_duty\":%.2f",
             track.id, reason, track.peak_norm_vel, track.last_features.drop_n, ground_duty);

    std::string mcp_payload = "{"
        "\"jsonrpc\":\"2.0\","
        "\"method\":\"notifications/event\","
        "\"params\":{"
            "\"event\":\"fall_detection\","
            "\"status\":\"panic\","
            "\"message\":\"Critical fall detected\","
            "\"total_falls\":" + std::to_string(total_falls) + "," +
            evidence +
        "}"
    "}";

    app.SendMcpMessage(mcp_payload);

    // Deliberately returns immediately. This runs on DetectionTask via ProcessDetectionLine, so
    // blocking here stops UART draining; the cooldown is enforced by alert_cooldown_until_ms_
    // above instead. Every call below this point is already non-blocking: SendMcpMessage only
    // Schedule()s, AlarmSoundTask is its own task, and AudioService::PlaySound queues.
}

void FallDetectionController::OnFallAlert(void* ctx, uint32_t now_ms, const TrackedPerson& track,
                                           const char* reason) {
    static_cast<FallDetectionController*>(ctx)->TriggerFallAlert(now_ms, track, reason);
}

void FallDetectionController::OnEventLog(void* /*ctx*/, uint32_t now_ms, const TrackedPerson& track,
                                          PostureState from, PostureState to) {
#if FD_CAPTURE_MODE
    ESP_LOGI(TAG, "FDEVT,%d,%s,%s,%lu,vy=%.2f,vh=%.2f,drop=%.2f,r=%.2f",
             track.id, PostureStateName(from), PostureStateName(to), (unsigned long)now_ms,
             track.last_features.v_cy_n, track.last_features.v_h_n, track.last_features.drop_n,
             track.last_features.r);
#endif
}

void FallDetectionController::OnSuppressLog(void* /*ctx*/, uint32_t now_ms, const TrackedPerson& track,
                                             const char* reason) {
#if FD_CAPTURE_MODE
    ESP_LOGW(TAG, "FDEVT,%d,SUPPRESS,%s,%lu", track.id, reason, (unsigned long)now_ms);
#endif
}

void FallDetectionController::ProcessDetectionLine(const char* line, uint32_t now_ms) {
    const char* ptr = strstr(line, "\"boxes\"");
    if (!ptr) return;

    ptr = strchr(ptr, '[');
    if (!ptr) return;
    ptr++; // Skip outer array bracket

    std::vector<BoxObservation> boxes;

    while ((ptr = strchr(ptr, '[')) != nullptr) {
        int x = 0, y = 0, w = 0, h = 0, score = 0, target = 0;

        if (sscanf(ptr, "[%d,%d,%d,%d,%d,%d]", &x, &y, &w, &h, &score, &target) == 6 ||
            sscanf(ptr, "[%d, %d, %d, %d, %d, %d]", &x, &y, &w, &h, &score, &target) == 6) {

            if (target == TARGET_PERSON_ID && w > 0 && h > 0 && score >= MIN_BOX_SCORE) {
                boxes.push_back(BoxObservation{
                    static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(h), score});
            }
        }

        const char* close_bracket = strchr(ptr, ']');
        if (close_bracket) {
            ptr = close_bracket + 1;
        } else {
            ptr++;
        }
    }

    posture_tracker_.Update(boxes.data(), boxes.size(), now_ms);
}

void FallDetectionController::DetectionTask(void* pvParameters) {
    auto* self = static_cast<FallDetectionController*>(pvParameters);
    uint8_t data[UART_BUF_SIZE];

    char line_buffer[UART_BUF_SIZE];
    int line_pos = 0;

    TickType_t last_poll_time = xTaskGetTickCount();
    TickType_t last_heartbeat = xTaskGetTickCount();
#if FD_CAPTURE_MODE
    uint32_t fd_seq = 0;
    bool line_overflow = false; // True signals potential data loss in FDLOG entries
#endif

    for (;;) {
        // ESP32-S3 sending request to Grove Vision Module every 200ms
        if ((xTaskGetTickCount() - last_poll_time) > pdMS_TO_TICKS(200)) {
            const char* poll_cmd = "AT+INVOKE=1,0,1\r";
            uart_write_bytes(self->uart_num_, poll_cmd, strlen(poll_cmd));
            last_poll_time = xTaskGetTickCount();
#if FD_CAPTURE_MODE
            ESP_LOGI(TAG, "FDPOLL,%lu", (unsigned long)(last_poll_time * portTICK_PERIOD_MS));
#endif
        }

        int len = uart_read_bytes(self->uart_num_, data, UART_BUF_SIZE - 1, pdMS_TO_TICKS(50));

        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = data[i];
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';

                        // Sampled once and passed down, so a replayed FDLOG reproduces the exact
                        // timebase the line was processed with.
                        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
#if FD_CAPTURE_MODE
                        ESP_LOGI(TAG, "FDLOG,%lu,%lu,%d,%s",
                                 (unsigned long)fd_seq++,
                                 (unsigned long)now_ms,
                                 line_overflow ? 1 : 0,
                                 line_buffer);
                        line_overflow = false;
#endif
                        self->ProcessDetectionLine(line_buffer, now_ms);
                        line_pos = 0;
                    }
                } else if (line_pos < (sizeof(line_buffer) - 1)) {
                    line_buffer[line_pos++] = c;
#if FD_CAPTURE_MODE
                } else {
                    line_overflow = true;
#endif
                }
            }
        }

        if ((xTaskGetTickCount() - last_heartbeat) >= pdMS_TO_TICKS(1000)) {
#if FD_CAPTURE_MODE
            // Bytes of stack never touched: tune kDetectionTaskStackSize from this.
            // Peak for UART_BUF_SIZE (1024) is ~3.6 KB = 2.1 KB (DetectionTask) + 1.5 KB (sscanf / ESP_LOGI)
            ESP_LOGI(TAG, "FDSTACK_FREE,%u", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
#endif
            last_heartbeat = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

FallDetectionController::FallDetectionController(uart_port_t uart_bus, int tx, int rx)
    : uart_num_(uart_bus) {
    state_mutex_ = xSemaphoreCreateMutex();

    posture_tracker_.SetFallAlertCallback(&FallDetectionController::OnFallAlert, this);
#if FD_CAPTURE_MODE
    posture_tracker_.SetEventLogCallback(&FallDetectionController::OnEventLog, this);
    posture_tracker_.SetSuppressLogCallback(&FallDetectionController::OnSuppressLog, this);
#endif

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

    xTaskCreatePinnedToCore(DetectionTask, "FallDetTask", kDetectionTaskStackSize, this, 1, nullptr, 1);
    ESP_LOGI(TAG, "Fall Detection Initialized via UART (YOLO Array Mode).");
}

FallDetectionController::~FallDetectionController() {
    if (state_mutex_) vSemaphoreDelete(state_mutex_);
}
