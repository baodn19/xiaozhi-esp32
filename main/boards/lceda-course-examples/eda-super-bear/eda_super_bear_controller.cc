/*
    EdaRobot controller - MCP protocol version
*/

#include <cJSON.h>
#include <esp_log.h>

#include <cstring>

#include "application.h"
#include "board.h"
#include "config.h"
#include "mcp_server.h"
#include "eda_super_bear_movements.h"
#include "sdkconfig.h"
#include "settings.h"

#define TAG "EdaSuperBearController"

class EdaSuperBearController {
private:
    EdaRobot edarobot_;
    TaskHandle_t action_task_handle_ = nullptr;
    QueueHandle_t action_queue_;
    bool has_hands_ = false;
    bool is_action_in_progress_ = false;

    struct EdaRobotActionParams {
        int action_type;
        int steps;
        int speed;
        int direction;
        int amount;
    };

    enum ActionType {
        ACTION_WALK = 1,
        ACTION_TURN = 2,
        ACTION_JUMP = 3,
        ACTION_SWING = 4,
        ACTION_MOONWALK = 5,
        ACTION_BEND = 6,
        ACTION_SHAKE_LEG = 7,
        ACTION_UPDOWN = 8,
        ACTION_TIPTOE_SWING = 9,
        ACTION_JITTER = 10,
        ACTION_ASCENDING_TURN = 11,
        ACTION_CRUSAITO = 12,
        ACTION_FLAPPING = 13,
        ACTION_HANDS_UP = 14,
        ACTION_HANDS_DOWN = 15,
        ACTION_HAND_WAVE = 16,
        ACTION_HOME = 17
    };

