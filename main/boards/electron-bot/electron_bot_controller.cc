/*
    Electron Bot controller - MCP protocol version
*/

#include <cJSON.h>
#include <esp_log.h>

#include <cstring>

#include "application.h"
#include "board.h"
#include "config.h"
#include "mcp_server.h"
#include "movements.h"
#include "sdkconfig.h"
#include "settings.h"

#define TAG "ElectronBotController"

struct ElectronBotActionParams {
    int action_type;
    int steps;
    int speed;
    int direction;
    int amount;
};

class ElectronBotController {
private:
    Otto electron_bot_;
    TaskHandle_t action_task_handle_ = nullptr;
    QueueHandle_t action_queue_;
    bool is_action_in_progress_ = false;

    enum ActionType {
        // Hand actions 1-12
        ACTION_HAND_LEFT_UP = 1,      // Raise left hand
        ACTION_HAND_RIGHT_UP = 2,     // Raise right hand
        ACTION_HAND_BOTH_UP = 3,      // Raise both hands
        ACTION_HAND_LEFT_DOWN = 4,    // Lower left hand
        ACTION_HAND_RIGHT_DOWN = 5,   // Lower right hand
        ACTION_HAND_BOTH_DOWN = 6,    // Lower both hands
        ACTION_HAND_LEFT_WAVE = 7,    // Wave left hand
        ACTION_HAND_RIGHT_WAVE = 8,   // Wave right hand
        ACTION_HAND_BOTH_WAVE = 9,    // Wave both hands
        ACTION_HAND_LEFT_FLAP = 10,   // Clap left hand
        ACTION_HAND_RIGHT_FLAP = 11,  // Clap right hand
        ACTION_HAND_BOTH_FLAP = 12,   // Clap both hands

        // Body actions 13-14
        ACTION_BODY_TURN_LEFT = 13,    // Turn left
        ACTION_BODY_TURN_RIGHT = 14,   // Turn right
        ACTION_BODY_TURN_CENTER = 15,  // Return to center

        // Head actions 16-20
        ACTION_HEAD_UP = 16,          // Look up
        ACTION_HEAD_DOWN = 17,        // Look down
        ACTION_HEAD_NOD_ONCE = 18,    // Nod once
        ACTION_HEAD_CENTER = 19,      // Return to center
        ACTION_HEAD_NOD_REPEAT = 20,  // Repeat nod

        // System action 21
        ACTION_HOME = 21  // Reset to home position
    };

