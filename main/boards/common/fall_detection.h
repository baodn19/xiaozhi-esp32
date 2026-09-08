#pragma once

#include <cstdint>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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
    static constexpr int kMaxTrackedPeople = 5;

    struct TrackedPerson {
        int id;
        int x;
        int y;
        int w;
        int h;
        int last_y;            // Tracks previous frame's Y position
        float last_ratio;      // Tracks previous frame's aspect ratio
        uint32_t last_time;    // Tracks exact tick timestamp
        int frames_until_untracked;
        bool active;
        float ratio;
    };

    TrackedPerson tracked_people_[kMaxTrackedPeople] = {};
    int next_track_id_ = 1; // 0 is for untracked, start IDs from 1

    uart_port_t uart_num_;
    bool alert_active_ = false;
    SemaphoreHandle_t state_mutex_ = nullptr;

    static void AlarmSoundTask(void* pvParameters);
    uint32_t IncrementLocalFallCount();
    void TriggerFallAlert();
    float GetBoxDistance(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2);
    void ProcessDetectionLine(const char* line);
    static void DetectionTask(void* pvParameters);
};