    static void ActionTask(void* arg) {
        EdaSuperBearController* controller = static_cast<EdaSuperBearController*>(arg);
        EdaRobotActionParams params;
        controller->edarobot_.AttachServos();

        while (true) {
            if (xQueueReceive(controller->action_queue_, &params, pdMS_TO_TICKS(1000)) == pdTRUE) {
                ESP_LOGI(TAG, "Executing action: %d", params.action_type);
                controller->is_action_in_progress_ = true;

                switch (params.action_type) {
                    case ACTION_WALK:
                        controller->edarobot_.Walk(params.steps, params.speed, params.direction,
                                               params.amount);
                        break;
                    case ACTION_TURN:
                        controller->edarobot_.Turn(params.steps, params.speed, params.direction,
                                               params.amount);
                        break;
                    case ACTION_JUMP:
                        controller->edarobot_.Jump(params.steps, params.speed);
                        break;
                    case ACTION_SWING:
                        controller->edarobot_.Swing(params.steps, params.speed, params.amount);
                        break;
                    case ACTION_MOONWALK:
                        controller->edarobot_.Moonwalker(params.steps, params.speed, params.amount,
                                                     params.direction);
                        break;
                    case ACTION_BEND:
                        controller->edarobot_.Bend(params.steps, params.speed, params.direction);
                        break;
                    case ACTION_SHAKE_LEG:
                        controller->edarobot_.ShakeLeg(params.steps, params.speed, params.direction);
                        break;
                    case ACTION_UPDOWN:
                        controller->edarobot_.UpDown(params.steps, params.speed, params.amount);
                        break;
                    case ACTION_TIPTOE_SWING:
                        controller->edarobot_.TiptoeSwing(params.steps, params.speed, params.amount);
                        break;
                    case ACTION_JITTER:
                        controller->edarobot_.Jitter(params.steps, params.speed, params.amount);
                        break;
                    case ACTION_ASCENDING_TURN:
                        controller->edarobot_.AscendingTurn(params.steps, params.speed, params.amount);
                        break;
                    case ACTION_CRUSAITO:
                        controller->edarobot_.Crusaito(params.steps, params.speed, params.amount,
                                                   params.direction);
                        break;
                    case ACTION_FLAPPING:
                        controller->edarobot_.Flapping(params.steps, params.speed, params.amount,
                                                   params.direction);
                        break;
                    case ACTION_HANDS_UP:
                        if (controller->has_hands_) {
                            controller->edarobot_.HandsUp(params.speed, params.direction);
                        }
                        break;
                    case ACTION_HANDS_DOWN:
                        if (controller->has_hands_) {
                            controller->edarobot_.HandsDown(params.speed, params.direction);
                        }
                        break;
                    case ACTION_HAND_WAVE:
                        if (controller->has_hands_) {
                            controller->edarobot_.HandWave(params.speed, params.direction);
                        }
                        break;
                    case ACTION_HOME:
                        controller->edarobot_.Home(params.direction == 1);
                        break;
                }
                if (params.action_type != ACTION_HOME) {
                    controller->edarobot_.Home(params.action_type < ACTION_HANDS_UP);
                }
                controller->is_action_in_progress_ = false;
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }

    void StartActionTaskIfNeeded() {
        if (action_task_handle_ == nullptr) {
            xTaskCreate(ActionTask, "edarobot_action", 1024 * 3, this, configMAX_PRIORITIES - 1,
                        &action_task_handle_);
        }
    }

    void QueueAction(int action_type, int steps, int speed, int direction, int amount) {
        // Check hand actions
        if ((action_type >= ACTION_HANDS_UP && action_type <= ACTION_HAND_WAVE) && !has_hands_) {
            ESP_LOGW(TAG, "Hand action requested but robot has no hand servos configured");
            return;
        }

        ESP_LOGI(TAG, "Action control: type=%d, steps=%d, speed=%d, direction=%d, amount=%d", action_type, steps,
                 speed, direction, amount);

        EdaRobotActionParams params = {action_type, steps, speed, direction, amount};
        xQueueSend(action_queue_, &params, portMAX_DELAY);
        StartActionTaskIfNeeded();
    }

    void LoadTrimsFromNVS() {
        Settings settings("edarobot_trims", false);

        int left_leg = settings.GetInt("left_leg", 0);
        int right_leg = settings.GetInt("right_leg", 0);
        int left_foot = settings.GetInt("left_foot", 0);
        int right_foot = settings.GetInt("right_foot", 0);
        int left_hand = settings.GetInt("left_hand", 0);
        int right_hand = settings.GetInt("right_hand", 0);

        ESP_LOGI(TAG, "Loaded trim settings from NVS: left_leg=%d, right_leg=%d, left_foot=%d, right_foot=%d, left_hand=%d, right_hand=%d",
                 left_leg, right_leg, left_foot, right_foot, left_hand, right_hand);

        edarobot_.SetTrims(left_leg, right_leg, left_foot, right_foot, left_hand, right_hand);
    }

public:
    EdaSuperBearController() {
        edarobot_.Init(LEFT_LEG_PIN, RIGHT_LEG_PIN, LEFT_FOOT_PIN, RIGHT_FOOT_PIN, LEFT_HAND_PIN,
                   RIGHT_HAND_PIN);

        has_hands_ = (LEFT_HAND_PIN != -1 && RIGHT_HAND_PIN != -1);
        ESP_LOGI(TAG, "EdaRobot initialized %s hand servos", has_hands_ ? "" : "without ");

        LoadTrimsFromNVS();

        action_queue_ = xQueueCreate(10, sizeof(EdaRobotActionParams));

        QueueAction(ACTION_HOME, 1, 1000, 1, 0);  // direction=1 resets hands

        RegisterMcpTools();
    }

    void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();

        ESP_LOGI(TAG, "Registering MCP tools...");

        // Basic locomotion actions
        mcp_server.AddTool("self.edarobot.walk_forward",
                           "Walk. steps: step count (1-100); speed: walk speed (500-1500, lower is faster); "
                           "direction: walk direction (-1=backward, 1=forward); arm_swing: arm swing amount (0-170 deg)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 3, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("arm_swing", kPropertyTypeInteger, 50, 0, 170),
                                         Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int arm_swing = properties["arm_swing"].value<int>();
                               int direction = properties["direction"].value<int>();
                               QueueAction(ACTION_WALK, steps, speed, direction, arm_swing);
                               return true;
                           });

        mcp_server.AddTool("self.edarobot.turn_left",
                           "Turn. steps: turn steps (1-100); speed: turn speed (500-1500, lower is faster); "
                           "direction: turn direction (1=left, -1=right); arm_swing: arm swing amount (0-170 deg)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 3, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("arm_swing", kPropertyTypeInteger, 50, 0, 170),
                                         Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int arm_swing = properties["arm_swing"].value<int>();
                               int direction = properties["direction"].value<int>();
                               QueueAction(ACTION_TURN, steps, speed, direction, arm_swing);
                               return true;
                           });

