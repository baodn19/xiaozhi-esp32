#include "movements.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "oscillator.h"

namespace {
float EaseOutCubic(float t) {
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}
}  // namespace

Otto::Otto() {
    is_otto_resting_ = false;
    for (int i = 0; i < SERVO_COUNT; i++) {
        servo_pins_[i] = -1;
        servo_trim_[i] = 0;
    }
}

Otto::~Otto() {
    DetachServos();
}

unsigned long IRAM_ATTR millis() {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

void Otto::Init(int right_pitch, int right_roll, int left_pitch, int left_roll, int body,
                int head) {
    servo_pins_[RIGHT_PITCH] = right_pitch;
    servo_pins_[RIGHT_ROLL] = right_roll;
    servo_pins_[LEFT_PITCH] = left_pitch;
    servo_pins_[LEFT_ROLL] = left_roll;
    servo_pins_[BODY] = body;
    servo_pins_[HEAD] = head;

    AttachServos();
    is_otto_resting_ = false;
}

///////////////////////////////////////////////////////////////////
//-- ATTACH & DETACH FUNCTIONS ----------------------------------//
///////////////////////////////////////////////////////////////////
void Otto::AttachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].Attach(servo_pins_[i]);
        }
    }
}

void Otto::DetachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].Detach();
        }
    }
}

///////////////////////////////////////////////////////////////////
//-- OSCILLATORS TRIMS ------------------------------------------//
///////////////////////////////////////////////////////////////////
void Otto::SetTrims(int right_pitch, int right_roll, int left_pitch, int left_roll, int body,
                    int head) {
    servo_trim_[RIGHT_PITCH] = right_pitch;
    servo_trim_[RIGHT_ROLL] = right_roll;
    servo_trim_[LEFT_PITCH] = left_pitch;
    servo_trim_[LEFT_ROLL] = left_roll;
    servo_trim_[BODY] = body;
    servo_trim_[HEAD] = head;

    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetTrim(servo_trim_[i]);
        }
    }
}

///////////////////////////////////////////////////////////////////
//-- BASIC MOTION FUNCTIONS -------------------------------------//
///////////////////////////////////////////////////////////////////
void Otto::MoveServos(int time, int servo_target[]) {
    if (GetRestState() == true) {
        SetRestState(false);
    }

    if (time <= 10) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                servo_[i].SetPosition(servo_target[i]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(time));
        return;
    }

    int start[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++) {
        start[i] = servo_[i].GetPosition();
    }

    int steps = std::max(1, time / 10);
    for (int step = 1; step <= steps; step++) {
        float t = static_cast<float>(step) / static_cast<float>(steps);
        float eased_t = EaseOutCubic(t);
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                float interpolated = start[i] + (servo_target[i] - start[i]) * eased_t;
                servo_[i].SetPosition(static_cast<int>(std::round(interpolated)));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetPosition(servo_target[i]);
        }
    }
}

void Otto::MoveSingle(int position, int servo_number) {
    if (position > 180)
        position = 90;
    if (position < 0)
        position = 90;

    if (GetRestState() == true) {
        SetRestState(false);
    }

    if (servo_number >= 0 && servo_number < SERVO_COUNT && servo_pins_[servo_number] != -1) {
        servo_[servo_number].SetPosition(position);
    }
}

void Otto::OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                           double phase_diff[SERVO_COUNT], float cycle = 1) {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetO(offset[i]);
            servo_[i].SetA(amplitude[i]);
            servo_[i].SetT(period);
            servo_[i].SetPh(phase_diff[i]);
        }
    }

    double ref = millis();
    double end_time = period * cycle + ref;

    while (millis() < end_time) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                servo_[i].Refresh();
            }
        }
        vTaskDelay(5);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}

void Otto::Execute(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                   double phase_diff[SERVO_COUNT], float steps = 1.0) {
    if (GetRestState() == true) {
        SetRestState(false);
    }

    int cycles = (int)steps;

    //-- Execute complete cycles
    if (cycles >= 1)
        for (int i = 0; i < cycles; i++)
            OscillateServos(amplitude, offset, period, phase_diff);

    //-- Execute the final not complete cycle
    OscillateServos(amplitude, offset, period, phase_diff, (float)steps - cycles);
    vTaskDelay(pdMS_TO_TICKS(10));
}

///////////////////////////////////////////////////////////////////
//-- HOME = Otto at rest position -------------------------------//
///////////////////////////////////////////////////////////////////
void Otto::Home(bool hands_down) {
    if (is_otto_resting_ == false) {  // Go to rest position only if necessary
        int max_delta = 0;
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                max_delta = std::max(max_delta, std::abs(servo_initial_[i] - servo_[i].GetPosition()));
            }
        }
        int home_time = std::clamp(500 + max_delta * 9, 500, 1700);
        MoveServos(home_time, servo_initial_);
        is_otto_resting_ = true;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
}

