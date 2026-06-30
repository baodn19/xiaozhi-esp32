/*
    Otto robot controller - MCP protocol version
*/

#include <cJSON.h>
#include <esp_log.h>

#include <cstdlib> 
#include <cstring>

#include "application.h"
#include "board.h"
#include "config.h"
#include "mcp_server.h"
#include "otto_movements.h"
#include "power_manager.h"
#include "sdkconfig.h"
#include "settings.h"
#include <wifi_manager.h>

#define TAG "OttoController"

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    QueueHandle_t action_queue_;
    bool has_hands_ = false;
    bool is_action_in_progress_ = false;

    struct OttoActionParams {
        int action_type;
        int steps;
        int speed;
        int direction;
        int amount;
        char servo_sequence_json[512];  // JSON string buffer for servo sequence
    };

    enum ActionType {
        ACTION_WALK = 1,
        ACTION_TURN = 2,
        ACTION_JUMP = 3,
        ACTION_SWING = 4,
        ACTION_MOONWALK = 5,
        ACTION_BEND = 6,
        ACTION_SHAKE_LEG = 7,
        ACTION_SIT = 25,  // Sit down
        ACTION_RADIO_CALISTHENICS = 26,  // Radio calisthenics
        ACTION_MAGIC_CIRCLE = 27,  // Magic circle spin
        ACTION_UPDOWN = 8,
        ACTION_TIPTOE_SWING = 9,
        ACTION_JITTER = 10,
        ACTION_ASCENDING_TURN = 11,
        ACTION_CRUSAITO = 12,
        ACTION_FLAPPING = 13,
        ACTION_HANDS_UP = 14,
        ACTION_HANDS_DOWN = 15,
        ACTION_HAND_WAVE = 16,
        ACTION_WINDMILL = 20,  // Windmill
        ACTION_TAKEOFF = 21,   // Takeoff
        ACTION_FITNESS = 22,   // Fitness
        ACTION_GREETING = 23,  // Greeting wave
        ACTION_SHY = 24,        // Shy gesture
        ACTION_SHOWCASE = 28,   // Showcase routine
        ACTION_HOME = 17,
        ACTION_SERVO_SEQUENCE = 18,  // Servo sequence (custom programming)
        ACTION_WHIRLWIND_LEG = 19    // Whirlwind leg
    };

    static void ActionTask(void* arg) {
        OttoController* controller = static_cast<OttoController*>(arg);
        OttoActionParams params;
        controller->otto_.AttachServos();

        while (true) {
            if (xQueueReceive(controller->action_queue_, &params, pdMS_TO_TICKS(1000)) == pdTRUE) {
                ESP_LOGI(TAG, "Executing action: %d", params.action_type);
                PowerManager::PauseBatteryUpdate();  // Pause battery updates when action starts
                controller->is_action_in_progress_ = true;
                if (params.action_type == ACTION_SERVO_SEQUENCE) {
                    // Execute servo sequence (custom) - short key names only
                    cJSON* json = cJSON_Parse(params.servo_sequence_json);
                    if (json != nullptr) {
                        ESP_LOGD(TAG, "JSON parsed OK, length=%d", strlen(params.servo_sequence_json));
                        // Short key "a" denotes the action array
                        cJSON* actions = cJSON_GetObjectItem(json, "a");
                        if (cJSON_IsArray(actions)) {
                            int array_size = cJSON_GetArraySize(actions);
                            ESP_LOGI(TAG, "Executing servo sequence, %d actions", array_size);
                            
                            // Post-sequence delay (short key "d", top-level)
                            int sequence_delay = 0;
                            cJSON* delay_item = cJSON_GetObjectItem(json, "d");
                            if (cJSON_IsNumber(delay_item)) {
                                sequence_delay = delay_item->valueint;
                                if (sequence_delay < 0) sequence_delay = 0;
                            }
                            
                            // Initialize current servo positions (preserve unspecified servos)
                            int current_positions[SERVO_COUNT];
                            for (int j = 0; j < SERVO_COUNT; j++) {
                                current_positions[j] = 90;  // Default center position
                            }
                            // Default hand servo positions
                            current_positions[LEFT_HAND] = 45;
                            current_positions[RIGHT_HAND] = 180 - 45;
                            
                            for (int i = 0; i < array_size; i++) {
                                cJSON* action_item = cJSON_GetArrayItem(actions, i);
                                if (cJSON_IsObject(action_item)) {
                                    // Check oscillator mode (short key "osc")
                                    cJSON* osc_item = cJSON_GetObjectItem(action_item, "osc");
                                    if (cJSON_IsObject(osc_item)) {
                                        // Oscillator mode - Execute2 with absolute angle as center
                                        int amplitude[SERVO_COUNT] = {0};
                                        int center_angle[SERVO_COUNT] = {0};
                                        double phase_diff[SERVO_COUNT] = {0};
                                        int period = 300;  // Default period 300 ms
                                        float steps = 8.0;  // Default steps 8.0
                                        
                                        const char* servo_names[] = {"ll", "rl", "lf", "rf", "lh", "rh"};
                                        
                                        // Read amplitude (short key "a"), default 0 degrees
                                        for (int j = 0; j < SERVO_COUNT; j++) {
                                            amplitude[j] = 0;  // Default amplitude 0 degrees
                                        }
                                        cJSON* amp_item = cJSON_GetObjectItem(osc_item, "a");
                                        if (cJSON_IsObject(amp_item)) {
                                            for (int j = 0; j < SERVO_COUNT; j++) {
                                                cJSON* amp_value = cJSON_GetObjectItem(amp_item, servo_names[j]);
                                                if (cJSON_IsNumber(amp_value)) {
                                                    int amp = amp_value->valueint;
                                                    if (amp >= 10 && amp <= 90) {
                                                        amplitude[j] = amp;
                                                    }
                                                }
                                            }
                                        }
                                        
                                        // Read center angle (short key "o"), default 90 (absolute 0-180)
                                        for (int j = 0; j < SERVO_COUNT; j++) {
                                            center_angle[j] = 90;  // Default center angle 90 (neutral)
                                        }
                                        cJSON* center_item = cJSON_GetObjectItem(osc_item, "o");
                                        if (cJSON_IsObject(center_item)) {
                                            for (int j = 0; j < SERVO_COUNT; j++) {
                                                cJSON* center_value = cJSON_GetObjectItem(center_item, servo_names[j]);
                                                if (cJSON_IsNumber(center_value)) {
                                                    int center = center_value->valueint;
                                                    if (center >= 0 && center <= 180) {
                                                        center_angle[j] = center;
                                                    }
                                                }
                                            }
                                        }
                                        
                                        // Safety check: prevent large simultaneous leg/foot oscillation
                                        const int LARGE_AMPLITUDE_THRESHOLD = 40;  // Large amplitude threshold: 40 degrees
                                        bool left_leg_large = amplitude[LEFT_LEG] >= LARGE_AMPLITUDE_THRESHOLD;
                                        bool right_leg_large = amplitude[RIGHT_LEG] >= LARGE_AMPLITUDE_THRESHOLD;
                                        bool left_foot_large = amplitude[LEFT_FOOT] >= LARGE_AMPLITUDE_THRESHOLD;
                                        bool right_foot_large = amplitude[RIGHT_FOOT] >= LARGE_AMPLITUDE_THRESHOLD;
                                        
                                        if (left_leg_large && right_leg_large) {
                                            ESP_LOGW(TAG, "Large-amplitude oscillation on both legs detected; limiting right leg amplitude");
                                            amplitude[RIGHT_LEG] = 0;  // Disable right leg oscillation
                                        }
                                        if (left_foot_large && right_foot_large) {
                                            ESP_LOGW(TAG, "Large-amplitude oscillation on both feet detected; limiting right foot amplitude");
                                            amplitude[RIGHT_FOOT] = 0;  // Disable right foot oscillation
                                        }
                                        
                                        // Read phase difference (short key "ph", degrees, converted to radians)
                                        cJSON* phase_item = cJSON_GetObjectItem(osc_item, "ph");
                                        if (cJSON_IsObject(phase_item)) {
                                            for (int j = 0; j < SERVO_COUNT; j++) {
                                                cJSON* phase_value = cJSON_GetObjectItem(phase_item, servo_names[j]);
                                                if (cJSON_IsNumber(phase_value)) {
                                                    // Convert degrees to radians
                                                    phase_diff[j] = phase_value->valuedouble * 3.141592653589793 / 180.0;
                                                }
                                            }
                                        }
                                        
                                        // Read period (short key "p"), range 100-3000 ms
                                        cJSON* period_item = cJSON_GetObjectItem(osc_item, "p");
                                        if (cJSON_IsNumber(period_item)) {
                                            period = period_item->valueint;
                                            if (period < 100) period = 100;
                                            if (period > 3000) period = 3000;  // Match spec: cap at 3000 ms
                                        }
                                        
                                        // Read cycle count (short key "c"), range 0.1-20.0
                                        cJSON* steps_item = cJSON_GetObjectItem(osc_item, "c");
                                        if (cJSON_IsNumber(steps_item)) {
                                            steps = (float)steps_item->valuedouble;
                                            if (steps < 0.1) steps = 0.1;
                                            if (steps > 20.0) steps = 20.0;  // Match spec: cap at 20.0
                                        }
                                        
                                        // Execute oscillation - Execute2 with absolute angle center
                                        ESP_LOGI(TAG, "Executing oscillation action %d: period=%d, steps=%.1f", i, period, steps);
                                        controller->otto_.Execute2(amplitude, center_angle, period, phase_diff, steps);
                                        
                                        // Update positions after oscillation (center_angle as final)
                                        for (int j = 0; j < SERVO_COUNT; j++) {
                                            current_positions[j] = center_angle[j];
                                        }
                                    } else {
                                        // Normal move mode
                                        // Copy from current positions; keep unspecified servos
                                        int servo_target[SERVO_COUNT];
                                        for (int j = 0; j < SERVO_COUNT; j++) {
                                            servo_target[j] = current_positions[j];
                                        }
                                        
                                        // Read servo positions from JSON (short key "s")
                                        cJSON* servos_item = cJSON_GetObjectItem(action_item, "s");
                                        if (cJSON_IsObject(servos_item)) {
                                            // Short keys: ll/rl/lf/rf/lh/rh
                                            const char* servo_names[] = {"ll", "rl", "lf", "rf", "lh", "rh"};
                                            
                                            for (int j = 0; j < SERVO_COUNT; j++) {
                                                cJSON* servo_value = cJSON_GetObjectItem(servos_item, servo_names[j]);
                                                if (cJSON_IsNumber(servo_value)) {
                                                    int position = servo_value->valueint;
                                                    // Clamp position to 0-180 degrees
                                                    if (position >= 0 && position <= 180) {
                                                        servo_target[j] = position;
                                                    }
                                                }
                                            }
                                        }
                                                                                                                    
                                        // Read move speed (short key "v", default 1000 ms)
                                        int speed = 1000;
                                        cJSON* speed_item = cJSON_GetObjectItem(action_item, "v");
                                        if (cJSON_IsNumber(speed_item)) {
                                            speed = speed_item->valueint;
                                            if (speed < 100) speed = 100;  // Minimum 100 ms
                                            if (speed > 3000) speed = 3000;  // Maximum 3000 ms
                                        }
                                        
                                        // Execute servo move
                                        ESP_LOGI(TAG, "Executing action %d: ll=%d, rl=%d, lf=%d, rf=%d, v=%d",
                                                 i, servo_target[LEFT_LEG], servo_target[RIGHT_LEG],
                                                 servo_target[LEFT_FOOT], servo_target[RIGHT_FOOT], speed);
                                        controller->otto_.MoveServos(speed, servo_target);
                                        
                                        // Update current positions for next action
                                        for (int j = 0; j < SERVO_COUNT; j++) {
                                            current_positions[j] = servo_target[j];
                                        }
                                    }
                                    
                                    // Post-action delay (short key "d")
                                    int delay_after = 0;
                                    cJSON* delay_item = cJSON_GetObjectItem(action_item, "d");
                                    if (cJSON_IsNumber(delay_item)) {
                                        delay_after = delay_item->valueint;
                                        if (delay_after < 0) delay_after = 0;
                                    }
                                    
                                    // Post-action delay (skip after last action)
                                    if (delay_after > 0 && i < array_size - 1) {
                                        ESP_LOGI(TAG, "Action %d complete, delaying %d ms", i, delay_after);
                                        vTaskDelay(pdMS_TO_TICKS(delay_after));
                                    }
                                }
                            }
                            
                            // Post-sequence delay (pause between sequences)
                            if (sequence_delay > 0) {
                                // Check if more sequences remain in queue
                                UBaseType_t queue_count = uxQueueMessagesWaiting(controller->action_queue_);
                                if (queue_count > 0) {
                                    ESP_LOGI(TAG, "Sequence complete, delaying %d ms before next sequence (%d sequences remaining in queue)", 
                                             sequence_delay, queue_count);
                                    vTaskDelay(pdMS_TO_TICKS(sequence_delay));
                                }
                            }
                            // Free JSON memory
                            cJSON_Delete(json);
                        } else {
                            ESP_LOGE(TAG, "Servo sequence format error: 'a' is not an array");
                            cJSON_Delete(json);
                        }
                    } else {
                        // Get cJSON error info
                        const char* error_ptr = cJSON_GetErrorPtr();
                        int json_len = strlen(params.servo_sequence_json);
                        ESP_LOGE(TAG, "Failed to parse servo sequence JSON, length=%d, error at: %s", json_len, 
                                 error_ptr ? error_ptr : "unknown");
                        ESP_LOGE(TAG, "JSON content: %s", params.servo_sequence_json);
                    }
                } else {
                    // Execute predefined action
                    switch (params.action_type) {
                        case ACTION_WALK:
                            controller->otto_.Walk(params.steps, params.speed, params.direction,
                                                   params.amount);
                            break;
                        case ACTION_TURN:
                            controller->otto_.Turn(params.steps, params.speed, params.direction,
                                                   params.amount);
                            break;
                        case ACTION_JUMP:
                            controller->otto_.Jump(params.steps, params.speed);
                            break;
                        case ACTION_SWING:
                            controller->otto_.Swing(params.steps, params.speed, params.amount);
                            break;
                        case ACTION_MOONWALK:
                            controller->otto_.Moonwalker(params.steps, params.speed, params.amount,
                                                         params.direction);
                            break;
                        case ACTION_BEND:
                            controller->otto_.Bend(params.steps, params.speed, params.direction);
                            break;
                        case ACTION_SHAKE_LEG:
                            controller->otto_.ShakeLeg(params.steps, params.speed, params.direction);
                            break;
                        case ACTION_SIT:
                            controller->otto_.Sit();
                            break;
                        case ACTION_RADIO_CALISTHENICS:
                            if (controller->has_hands_) {
                                controller->otto_.RadioCalisthenics();
                            }
                            break;
                        case ACTION_MAGIC_CIRCLE:
                            if (controller->has_hands_) {
                                controller->otto_.MagicCircle();
                            }
                            break;
                        case ACTION_SHOWCASE:
                            controller->otto_.Showcase();
                            break;
                        case ACTION_UPDOWN:
                            controller->otto_.UpDown(params.steps, params.speed, params.amount);
                            break;
                        case ACTION_TIPTOE_SWING:
                            controller->otto_.TiptoeSwing(params.steps, params.speed, params.amount);
                            break;
                        case ACTION_JITTER:
                            controller->otto_.Jitter(params.steps, params.speed, params.amount);
                            break;
                        case ACTION_ASCENDING_TURN:
                            controller->otto_.AscendingTurn(params.steps, params.speed, params.amount);
                            break;
                        case ACTION_CRUSAITO:
                            controller->otto_.Crusaito(params.steps, params.speed, params.amount,
                                                       params.direction);
                            break;
                        case ACTION_FLAPPING:
                            controller->otto_.Flapping(params.steps, params.speed, params.amount,
                                                       params.direction);
                            break;
                        case ACTION_WHIRLWIND_LEG:
                            controller->otto_.WhirlwindLeg(params.steps, params.speed, params.amount);
                            break;
                        case ACTION_HANDS_UP:
                            if (controller->has_hands_) {
                                controller->otto_.HandsUp(params.speed, params.direction);
                            }
                            break;
                        case ACTION_HANDS_DOWN:
                            if (controller->has_hands_) {
                                controller->otto_.HandsDown(params.speed, params.direction);
                            }
                            break;
                        case ACTION_HAND_WAVE:
                            if (controller->has_hands_) {
                                controller->otto_.HandWave( params.direction);
                            }
                            break;
                        case ACTION_WINDMILL:
                            if (controller->has_hands_) {
                                controller->otto_.Windmill(params.steps, params.speed, params.amount);
                            }
                            break;
                        case ACTION_TAKEOFF:
                            if (controller->has_hands_) {
                                controller->otto_.Takeoff(params.steps, params.speed, params.amount);
                            }
                            break;
                        case ACTION_FITNESS:
                            if (controller->has_hands_) {
                                controller->otto_.Fitness(params.steps, params.speed, params.amount);
                            }
                            break;
                        case ACTION_GREETING:
                            if (controller->has_hands_) {
                                controller->otto_.Greeting(params.direction, params.steps);
                            }
                            break;
                        case ACTION_SHY:
                            if (controller->has_hands_) {
                                controller->otto_.Shy(params.direction, params.steps);
                            }
                            break;
                        case ACTION_HOME:
                            controller->otto_.Home(true);
                            break;
                    }
                    if(params.action_type != ACTION_SIT){
                        if (params.action_type != ACTION_HOME && params.action_type != ACTION_SERVO_SEQUENCE) {
                            UBaseType_t pending_actions =
                                uxQueueMessagesWaiting(controller->action_queue_);
                            // Skip homing if more actions are queued to avoid abrupt stop-then-restart at action end
                            if (pending_actions == 0) {
                                controller->otto_.Home(params.action_type != ACTION_HANDS_UP);
                            }
                        }
                    }
                }
                controller->is_action_in_progress_ = false;
                PowerManager::ResumeBatteryUpdate();  // Resume battery updates when action ends
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }

    void StartActionTaskIfNeeded() {
        if (action_task_handle_ == nullptr) {
            xTaskCreate(ActionTask, "otto_action", 1024 * 3, this, configMAX_PRIORITIES - 1,
                        &action_task_handle_);
        }
    }

    void QueueAction(int action_type, int steps, int speed, int direction, int amount) {
        // Check hand actions
        if ((action_type >= ACTION_HANDS_UP && action_type <= ACTION_HAND_WAVE) || 
            (action_type == ACTION_WINDMILL) || (action_type == ACTION_TAKEOFF) || 
            (action_type == ACTION_FITNESS) || (action_type == ACTION_GREETING) ||
            (action_type == ACTION_SHY) || (action_type == ACTION_RADIO_CALISTHENICS) ||
            (action_type == ACTION_MAGIC_CIRCLE)) {
            if (!has_hands_) {
                ESP_LOGW(TAG, "Attempted hand action but hand servos are not configured");
                return;
            }
        }

        ESP_LOGI(TAG, "Action control: type=%d, steps=%d, speed=%d, direction=%d, amount=%d", action_type, steps,
                 speed, direction, amount);

        OttoActionParams params = {action_type, steps, speed, direction, amount, ""};
        xQueueSend(action_queue_, &params, portMAX_DELAY);
        StartActionTaskIfNeeded();
    }

    void QueueServoSequence(const char* servo_sequence_json) {
        if (servo_sequence_json == nullptr) {
            ESP_LOGE(TAG, "Sequence JSON is null");
            return;
        }
        
        int input_len = strlen(servo_sequence_json);
        const int buffer_size = 512;  // servo_sequence_json buffer size
        ESP_LOGI(TAG, "Queue servo sequence, input length=%d, buffer size=%d", input_len, buffer_size);
        
        if (input_len >= buffer_size) {
            ESP_LOGE(TAG, "JSON string too long! input length=%d, max allowed=%d", input_len, buffer_size - 1);
            return;
        }
        
        if (input_len == 0) {
            ESP_LOGW(TAG, "Sequence JSON is empty string");
            return;
        }
        
        OttoActionParams params = {ACTION_SERVO_SEQUENCE, 0, 0, 0, 0, ""};
        // Copy JSON string into struct (length limited)
        strncpy(params.servo_sequence_json, servo_sequence_json, sizeof(params.servo_sequence_json) - 1);
        params.servo_sequence_json[sizeof(params.servo_sequence_json) - 1] = '\0';
        
        ESP_LOGD(TAG, "Sequence queued: %s", params.servo_sequence_json);
        
        xQueueSend(action_queue_, &params, portMAX_DELAY);
        StartActionTaskIfNeeded();
    }

    void LoadTrimsFromNVS() {
        Settings settings("otto_trims", false);

        int left_leg = settings.GetInt("left_leg", 0);
        int right_leg = settings.GetInt("right_leg", 0);
        int left_foot = settings.GetInt("left_foot", 0);
        int right_foot = settings.GetInt("right_foot", 0);
        int left_hand = settings.GetInt("left_hand", 0);
        int right_hand = settings.GetInt("right_hand", 0);

        ESP_LOGI(TAG, "Loaded trim settings from NVS: left_leg=%d, right_leg=%d, left_foot=%d, right_foot=%d, left_hand=%d, right_hand=%d",
                 left_leg, right_leg, left_foot, right_foot, left_hand, right_hand);

        otto_.SetTrims(left_leg, right_leg, left_foot, right_foot, left_hand, right_hand);
    }

public:
    OttoController(const HardwareConfig& hw_config) {
        otto_.Init(
            hw_config.left_leg_pin, 
            hw_config.right_leg_pin, 
            hw_config.left_foot_pin, 
            hw_config.right_foot_pin, 
            hw_config.left_hand_pin,
            hw_config.right_hand_pin
        );

        has_hands_ = (hw_config.left_hand_pin != GPIO_NUM_NC && hw_config.right_hand_pin != GPIO_NUM_NC);
        ESP_LOGI(TAG, "Otto robot initialized %s hand servos", has_hands_ ? "with" : "without");
        ESP_LOGI(TAG, "Servo pin config: LL=%d, RL=%d, LF=%d, RF=%d, LH=%d, RH=%d",
                 hw_config.left_leg_pin, hw_config.right_leg_pin,
                 hw_config.left_foot_pin, hw_config.right_foot_pin,
                 hw_config.left_hand_pin, hw_config.right_hand_pin);

        LoadTrimsFromNVS();

        action_queue_ = xQueueCreate(10, sizeof(OttoActionParams));

        QueueAction(ACTION_HOME, 1, 1000, 1, 0);  // direction=1 resets hands

        RegisterMcpTools();
    }

    void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();

        ESP_LOGI(TAG, "Registering MCP tools...");

        // Unified action tool (all actions except servo sequences)
        mcp_server.AddTool("self.otto.action",
                           "Execute a robot action. action: action name; provide parameters per action type: direction: 1=forward/turn left, -1=backward/turn right, 0=both sides; "
                           "steps: action steps, 1-100; speed: action speed, 100-3000, lower is faster; amount: action amplitude, 0-170; arm_swing: arm swing amplitude, 0-170; "
                           "Basic actions: walk (needs steps/speed/direction/arm_swing), turn (needs steps/speed/direction/arm_swing), jump (needs steps/speed), "
                           "swing (needs steps/speed/amount), moonwalk (needs steps/speed/direction/amount), bend (needs steps/speed/direction), "
                           "shake_leg (needs steps/speed/direction), updown (needs steps/speed/amount), whirlwind_leg (needs steps/speed/amount); "
                           "Fixed actions: sit, showcase, home; "
                           "Hand actions (require hand servos): hands_up (needs speed/direction), hands_down (needs speed/direction), hand_wave (needs direction), "
                           "windmill (needs steps/speed/amount), takeoff (needs steps/speed/amount), fitness (needs steps/speed/amount), "
                           "greeting (needs direction/steps), shy (needs direction/steps), radio_calisthenics, magic_circle",
                           PropertyList({
                               Property("action", kPropertyTypeString, "sit"),
                               Property("steps", kPropertyTypeInteger, 3, 1, 100),
                               Property("speed", kPropertyTypeInteger, 700, 100, 3000),
                               Property("direction", kPropertyTypeInteger, 1, -1, 1),
                               Property("amount", kPropertyTypeInteger, 30, 0, 170),
                               Property("arm_swing", kPropertyTypeInteger, 50, 0, 170)
                           }),
                           [this](const PropertyList& properties) -> ReturnValue {
                               std::string action = properties["action"].value<std::string>();
                               // All parameters have defaults; access directly
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int direction = properties["direction"].value<int>();
                               int amount = properties["amount"].value<int>();
                               int arm_swing = properties["arm_swing"].value<int>();

                               // Basic movement actions
                               if (action == "walk") {
                                   QueueAction(ACTION_WALK, steps, speed, direction, arm_swing);
                                   return true;
                               } else if (action == "turn") {
                                   QueueAction(ACTION_TURN, steps, speed, direction, arm_swing);
                                   return true;
                               } else if (action == "jump") {
                                   QueueAction(ACTION_JUMP, steps, speed, 0, 0);
                                   return true;
                               } else if (action == "swing") {
                                   QueueAction(ACTION_SWING, steps, speed, 0, amount);
                                   return true;
                               } else if (action == "moonwalk") {
                                   QueueAction(ACTION_MOONWALK, steps, speed, direction, amount);
                                   return true;
                               } else if (action == "bend") {
                                   QueueAction(ACTION_BEND, steps, speed, direction, 0);
                                   return true;
                               } else if (action == "shake_leg") {
                                   QueueAction(ACTION_SHAKE_LEG, steps, speed, direction, 0);
                                   return true;
                               } else if (action == "updown") {
                                   QueueAction(ACTION_UPDOWN, steps, speed, 0, amount);
                                   return true;
                               } else if (action == "whirlwind_leg") {
                                   QueueAction(ACTION_WHIRLWIND_LEG, steps, speed, 0, amount);
                                   return true;
                               }
                               // Fixed actions
                               else if (action == "sit") {
                                   QueueAction(ACTION_SIT, 1, 0, 0, 0);
                                   return true;
                               } else if (action == "showcase") {
                                   QueueAction(ACTION_SHOWCASE, 1, 0, 0, 0);
                                   return true;
                               } else if (action == "home") {
                                   QueueAction(ACTION_HOME, 1, 1000, 1, 0);
                                   return true;
                               }
                               // Hand actions
                               else if (action == "hands_up") {
                                   if (!has_hands_) {
                                       return "Error: this action requires hand servo support";
                                   }
                                   QueueAction(ACTION_HANDS_UP, 1, speed, direction, 0);
                                   return true;
                               } else if (action == "hands_down") {
                                   if (!has_hands_) {
                                       return "Error: this action requires hand servo support";
                                   }
                                   QueueAction(ACTION_HANDS_DOWN, 1, speed, direction, 0);
                                   return true;
                               } else if (action == "hand_wave") {
                                   if (!has_hands_) {
                                       return "Error: this action requires hand servo support";
                                   }
                                   QueueAction(ACTION_HAND_WAVE, 1, 0, 0, direction);
                                   return true;
                               } else if (action == "windmill") {
                                   if (!has_hands_) {
                                       return "Error: this action requires hand servo support";
                                   }
                                   QueueAction(ACTION_WINDMILL, steps, speed, 0, amount);
                                   return true;
                               } else if (action == "takeoff") {
                                   if (!has_hands_) {
                                       return "Error: this action requires hand servo support";
                                   }
                                   QueueAction(ACTION_TAKEOFF, steps, speed, 0, amount);
                                   return true;
                               } else if (action == "fitness") {
                                   if (!has_hands_) {
                                       return "Error: this action requires hand servo support";
                                   }
                                   QueueAction(ACTION_FITNESS, steps, speed, 0, amount);
                                   return true;
                               } else if (action == "greeting") {
                                   if (!has_hands_) {
                                       return "Error: this action requires hand servo support";
                                   }
                                   QueueAction(ACTION_GREETING, steps, 0, direction, 0);
                                   return true;
                               } else if (action == "shy") {
                                   if (!has_hands_) {
                                       return "Error: this action requires hand servo support";
                                   }
                                   QueueAction(ACTION_SHY, steps, 0, direction, 0);
                                   return true;
                               } else if (action == "radio_calisthenics") {
                                   if (!has_hands_) {
                                       return "Error: this action requires hand servo support";
                                   }
                                   QueueAction(ACTION_RADIO_CALISTHENICS, 1, 0, 0, 0);
                                   return true;
                               } else if (action == "magic_circle") {
                                   if (!has_hands_) {
                                       return "Error: this action requires hand servo support";
                                   }
                                   QueueAction(ACTION_MAGIC_CIRCLE, 1, 0, 0, 0);
                                   return true;
                               } else {
                                   return "Error: invalid action name. Available actions: walk, turn, jump, swing, moonwalk, bend, shake_leg, updown, whirlwind_leg, sit, showcase, home, hands_up, hands_down, hand_wave, windmill, takeoff, fitness, greeting, shy, radio_calisthenics, magic_circle";
                               }
                           });


        // Servo sequence tool (segmented send, auto-queued execution)
        mcp_server.AddTool(
            "self.otto.servo_sequences",
            "AI custom motion programming (improvised actions). Supports segmented sequences: for more than 5 sequences, call this tool multiple times with short sequences; they queue and run in order. Supports normal movement and oscillator modes."
            "Robot structure: both arms swing up/down, both legs adduct/abduct, both feet flip up/down."
            "Servo reference:"
            "ll (left leg): adduct/abduct, 0=fully abducted, 90=neutral, 180=fully adducted;"
            "rl (right leg): adduct/abduct, 0=fully adducted, 90=neutral, 180=fully abducted;"
            "lf (left foot): up/down flip, 0=fully up, 90=horizontal, 180=fully down;"
            "rf (right foot): up/down flip, 0=fully down, 90=horizontal, 180=fully up;"
            "lh (left hand): up/down swing, 0=fully down, 90=horizontal, 180=fully up;"
            "rh (right hand): up/down swing, 0=fully up, 90=horizontal, 180=fully down;"
            "sequence: single sequence object with 'a' action array; optional top-level:"
            "'d' (delay in ms after sequence completes, for pauses between sequences)."
            "Each action object contains:"
            "Normal mode: 's' servo positions (keys ll/rl/lf/rf/lh/rh, values 0-180), 'v' speed 100-3000 ms (default 1000), 'd' post-action delay ms (default 0);"
            "Oscillator mode: 'osc' with 'a' amplitudes (10-90 deg, default 20), 'o' center angles (0-180, default 90), 'ph' phase diff (degrees 0-360, default 0), 'p' period 100-3000 ms (default 500), 'c' cycles 0.1-20.0 (default 5.0);"
            "Usage: call multiple times with one sequence each; system queues and executes in order."
            "Important: when oscillating legs/feet, one foot must stay at 90 degrees or the robot may be damaged. After multiple sequences (>1), call self.otto.home separately to reset; do not embed home in sequences."
            "Normal mode example: send 3 sequences, then home:"
            "1st call {\"sequence\":\"{\\\"a\\\":[{\\\"s\\\":{\\\"ll\\\":100},\\\"v\\\":1000}],\\\"d\\\":500}\"},"
            "2nd call {\"sequence\":\"{\\\"a\\\":[{\\\"s\\\":{\\\"ll\\\":90},\\\"v\\\":800}],\\\"d\\\":500}\"},"
            "3rd call {\"sequence\":\"{\\\"a\\\":[{\\\"s\\\":{\\\"ll\\\":80},\\\"v\\\":800}]}\"},"
            "then call self.otto.home."
            "Oscillator examples:"
            "Example 1 - synchronized arms: {\"sequence\":\"{\\\"a\\\":[{\\\"osc\\\":{\\\"a\\\":{\\\"lh\\\":30,\\\"rh\\\":30},\\\"o\\\":{\\\"lh\\\":90,\\\"rh\\\":-90},\\\"p\\\":500,\\\"c\\\":5.0}}],\\\"d\\\":0}\"};"
            "Example 2 - alternating legs (wave): {\"sequence\":\"{\\\"a\\\":[{\\\"osc\\\":{\\\"a\\\":{\\\"ll\\\":20,\\\"rl\\\":20},\\\"o\\\":{\\\"ll\\\":90,\\\"rl\\\":-90},\\\"ph\\\":{\\\"rl\\\":180},\\\"p\\\":600,\\\"c\\\":3.0}}],\\\"d\\\":0}\"};"
            "Example 3 - single leg with fixed foot (safe): {\"sequence\":\"{\\\"a\\\":[{\\\"osc\\\":{\\\"a\\\":{\\\"ll\\\":45},\\\"o\\\":{\\\"ll\\\":90,\\\"lf\\\":90},\\\"p\\\":400,\\\"c\\\":4.0}}],\\\"d\\\":0}\"};"
            "Example 4 - multi-servo (arms and legs): {\"sequence\":\"{\\\"a\\\":[{\\\"osc\\\":{\\\"a\\\":{\\\"lh\\\":25,\\\"rh\\\":25,\\\"ll\\\":15},\\\"o\\\":{\\\"lh\\\":90,\\\"rh\\\":90,\\\"ll\\\":90,\\\"lf\\\":90},\\\"ph\\\":{\\\"rh\\\":180},\\\"p\\\":800,\\\"c\\\":6.0}}],\\\"d\\\":500}\"};"
            "Example 5 - fast sway: {\"sequence\":\"{\\\"a\\\":[{\\\"osc\\\":{\\\"a\\\":{\\\"ll\\\":30,\\\"rl\\\":30},\\\"o\\\":{\\\"ll\\\":90,\\\"rl\\\":90},\\\"ph\\\":{\\\"rl\\\":180},\\\"p\\\":300,\\\"c\\\":10.0}}],\\\"d\\\":0}\"}.",
            PropertyList({Property("sequence", kPropertyTypeString,
                                   "{\"a\":[{\"s\":{\"ll\":90,\"rl\":90},\"v\":1000}]}")}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string sequence = properties["sequence"].value<std::string>();
                // Check if JSON object (string or parsed)
                // Use sequence directly whether JSON string or object string
                QueueServoSequence(sequence.c_str());
                return true;
            });


        mcp_server.AddTool("self.otto.stop", "Stop all actions immediately and reset", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               if (action_task_handle_ != nullptr) {
                                   vTaskDelete(action_task_handle_);
                                   action_task_handle_ = nullptr;
                               }
                               is_action_in_progress_ = false;
                               PowerManager::ResumeBatteryUpdate();  // Resume battery updates when stopping
                               xQueueReset(action_queue_);

                               QueueAction(ACTION_HOME, 1, 1000, 1, 0);
                               return true;
                           });

        mcp_server.AddTool(
            "self.otto.set_trim",
            "Calibrate a single servo position. Set trim for the specified servo to adjust initial standing posture; saved permanently. "
            "servo_type: servo type (left_leg/right_leg/left_foot/right_foot/left_hand/right_hand); "
            "trim_value: trim value (-50 to 50 degrees)",
            PropertyList({Property("servo_type", kPropertyTypeString, "left_leg"),
                          Property("trim_value", kPropertyTypeInteger, 0, -50, 50)}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string servo_type = properties["servo_type"].value<std::string>();
                int trim_value = properties["trim_value"].value<int>();

                ESP_LOGI(TAG, "Setting servo trim: %s = %d degrees", servo_type.c_str(), trim_value);

                // Get all current trim values
                Settings settings("otto_trims", true);
                int left_leg = settings.GetInt("left_leg", 0);
                int right_leg = settings.GetInt("right_leg", 0);
                int left_foot = settings.GetInt("left_foot", 0);
                int right_foot = settings.GetInt("right_foot", 0);
                int left_hand = settings.GetInt("left_hand", 0);
                int right_hand = settings.GetInt("right_hand", 0);

                // Update trim for specified servo
                if (servo_type == "left_leg") {
                    left_leg = trim_value;
                    settings.SetInt("left_leg", left_leg);
                } else if (servo_type == "right_leg") {
                    right_leg = trim_value;
                    settings.SetInt("right_leg", right_leg);
                } else if (servo_type == "left_foot") {
                    left_foot = trim_value;
                    settings.SetInt("left_foot", left_foot);
                } else if (servo_type == "right_foot") {
                    right_foot = trim_value;
                    settings.SetInt("right_foot", right_foot);
                } else if (servo_type == "left_hand") {
                    if (!has_hands_) {
                        return "Error: hand servos are not configured on this robot";
                    }
                    left_hand = trim_value;
                    settings.SetInt("left_hand", left_hand);
                } else if (servo_type == "right_hand") {
                    if (!has_hands_) {
                        return "Error: hand servos are not configured on this robot";
                    }
                    right_hand = trim_value;
                    settings.SetInt("right_hand", right_hand);
                } else {
                    return "Error: invalid servo type, use: left_leg, right_leg, left_foot, "
                           "right_foot, left_hand, right_hand";
                }

                otto_.SetTrims(left_leg, right_leg, left_foot, right_foot, left_hand, right_hand);

                QueueAction(ACTION_JUMP, 1, 500, 0, 0);

                return "Servo " + servo_type + " trim set to " + std::to_string(trim_value) +
                       " degrees, saved permanently";
            });

        mcp_server.AddTool("self.otto.get_trims", "Get current servo trim settings", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               Settings settings("otto_trims", false);

                               int left_leg = settings.GetInt("left_leg", 0);
                               int right_leg = settings.GetInt("right_leg", 0);
                               int left_foot = settings.GetInt("left_foot", 0);
                               int right_foot = settings.GetInt("right_foot", 0);
                               int left_hand = settings.GetInt("left_hand", 0);
                               int right_hand = settings.GetInt("right_hand", 0);

                               std::string result =
                                   "{\"left_leg\":" + std::to_string(left_leg) +
                                   ",\"right_leg\":" + std::to_string(right_leg) +
                                   ",\"left_foot\":" + std::to_string(left_foot) +
                                   ",\"right_foot\":" + std::to_string(right_foot) +
                                   ",\"left_hand\":" + std::to_string(left_hand) +
                                   ",\"right_hand\":" + std::to_string(right_hand) + "}";

                               ESP_LOGI(TAG, "Getting trim settings: %s", result.c_str());
                               return result;
                           });

        mcp_server.AddTool("self.otto.get_status", "Get robot status; returns moving or idle",
                           PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
                               return is_action_in_progress_ ? "moving" : "idle";
                           });

        mcp_server.AddTool("self.battery.get_level", "Get robot battery level and charging status", PropertyList(),
                           [](const PropertyList& properties) -> ReturnValue {
                               auto& board = Board::GetInstance();
                               int level = 0;
                               bool charging = false;
                               bool discharging = false;
                               board.GetBatteryLevel(level, charging, discharging);

                               std::string status =
                                   "{\"level\":" + std::to_string(level) +
                                   ",\"charging\":" + (charging ? "true" : "false") + "}";
                               return status;
                           });
                           
        mcp_server.AddTool("self.otto.get_ip", "Get robot WiFi IP address", PropertyList(),
                           [](const PropertyList& properties) -> ReturnValue {
                               auto& wifi = WifiManager::GetInstance();
                               std::string ip = wifi.GetIpAddress();
                               if (ip.empty()) {
                                   return "{\"ip\":\"\",\"connected\":false}";
                               }
                               std::string status = "{\"ip\":\"" + ip + "\",\"connected\":true}";
                               return status;
                           });                           

        ESP_LOGI(TAG, "MCP tool registration complete");
    }

    ~OttoController() {
        if (action_task_handle_ != nullptr) {
            vTaskDelete(action_task_handle_);
            action_task_handle_ = nullptr;
        }
        vQueueDelete(action_queue_);
    }
};

static OttoController* g_otto_controller = nullptr;

void InitializeOttoController(const HardwareConfig& hw_config) {
    if (g_otto_controller == nullptr) {
        g_otto_controller = new OttoController(hw_config);
        ESP_LOGI(TAG, "Otto controller initialized and MCP tools registered");
    }
}