        mcp_server.AddTool("self.edarobot.jump",
                           "Jump. steps: jump count (1-100); speed: jump speed (500-1500, lower is faster)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 1, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               QueueAction(ACTION_JUMP, steps, speed, 0, 0);
                               return true;
                           });

        // Special actions
        mcp_server.AddTool("self.edarobot.swing",
                           "Swing side to side. steps: swing count (1-100); speed: "
                           "swing speed (500-1500, lower is faster); amount: swing amount (0-170 deg)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 3, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("amount", kPropertyTypeInteger, 30, 0, 170)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int amount = properties["amount"].value<int>();
                               QueueAction(ACTION_SWING, steps, speed, 0, amount);
                               return true;
                           });

        mcp_server.AddTool("self.edarobot.moonwalk",
                           "Moonwalk. steps: moonwalk steps (1-100); speed: speed (500-1500, lower is faster); "
                           "direction: direction (1=left, -1=right); amount: amount (0-170 deg)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 3, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("direction", kPropertyTypeInteger, 1, -1, 1),
                                         Property("amount", kPropertyTypeInteger, 25, 0, 170)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int direction = properties["direction"].value<int>();
                               int amount = properties["amount"].value<int>();
                               QueueAction(ACTION_MOONWALK, steps, speed, direction, amount);
                               return true;
                           });

        mcp_server.AddTool("self.edarobot.bend",
                           "Bend body. steps: bend count (1-100); speed: "
                           "bend speed (500-1500, lower is faster); direction: bend direction (1=left, -1=right)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 1, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int direction = properties["direction"].value<int>();
                               QueueAction(ACTION_BEND, steps, speed, direction, 0);
                               return true;
                           });

        mcp_server.AddTool("self.edarobot.shake_leg",
                           "Shake leg. steps: shake count (1-100); speed: shake speed (500-1500, lower is faster); "
                           "direction: leg selection (1=left leg, -1=right leg)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 1, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int direction = properties["direction"].value<int>();
                               QueueAction(ACTION_SHAKE_LEG, steps, speed, direction, 0);
                               return true;
                           });

        mcp_server.AddTool("self.edarobot.updown",
                           "Up/down motion. steps: motion count (1-100); speed: "
                           "motion speed (500-1500, lower is faster); amount: motion amount (0-170 deg)",
                           PropertyList({Property("steps", kPropertyTypeInteger, 3, 1, 100),
                                         Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                                         Property("amount", kPropertyTypeInteger, 20, 0, 170)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int steps = properties["steps"].value<int>();
                               int speed = properties["speed"].value<int>();
                               int amount = properties["amount"].value<int>();
                               QueueAction(ACTION_UPDOWN, steps, speed, 0, amount);
                               return true;
                           });

        // Hand actions (only when hand servos are configured)
        if (has_hands_) {
            mcp_server.AddTool(
                "self.edarobot.hands_up",
                "Raise hand(s). speed: raise speed (500-1500, lower is faster); direction: hand selection (1=left, "
                "-1=right, 0=both)",
                PropertyList({Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                              Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                [this](const PropertyList& properties) -> ReturnValue {
                    int speed = properties["speed"].value<int>();
                    int direction = properties["direction"].value<int>();
                    QueueAction(ACTION_HANDS_UP, 1, speed, direction, 0);
                    return true;
                });

            mcp_server.AddTool(
                "self.edarobot.hands_down",
                "Lower hand(s). speed: lower speed (500-1500, lower is faster); direction: hand selection (1=left, "
                "-1=right, 0=both)",
                PropertyList({Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                              Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                [this](const PropertyList& properties) -> ReturnValue {
                    int speed = properties["speed"].value<int>();
                    int direction = properties["direction"].value<int>();
                    QueueAction(ACTION_HANDS_DOWN, 1, speed, direction, 0);
                    return true;
                });

            mcp_server.AddTool(
                "self.edarobot.hand_wave",
                "Wave hand(s). speed: wave speed (500-1500, lower is faster); direction: hand selection (1=left, "
                "-1=right, 0=both)",
                PropertyList({Property("speed", kPropertyTypeInteger, 1000, 500, 1500),
                              Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
                [this](const PropertyList& properties) -> ReturnValue {
                    int speed = properties["speed"].value<int>();
                    int direction = properties["direction"].value<int>();
                    QueueAction(ACTION_HAND_WAVE, 1, speed, direction, 0);
                    return true;
                });
        }

        // System tools
        mcp_server.AddTool("self.edarobot.stop", "Stop immediately", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               if (action_task_handle_ != nullptr) {
                                   vTaskDelete(action_task_handle_);
                                   action_task_handle_ = nullptr;
                               }
                               is_action_in_progress_ = false;
                               xQueueReset(action_queue_);

                               QueueAction(ACTION_HOME, 1, 1000, 1, 0);
                               return true;
                           });

        mcp_server.AddTool(
            "self.edarobot.set_trim",
            "Calibrate a single servo. Set trim to adjust EdaRobot standing posture; saved permanently."
            "servo_type: servo type (left_leg/right_leg/left_foot/right_foot/left_hand/right_hand); "
            "trim_value: trim value (-50 to 50 degrees)",
            PropertyList({Property("servo_type", kPropertyTypeString, "left_leg"),
                          Property("trim_value", kPropertyTypeInteger, 0, -50, 50)}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string servo_type = properties["servo_type"].value<std::string>();
                int trim_value = properties["trim_value"].value<int>();

                ESP_LOGI(TAG, "Set servo trim: %s = %d degrees", servo_type.c_str(), trim_value);

                // Get all current trim values
                Settings settings("edarobot_trims", true);
                int left_leg = settings.GetInt("left_leg", 0);
                int right_leg = settings.GetInt("right_leg", 0);
                int left_foot = settings.GetInt("left_foot", 0);
                int right_foot = settings.GetInt("right_foot", 0);
                int left_hand = settings.GetInt("left_hand", 0);
                int right_hand = settings.GetInt("right_hand", 0);

                // Update trim for the specified servo
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
                        return "Error: robot has no hand servos configured";
                    }
                    left_hand = trim_value;
                    settings.SetInt("left_hand", left_hand);
                } else if (servo_type == "right_hand") {
                    if (!has_hands_) {
                        return "Error: robot has no hand servos configured";
                    }
                    right_hand = trim_value;
                    settings.SetInt("right_hand", right_hand);
                } else {
                    return "Error: invalid servo type. Use: left_leg, right_leg, left_foot, "
                           "right_foot, left_hand, right_hand";
                }

                edarobot_.SetTrims(left_leg, right_leg, left_foot, right_foot, left_hand, right_hand);

                QueueAction(ACTION_JUMP, 1, 500, 0, 0);

                return "Servo " + servo_type + " trim set to " + std::to_string(trim_value) +
                       " degrees, saved permanently";
            });

        mcp_server.AddTool("self.edarobot.get_trims", "Get current servo trim settings", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               Settings settings("edarobot_trims", false);

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

                               ESP_LOGI(TAG, "Get trim settings: %s", result.c_str());
                               return result;
                           });

        mcp_server.AddTool("self.edarobot.get_status", "Get robot status; returns moving or idle",
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

        ESP_LOGI(TAG, "MCP tool registration complete");
    }

    ~EdaSuperBearController() {
        if (action_task_handle_ != nullptr) {
            vTaskDelete(action_task_handle_);
            action_task_handle_ = nullptr;
        }
        vQueueDelete(action_queue_);
    }
};

static EdaSuperBearController* g_edarobot_controller = nullptr;

void InitializeEdaSuperBearController() {
    if (g_edarobot_controller == nullptr) {
        g_edarobot_controller = new EdaSuperBearController();
        ESP_LOGI(TAG, "EdaRobot controller initialized and MCP tools registered");
    }
}