bool Otto::GetRestState() {
    return is_otto_resting_;
}

void Otto::SetRestState(bool state) {
    is_otto_resting_ = state;
}

///////////////////////////////////////////////////////////////////
//-- PREDETERMINED MOTION SEQUENCES -----------------------------//
///////////////////////////////////////////////////////////////////

//---------------------------------------------------------
//-- Unified hand action function
//--  Parameters:
//--    action: 1=raise left, 2=raise right, 3=raise both, 4=lower left, 5=lower right, 6=lower both,
//--            7=wave left, 8=wave right, 9=wave both, 10=clap left, 11=clap right, 12=clap both
//--    times: repeat count
//--    amount: action amount (10-50)
//--    period: action duration
//---------------------------------------------------------
void Otto::HandAction(int action, int times, int amount, int period) {
    // Clamp parameters
    times = 2 * std::max(3, std::min(100, times));
    amount = std::max(10, std::min(50, amount));
    period = std::max(100, std::min(1000, period));

    int current_positions[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++) {
        current_positions[i] = (servo_pins_[i] != -1) ? servo_[i].GetPosition() : servo_initial_[i];
    }

    switch (action) {
        case 1:  // Raise left hand
            current_positions[LEFT_PITCH] = 180;
            MoveServos(period, current_positions);
            break;

        case 2:  // Raise right hand
            current_positions[RIGHT_PITCH] = 0;
            MoveServos(period, current_positions);
            break;

        case 3:  // Raise both hands
            current_positions[LEFT_PITCH] = 180;
            current_positions[RIGHT_PITCH] = 0;
            MoveServos(period, current_positions);
            break;

        case 4:  // Lower left hand
        case 5:  // Lower right hand
        case 6:  // Lower both hands
            // Return to initial position
            memcpy(current_positions, servo_initial_, sizeof(current_positions));
            MoveServos(period, current_positions);
            break;

        case 7:  // Wave left hand
            current_positions[LEFT_PITCH] = 150;
            MoveServos(period, current_positions);
            for (int i = 0; i < times; i++) {
                current_positions[LEFT_PITCH] = 150 + (i % 2 == 0 ? -30 : 30);
                MoveServos(period / 10, current_positions);
                vTaskDelay(pdMS_TO_TICKS(period / 10));
            }
            memcpy(current_positions, servo_initial_, sizeof(current_positions));
            MoveServos(period, current_positions);
            break;

        case 8:  // Wave right hand
            current_positions[RIGHT_PITCH] = 30;
            MoveServos(period, current_positions);
            for (int i = 0; i < times; i++) {
                current_positions[RIGHT_PITCH] = 30 + (i % 2 == 0 ? 30 : -30);
                MoveServos(period / 10, current_positions);
                vTaskDelay(pdMS_TO_TICKS(period / 10));
            }
            memcpy(current_positions, servo_initial_, sizeof(current_positions));
            MoveServos(period, current_positions);
            break;

        case 9:  // Wave both hands
            current_positions[LEFT_PITCH] = 150;
            current_positions[RIGHT_PITCH] = 30;
            MoveServos(period, current_positions);
            for (int i = 0; i < times; i++) {
                current_positions[LEFT_PITCH] = 150 + (i % 2 == 0 ? -30 : 30);
                current_positions[RIGHT_PITCH] = 30 + (i % 2 == 0 ? 30 : -30);
                MoveServos(period / 10, current_positions);
                vTaskDelay(pdMS_TO_TICKS(period / 10));
            }
            memcpy(current_positions, servo_initial_, sizeof(current_positions));
            MoveServos(period, current_positions);
            break;

        case 10:  // Clap left hand
            current_positions[LEFT_ROLL] = 20;
            MoveServos(period, current_positions);
            for (int i = 0; i < times; i++) {
                current_positions[LEFT_ROLL] = 20 - amount;
                MoveServos(period / 10, current_positions);
                current_positions[LEFT_ROLL] = 20 + amount;
                MoveServos(period / 10, current_positions);
            }
            current_positions[LEFT_ROLL] = 0;
            MoveServos(period, current_positions);
            break;

        case 11:  // Clap right hand
            current_positions[RIGHT_ROLL] = 160;
            MoveServos(period, current_positions);
            for (int i = 0; i < times; i++) {
                current_positions[RIGHT_ROLL] = 160 + amount;
                MoveServos(period / 10, current_positions);
                current_positions[RIGHT_ROLL] = 160 - amount;
                MoveServos(period / 10, current_positions);
            }
            current_positions[RIGHT_ROLL] = 180;
            MoveServos(period, current_positions);
            break;

        case 12:  // Clap both hands
            current_positions[LEFT_ROLL] = 20;
            current_positions[RIGHT_ROLL] = 160;
            MoveServos(period, current_positions);
            for (int i = 0; i < times; i++) {
                current_positions[LEFT_ROLL] = 20 - amount;
                current_positions[RIGHT_ROLL] = 160 + amount;
                MoveServos(period / 10, current_positions);
                current_positions[LEFT_ROLL] = 20 + amount;
                current_positions[RIGHT_ROLL] = 160 - amount;
                MoveServos(period / 10, current_positions);
            }
            current_positions[LEFT_ROLL] = 0;
            current_positions[RIGHT_ROLL] = 180;
            MoveServos(period, current_positions);
            break;
    }
}

