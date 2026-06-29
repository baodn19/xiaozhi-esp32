#ifndef __MOVEMENTS_H__
#define __MOVEMENTS_H__

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oscillator.h"

//-- Constants
#define FORWARD 1
#define BACKWARD -1
#define LEFT 1
#define RIGHT -1
#define BOTH 0
#define SMALL 5
#define MEDIUM 15
#define BIG 30

// -- Servo delta limit default. degree / sec
#define SERVO_LIMIT_DEFAULT 240

// -- Servo indexes for easy access
#define RIGHT_PITCH 0
#define RIGHT_ROLL 1
#define LEFT_PITCH 2
#define LEFT_ROLL 3
#define BODY 4
#define HEAD 5
#define SERVO_COUNT 6

class Otto {
public:
    Otto();
    ~Otto();

    //-- Otto initialization
    void Init(int right_pitch, int right_roll, int left_pitch, int left_roll, int body, int head);
    //-- Attach & detach functions
    void AttachServos();
    void DetachServos();

    //-- Oscillator Trims
    void SetTrims(int right_pitch, int right_roll, int left_pitch, int left_roll, int body,
                  int head);

    //-- Predetermined Motion Functions
    void MoveServos(int time, int servo_target[]);
    void MoveSingle(int position, int servo_number);
    void OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                         double phase_diff[SERVO_COUNT], float cycle);

    //-- HOME = Otto at rest position
    void Home(bool hands_down = true);
    bool GetRestState();
    void SetRestState(bool state);

    // -- Hand actions
    void HandAction(int action, int times = 1, int amount = 30, int period = 1000);
    // action: 1=raise left, 2=raise right, 3=raise both, 4=lower left, 5=lower right, 6=lower both, 7=wave left, 8=wave right,
    // 9=wave both, 10=clap left, 11=clap right, 12=clap both

    //-- Body actions
    void BodyAction(int action, int times = 1, int amount = 30, int period = 1000);
    // action: 1=turn left, 2=turn right

    //-- Head actions
    void HeadAction(int action, int times = 1, int amount = 10, int period = 500);
    // action: 1=look up, 2=look down, 3=nod once, 4=center, 5=repeat nod

private:
    Oscillator servo_[SERVO_COUNT];

    int servo_pins_[SERVO_COUNT];
    int servo_trim_[SERVO_COUNT];
    int servo_initial_[SERVO_COUNT] = {180, 180, 0, 0, 90, 90};

    unsigned long final_time_;
    unsigned long partial_time_;
    float increment_[SERVO_COUNT];

    bool is_otto_resting_;

    void Execute(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                 double phase_diff[SERVO_COUNT], float steps);
};

#endif  // __MOVEMENTS_H__