    static void ActionTask(void* arg) {
        ElectronBotController* controller = static_cast<ElectronBotController*>(arg);
        ElectronBotActionParams params;
        controller->electron_bot_.AttachServos();

        while (true) {
            if (xQueueReceive(controller->action_queue_, &params, pdMS_TO_TICKS(1000)) == pdTRUE) {
                ESP_LOGI(TAG, "Executing action: %d", params.action_type);
                controller->is_action_in_progress_ = true;  // Action started

                // Execute the corresponding action
                if (params.action_type >= ACTION_HAND_LEFT_UP &&
                    params.action_type <= ACTION_HAND_BOTH_FLAP) {
                    // Hand actions
                    controller->electron_bot_.HandAction(params.action_type, params.steps,
                                                         params.amount, params.speed);
                } else if (params.action_type >= ACTION_BODY_TURN_LEFT &&
                           params.action_type <= ACTION_BODY_TURN_CENTER) {
                    // Body actions
                    int body_direction = params.action_type - ACTION_BODY_TURN_LEFT + 1;
                    controller->electron_bot_.BodyAction(body_direction, params.steps,
                                                         params.amount, params.speed);
                } else if (params.action_type >= ACTION_HEAD_UP &&
                           params.action_type <= ACTION_HEAD_NOD_REPEAT) {
                    // Head actions
                    int head_action = params.action_type - ACTION_HEAD_UP + 1;
                    controller->electron_bot_.HeadAction(head_action, params.steps, params.amount,
                                                         params.speed);
                } else if (params.action_type == ACTION_HOME) {
                    // Home action
                    controller->electron_bot_.Home(true);
                }
                if (params.action_type != ACTION_HOME) {
                    UBaseType_t pending_actions =
                        uxQueueMessagesWaiting(controller->action_queue_);
                    // Skip intermediate homing during chained actions
                    if (pending_actions == 0) {
                        controller->electron_bot_.Home(true);
                    }
                }
                controller->is_action_in_progress_ = false;  // Action finished
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void QueueAction(int action_type, int steps, int speed, int direction, int amount) {
        ESP_LOGI(TAG, "Action control: type=%d, steps=%d, speed=%d, direction=%d, amount=%d", action_type, steps,
                 speed, direction, amount);

        ElectronBotActionParams params = {action_type, steps, speed, direction, amount};
        xQueueSend(action_queue_, &params, portMAX_DELAY);
        StartActionTaskIfNeeded();
    }

    void StartActionTaskIfNeeded() {
        if (action_task_handle_ == nullptr) {
            xTaskCreate(ActionTask, "electron_bot_action", 1024 * 4, this, configMAX_PRIORITIES - 1,
                        &action_task_handle_);
        }
    }

    void LoadTrimsFromNVS() {
        Settings settings("electron_trims", false);

        int right_pitch = settings.GetInt("right_pitch", 0);
        int right_roll = settings.GetInt("right_roll", 0);
        int left_pitch = settings.GetInt("left_pitch", 0);
        int left_roll = settings.GetInt("left_roll", 0);
        int body = settings.GetInt("body", 0);
        int head = settings.GetInt("head", 0);
        electron_bot_.SetTrims(right_pitch, right_roll, left_pitch, left_roll, body, head);
    }

public:
    ElectronBotController() {
        electron_bot_.Init(Right_Pitch_Pin, Right_Roll_Pin, Left_Pitch_Pin, Left_Roll_Pin, Body_Pin,
                           Head_Pin);

        LoadTrimsFromNVS();
        action_queue_ = xQueueCreate(10, sizeof(ElectronBotActionParams));

        QueueAction(ACTION_HOME, 1, 1000, 0, 0);

        RegisterMcpTools();
        ESP_LOGI(TAG, "Electron Bot controller initialized and MCP tools registered");
    }

    void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();

        ESP_LOGI(TAG, "Registering Electron Bot MCP tools...");

        // Unified hand action tool
        mcp_server.AddTool(
            "self.electron.hand_action",
            "Hand action control. action: 1=raise, 2=lower, 3=wave, 4=clap; hand: 1=left, 2=right, 3=both; "
            "steps: repeat count (1-10); speed: action speed (500-1500, lower is faster); amount: "
            "action amount (10-50, raise only)",
            PropertyList({Property("action", kPropertyTypeInteger, 1, 1, 4),
                          Property("hand", kPropertyTypeInteger, 3, 1, 3),
                          Property("steps", kPropertyTypeInteger, 1, 1, 10),
                          Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                          Property("amount", kPropertyTypeInteger, 30, 10, 50)}),
            [this](const PropertyList& properties) -> ReturnValue {
                int action_type = properties["action"].value<int>();
                int hand_type = properties["hand"].value<int>();
                int steps = properties["steps"].value<int>();
                int speed = properties["speed"].value<int>();
                int amount = properties["amount"].value<int>();

                // Map action and hand type to concrete action
                int base_action;
                switch (action_type) {
                    case 1:
                        base_action = ACTION_HAND_LEFT_UP;
                        break;  // Raise
                    case 2:
                        base_action = ACTION_HAND_LEFT_DOWN;
                        amount = 0;
                        break;  // Lower
                    case 3:
                        base_action = ACTION_HAND_LEFT_WAVE;
                        amount = 0;
                        break;  // Wave
                    case 4:
                        base_action = ACTION_HAND_LEFT_FLAP;
                        amount = 0;
                        break;  // Clap
                    default:
                        base_action = ACTION_HAND_LEFT_UP;
                }
                int action_id = base_action + (hand_type - 1);

                QueueAction(action_id, steps, speed, 0, amount);
                return true;
            });

        // Body actions
        mcp_server.AddTool(
            "self.electron.body_turn",
            "Body turn. steps: turn steps (1-10); speed: turn speed (500-1500, lower is faster); direction: "
            "turn direction (1=left, 2=right, 3=center); angle: turn angle (0-90 deg)",
            PropertyList({Property("steps", kPropertyTypeInteger, 1, 1, 10),
                          Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                          Property("direction", kPropertyTypeInteger, 1, 1, 3),
                          Property("angle", kPropertyTypeInteger, 45, 0, 90)}),
            [this](const PropertyList& properties) -> ReturnValue {
                int steps = properties["steps"].value<int>();
                int speed = properties["speed"].value<int>();
                int direction = properties["direction"].value<int>();
                int amount = properties["angle"].value<int>();

                int action;
                switch (direction) {
                    case 1:
                        action = ACTION_BODY_TURN_LEFT;
                        break;
                    case 2:
                        action = ACTION_BODY_TURN_RIGHT;
                        break;
                    case 3:
                        action = ACTION_BODY_TURN_CENTER;
                        break;
                    default:
                        action = ACTION_BODY_TURN_LEFT;
                }

                QueueAction(action, steps, speed, 0, amount);
                return true;
            });

        // Head actions
        mcp_server.AddTool("self.electron.head_move",
                           "Head motion. action: 1=look up, 2=look down, 3=nod once, 4=center, 5=repeat nod; steps: "
                           "repeat count (1-10); speed: action speed (500-1500, lower is faster); angle: "
                           "head rotation angle (1-15 deg)",
                           PropertyList({Property("action", kPropertyTypeInteger, 3, 1, 5),
                                         Property("steps", kPropertyTypeInteger, 1, 1, 10),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("angle", kPropertyTypeInteger, 5, 1, 15)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int action_num = properties["action"].value<int>();
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int amount = properties["angle"].value<int>();
                               int action = ACTION_HEAD_UP + (action_num - 1);
                               QueueAction(action, steps, speed, 0, amount);
                               return true;
                           });

        // System tools
        mcp_server.AddTool("self.electron.stop", "Stop immediately", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               // Clear queue but keep task alive
                               xQueueReset(action_queue_);
                               is_action_in_progress_ = false;
                               QueueAction(ACTION_HOME, 1, 1000, 0, 0);
                               return true;
                           });

        mcp_server.AddTool("self.electron.get_status", "Get robot status; returns moving or idle",
                           PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
                               return is_action_in_progress_ ? "moving" : "idle";
                           });

        // Single-servo calibration tool
        mcp_server.AddTool(
            "self.electron.set_trim",
            "Calibrate a single servo. Set trim to adjust ElectronBot initial posture; saved permanently."
            "servo_type: servo type (right_pitch: right arm rotation, right_roll: right arm push/pull, left_pitch: left arm rotation, "
            "left_roll: left arm push/pull, body: body, head: head); "
            "trim_value: trim value (-30 to 30 degrees)",
            PropertyList({Property("servo_type", kPropertyTypeString, "right_pitch"),
                          Property("trim_value", kPropertyTypeInteger, 0, -30, 30)}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string servo_type = properties["servo_type"].value<std::string>();
                int trim_value = properties["trim_value"].value<int>();

                ESP_LOGI(TAG, "Set servo trim: %s = %d degrees", servo_type.c_str(), trim_value);

                // Get all current trim values
                Settings settings("electron_trims", true);
                int right_pitch = settings.GetInt("right_pitch", 0);
                int right_roll = settings.GetInt("right_roll", 0);
                int left_pitch = settings.GetInt("left_pitch", 0);
                int left_roll = settings.GetInt("left_roll", 0);
                int body = settings.GetInt("body", 0);
                int head = settings.GetInt("head", 0);

                // Update trim for the specified servo
                if (servo_type == "right_pitch") {
                    right_pitch = trim_value;
                    settings.SetInt("right_pitch", right_pitch);
                } else if (servo_type == "right_roll") {
                    right_roll = trim_value;
                    settings.SetInt("right_roll", right_roll);
                } else if (servo_type == "left_pitch") {
                    left_pitch = trim_value;
                    settings.SetInt("left_pitch", left_pitch);
                } else if (servo_type == "left_roll") {
                    left_roll = trim_value;
                    settings.SetInt("left_roll", left_roll);
                } else if (servo_type == "body") {
                    body = trim_value;
                    settings.SetInt("body", body);
                } else if (servo_type == "head") {
                    head = trim_value;
                    settings.SetInt("head", head);
                } else {
                    return "Error: invalid servo type. Use: right_pitch, right_roll, left_pitch, "
                           "left_roll, body, head";
                }

                electron_bot_.SetTrims(right_pitch, right_roll, left_pitch, left_roll, body, head);

                QueueAction(ACTION_HOME, 1, 500, 0, 0);

                return "Servo " + servo_type + " trim set to " + std::to_string(trim_value) +
                       " degrees, saved permanently";
            });

        mcp_server.AddTool("self.electron.get_trims", "Get current servo trim settings", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               Settings settings("electron_trims", false);

                               int right_pitch = settings.GetInt("right_pitch", 0);
                               int right_roll = settings.GetInt("right_roll", 0);
                               int left_pitch = settings.GetInt("left_pitch", 0);
                               int left_roll = settings.GetInt("left_roll", 0);
                               int body = settings.GetInt("body", 0);
                               int head = settings.GetInt("head", 0);

                               std::string result =
                                   "{\"right_pitch\":" + std::to_string(right_pitch) +
                                   ",\"right_roll\":" + std::to_string(right_roll) +
                                   ",\"left_pitch\":" + std::to_string(left_pitch) +
                                   ",\"left_roll\":" + std::to_string(left_roll) +
                                   ",\"body\":" + std::to_string(body) +
                                   ",\"head\":" + std::to_string(head) + "}";

                               ESP_LOGI(TAG, "Get trim settings: %s", result.c_str());
                               return result;
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

        ESP_LOGI(TAG, "Electron Bot MCP tool registration complete");
    }

    ~ElectronBotController() {
        if (action_task_handle_ != nullptr) {
            vTaskDelete(action_task_handle_);
            action_task_handle_ = nullptr;
        }
        vQueueDelete(action_queue_);
    }
};

static ElectronBotController* g_electron_controller = nullptr;

void InitializeElectronBotController() {
    if (g_electron_controller == nullptr) {
        g_electron_controller = new ElectronBotController();
        ESP_LOGI(TAG, "Electron Bot controller initialized and MCP tools registered");
    }
}
