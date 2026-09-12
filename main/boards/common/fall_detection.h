#pragma once

#include <cstdint>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "fall_posture.h"

// Alert when a person is detected to have fallen.
// Example:
//  static FallDetectionController fall_detector(UART_NUM_1, 11, 12);
//  fall_detector_ = &fall_detector;
class FallDetectionController {
public:
    FallDetectionController(uart_port_t uart_bus, int tx, int rx);
    ~FallDetectionController();

    uint32_t GetLocalFallCount();

private:
    // UART, parsing and alert fan-out live here; classification is delegated to PostureTracker
    // (fall_posture.h/.cc), which has no ESP-IDF/FreeRTOS/NVS includes so it also compiles on the
    // host for tools/fall_replay/.
    PostureTracker posture_tracker_;

    uart_port_t uart_num_;
    // Non-blocking alert re-arm point, in the same ms timebase as ProcessDetectionLine's now_ms.
    // Global cooldown on top of PostureTracker's per-track `alerted` latch.
    uint32_t alert_cooldown_until_ms_ = 0;
    SemaphoreHandle_t state_mutex_ = nullptr;

    static void AlarmSoundTask(void* pvParameters);
    uint32_t IncrementLocalFallCount();
    void TriggerFallAlert(uint32_t now_ms, const TrackedPerson& track, const char* reason);
    void ProcessDetectionLine(const char* line, uint32_t now_ms);
    static void DetectionTask(void* pvParameters);

    // PostureTracker callback trampolines (plain function pointers, no std::function).
    static void OnFallAlert(void* ctx, uint32_t now_ms, const TrackedPerson& track, const char* reason);
    static void OnEventLog(void* ctx, uint32_t now_ms, const TrackedPerson& track, PostureState from,
                           PostureState to);
    static void OnSuppressLog(void* ctx, uint32_t now_ms, const TrackedPerson& track, const char* reason);
};