//---------------------------------------------------------
//-- Unified body action function
//--  Parameters:
//--    action: 1=turn left, 2=turn right, 3=return to center
//--    times: turn count
//--    amount: rotation angle (0-90 deg, centered at 90 deg)
//--    period: action duration
//---------------------------------------------------------
void Otto::BodyAction(int action, int times, int amount, int period) {
    // Clamp parameters
    times = std::max(1, std::min(10, times));
    amount = std::max(0, std::min(90, amount));
    period = std::max(500, std::min(3000, period));

    int current_positions[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            current_positions[i] = servo_[i].GetPosition();
        } else {
            current_positions[i] = servo_initial_[i];
        }
    }

    int body_center = servo_initial_[BODY];
    int target_angle = body_center;

    switch (action) {
        case 1:  // Turn left
            target_angle = body_center + amount;
            target_angle = std::min(180, target_angle);
            break;
        case 2:  // Turn right
            target_angle = body_center - amount;
            target_angle = std::max(0, target_angle);
            break;
        case 3:  // Return to center
            target_angle = body_center;
            break;
        default:
            return;  // Invalid action
    }

    current_positions[BODY] = target_angle;
    MoveServos(period, current_positions);
    vTaskDelay(pdMS_TO_TICKS(100));
}

//---------------------------------------------------------
//-- Unified head action function
//--  Parameters:
//--    action: 1=look up, 2=look down, 3=nod once, 4=return to center, 5=repeat nod
//--    times: repeat count (only for repeat nod)
//--    amount: angle offset (1-15 deg)
//--    period: action duration
//---------------------------------------------------------
void Otto::HeadAction(int action, int times, int amount, int period) {
    // Clamp parameters
    times = std::max(1, std::min(10, times));
    amount = std::max(1, std::min(15, abs(amount)));
    period = std::max(300, std::min(3000, period));

    int current_positions[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            current_positions[i] = servo_[i].GetPosition();
        } else {
            current_positions[i] = servo_initial_[i];
        }
    }

    int head_center = 90;  // Head center position

    switch (action) {
        case 1:                                              // Look up
            current_positions[HEAD] = head_center + amount;  // Look up increases angle
            MoveServos(period, current_positions);
            break;

        case 2:                                              // Look down
            current_positions[HEAD] = head_center - amount;  // Look down decreases angle
            MoveServos(period, current_positions);
            break;

        case 3:  // Nod once (up/down motion)
            // Look up first
            current_positions[HEAD] = head_center + amount;
            MoveServos(period / 3, current_positions);
            vTaskDelay(pdMS_TO_TICKS(period / 6));

            // Look down next
            current_positions[HEAD] = head_center - amount;
            MoveServos(period / 3, current_positions);
            vTaskDelay(pdMS_TO_TICKS(period / 6));

            // Return to center
            current_positions[HEAD] = head_center;
            MoveServos(period / 3, current_positions);
            break;

        case 4:  // Return to center position
            current_positions[HEAD] = head_center;
            MoveServos(period, current_positions);
            break;

        case 5:  // Repeat nod
            for (int i = 0; i < times; i++) {
                // Look up
                current_positions[HEAD] = head_center + amount;
                MoveServos(period / 2, current_positions);

                // Look down
                current_positions[HEAD] = head_center - amount;
                MoveServos(period / 2, current_positions);

                vTaskDelay(pdMS_TO_TICKS(50));  // Brief pause
            }

            // Return to center
            current_positions[HEAD] = head_center;
            MoveServos(period / 2, current_positions);
            break;

        default:
            // Invalid action; return to center
            current_positions[HEAD] = head_center;
            MoveServos(period, current_positions);
            break;
    }
}
