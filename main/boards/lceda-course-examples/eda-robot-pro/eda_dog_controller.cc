/*
    EDA robot dog controller - MCP protocol version
*/

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "application.h"
#include "board.h"
#include "config.h"
#include "eda_dog_movements.h"
#include "mcp_server.h"
#include "sdkconfig.h"
#include "settings.h"

#define TAG "EDARobotDogController"

class EDARobotDogController {
private:
  EDARobotDog dog_;
  TaskHandle_t action_task_handle_ = nullptr;
  QueueHandle_t action_queue_;
  bool is_action_in_progress_ = false;

  struct DogActionParams {
    int action_type;
    int steps;
    int speed;
    int direction;
    int height;
  };

  enum ActionType {
    ACTION_WALK = 1,
    ACTION_TURN = 2,
    ACTION_SIT = 3,
    ACTION_STAND = 4,
    ACTION_STRETCH = 5,
    ACTION_SHAKE = 6,
    ACTION_LIFT_LEFT_FRONT = 7,
    ACTION_LIFT_LEFT_REAR = 8,
    ACTION_LIFT_RIGHT_FRONT = 9,
    ACTION_LIFT_RIGHT_REAR = 10,
    ACTION_HOME = 11
  };

  static void ActionTask(void *arg) {
    EDARobotDogController *controller = static_cast<EDARobotDogController *>(arg);
    DogActionParams params;
    controller->dog_.AttachServos();

    while (true) {
      if (xQueueReceive(controller->action_queue_, &params,
                        pdMS_TO_TICKS(1000)) == pdTRUE) {
        ESP_LOGI(TAG, "Executing action: %d", params.action_type);
        controller->is_action_in_progress_ = true;

        switch (params.action_type) {
        case ACTION_WALK:
          controller->dog_.Walk(params.steps, params.speed, params.direction);
          break;
        case ACTION_TURN:
          controller->dog_.Turn(params.steps, params.speed, params.direction);
          break;
        case ACTION_SIT:
          controller->dog_.Sit(params.speed);
          break;
        case ACTION_STAND:
          controller->dog_.Stand(params.speed);
          break;
        case ACTION_STRETCH:
          controller->dog_.Stretch(params.speed);
          break;
        case ACTION_SHAKE:
          controller->dog_.Shake(params.speed);
          break;
        case ACTION_LIFT_LEFT_FRONT:
          controller->dog_.LiftLeftFrontLeg(params.speed, params.height);
          break;
        case ACTION_LIFT_LEFT_REAR:
          controller->dog_.LiftLeftRearLeg(params.speed, params.height);
          break;
        case ACTION_LIFT_RIGHT_FRONT:
          controller->dog_.LiftRightFrontLeg(params.speed, params.height);
          break;
        case ACTION_LIFT_RIGHT_REAR:
          controller->dog_.LiftRightRearLeg(params.speed, params.height);
          break;
        case ACTION_HOME:
          controller->dog_.Home();
          break;
        }

        if (params.action_type != ACTION_HOME &&
            params.action_type != ACTION_SIT) {
          controller->dog_.Home();
        }
        controller->is_action_in_progress_ = false;
        vTaskDelay(pdMS_TO_TICKS(20));
      }
    }
  }

  void StartActionTaskIfNeeded() {
    if (action_task_handle_ == nullptr) {
      xTaskCreate(ActionTask, "dog_action", 1024 * 3, this,
                  configMAX_PRIORITIES - 1, &action_task_handle_);
    }
  }

  void QueueAction(int action_type, int steps, int speed, int direction,
                   int height) {
    ESP_LOGI(TAG, "Action control: type=%d, steps=%d, speed=%d, direction=%d, height=%d",
             action_type, steps, speed, direction, height);

    DogActionParams params = {action_type, steps, speed, direction, height};
    xQueueSend(action_queue_, &params, portMAX_DELAY);
    StartActionTaskIfNeeded();
  }

  void LoadTrimsFromNVS() {
    Settings settings("dog_trims", false);

    int left_front_leg = settings.GetInt("left_front_leg", 0);
    int left_rear_leg = settings.GetInt("left_rear_leg", 0);
    int right_front_leg = settings.GetInt("right_front_leg", 0);
    int right_rear_leg = settings.GetInt("right_rear_leg", 0);

    ESP_LOGI(TAG,
             "Loaded trim settings from NVS: left_front_leg=%d, left_rear_leg=%d, right_front_leg=%d, right_rear_leg=%d",
             left_front_leg, left_rear_leg, right_front_leg, right_rear_leg);

    dog_.SetTrims(left_front_leg, left_rear_leg, right_front_leg,
                  right_rear_leg);
  }

public:
  EDARobotDogController() {
    dog_.Init(LEFT_FRONT_LEG_PIN, LEFT_REAR_LEG_PIN, RIGHT_FRONT_LEG_PIN,
              RIGHT_REAR_LEG_PIN);

    ESP_LOGI(TAG, "EDA robot dog initialization complete");

    LoadTrimsFromNVS();

    action_queue_ = xQueueCreate(10, sizeof(DogActionParams));

    QueueAction(ACTION_HOME, 1, 1000, 0, 0);

    RegisterMcpTools();
  }

  void RegisterMcpTools() {
    auto &mcp_server = McpServer::GetInstance();

    ESP_LOGI(TAG, "Registering MCP tools...");

    // Basic locomotion actions
    mcp_server.AddTool(
        "self.dog.walk",
        "Walk. steps: step count (1-100); speed: "
        "walk speed (500-2000, lower is faster); "
        "direction: walk direction (-1=backward, 1=forward)",
        PropertyList({Property("steps", kPropertyTypeInteger, 4, 1, 100),
                      Property("speed", kPropertyTypeInteger, 1000, 500, 2000),
                      Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
        [this](const PropertyList &properties) -> ReturnValue {
          int steps = properties["steps"].value<int>();
          int speed = properties["speed"].value<int>();
          int direction = properties["direction"].value<int>();
          QueueAction(ACTION_WALK, steps, speed, direction, 0);
          return true;
        });

    mcp_server.AddTool(
        "self.dog.turn",
        "Turn. steps: turn steps (1-100); speed: "
        "turn speed (500-2000, lower is faster); "
        "direction: turn direction (1=left, -1=right)",
        PropertyList({Property("steps", kPropertyTypeInteger, 4, 1, 100),
                      Property("speed", kPropertyTypeInteger, 2000, 500, 2000),
                      Property("direction", kPropertyTypeInteger, 1, -1, 1)}),
        [this](const PropertyList &properties) -> ReturnValue {
          int steps = properties["steps"].value<int>();
          int speed = properties["speed"].value<int>();
          int direction = properties["direction"].value<int>();
          QueueAction(ACTION_TURN, steps, speed, direction, 0);
          return true;
        });

    // Posture actions
    mcp_server.AddTool("self.dog.sit",
                       "Sit. speed: sit speed (500-2000, lower is faster)",
                       PropertyList({Property("speed", kPropertyTypeInteger,
                                              1500, 500, 2000)}),
                       [this](const PropertyList &properties) -> ReturnValue {
                         int speed = properties["speed"].value<int>();
                         QueueAction(ACTION_SIT, 1, speed, 0, 0);
                         return true;
                       });

    mcp_server.AddTool("self.dog.stand",
                       "Stand. speed: stand speed (500-2000, lower is faster)",
                       PropertyList({Property("speed", kPropertyTypeInteger,
                                              1500, 500, 2000)}),
                       [this](const PropertyList &properties) -> ReturnValue {
                         int speed = properties["speed"].value<int>();
                         QueueAction(ACTION_STAND, 1, speed, 0, 0);
                         return true;
                       });

    mcp_server.AddTool("self.dog.stretch",
                       "Stretch. speed: stretch speed (500-2000, lower is faster)",
                       PropertyList({Property("speed", kPropertyTypeInteger,
                                              2000, 500, 2000)}),
                       [this](const PropertyList &properties) -> ReturnValue {
                         int speed = properties["speed"].value<int>();
                         QueueAction(ACTION_STRETCH, 1, speed, 0, 0);
                         return true;
                       });

    mcp_server.AddTool("self.dog.shake",
                       "Shake. speed: shake speed (500-2000, lower is faster)",
                       PropertyList({Property("speed", kPropertyTypeInteger,
                                              1000, 500, 2000)}),
                       [this](const PropertyList &properties) -> ReturnValue {
                         int speed = properties["speed"].value<int>();
                         QueueAction(ACTION_SHAKE, 1, speed, 0, 0);
                         return true;
                       });

    // Single-leg lift actions
    mcp_server.AddTool(
        "self.dog.lift_left_front_leg",
        "Lift left front leg. speed: action speed (500-2000, lower is faster); height: "
        "lift height (10-90 degrees)",
        PropertyList({Property("speed", kPropertyTypeInteger, 1000, 500, 2000),
                      Property("height", kPropertyTypeInteger, 45, 10, 90)}),
        [this](const PropertyList &properties) -> ReturnValue {
          int speed = properties["speed"].value<int>();
          int height = properties["height"].value<int>();
          QueueAction(ACTION_LIFT_LEFT_FRONT, 1, speed, 0, height);
          return true;
        });

    mcp_server.AddTool(
        "self.dog.lift_left_rear_leg",
        "Lift left rear leg. speed: action speed (500-2000, lower is faster); height: "
        "lift height (10-90 degrees)",
        PropertyList({Property("speed", kPropertyTypeInteger, 1000, 500, 2000),
                      Property("height", kPropertyTypeInteger, 45, 10, 90)}),
        [this](const PropertyList &properties) -> ReturnValue {
          int speed = properties["speed"].value<int>();
          int height = properties["height"].value<int>();
          QueueAction(ACTION_LIFT_LEFT_REAR, 1, speed, 0, height);
          return true;
        });

    mcp_server.AddTool(
        "self.dog.lift_right_front_leg",
        "Lift right front leg. speed: action speed (500-2000, lower is faster); height: "
        "lift height (10-90 degrees)",
        PropertyList({Property("speed", kPropertyTypeInteger, 1000, 500, 2000),
                      Property("height", kPropertyTypeInteger, 45, 10, 90)}),
        [this](const PropertyList &properties) -> ReturnValue {
          int speed = properties["speed"].value<int>();
          int height = properties["height"].value<int>();
          QueueAction(ACTION_LIFT_RIGHT_FRONT, 1, speed, 0, height);
          return true;
        });

    mcp_server.AddTool(
        "self.dog.lift_right_rear_leg",
        "Lift right rear leg. speed: action speed (500-2000, lower is faster); height: "
        "lift height (10-90 degrees)",
        PropertyList({Property("speed", kPropertyTypeInteger, 1000, 500, 2000),
                      Property("height", kPropertyTypeInteger, 45, 10, 90)}),
        [this](const PropertyList &properties) -> ReturnValue {
          int speed = properties["speed"].value<int>();
          int height = properties["height"].value<int>();
          QueueAction(ACTION_LIFT_RIGHT_REAR, 1, speed, 0, height);
          return true;
        });

    // System tools
    mcp_server.AddTool("self.dog.stop", "Stop immediately", PropertyList(),
                       [this](const PropertyList &properties) -> ReturnValue {
                         if (action_task_handle_ != nullptr) {
                           vTaskDelete(action_task_handle_);
                           action_task_handle_ = nullptr;
                         }
                         is_action_in_progress_ = false;
                         xQueueReset(action_queue_);

                         QueueAction(ACTION_HOME, 1, 1000, 0, 0);
                         return true;
                       });

    mcp_server.AddTool(
        "self.dog.set_trim",
        "Calibrate a single servo. Set trim to adjust the robot dog standing posture, saved"
        "permanently."
        "servo_type: "
        "servo_type (left_front_leg/left_rear_leg/right_front_leg/"
        "right_rear_leg); "
        "trim_value: trim value (-50 to 50 degrees)",
        PropertyList(
            {Property("servo_type", kPropertyTypeString, "left_front_leg"),
             Property("trim_value", kPropertyTypeInteger, 0, -50, 50)}),
        [this](const PropertyList &properties) -> ReturnValue {
          std::string servo_type =
              properties["servo_type"].value<std::string>();
          int trim_value = properties["trim_value"].value<int>();

          ESP_LOGI(TAG, "Set servo trim: %s = %d degrees", servo_type.c_str(),
                   trim_value);

          // Get all current trim values
          Settings settings("dog_trims", true);
          int left_front_leg = settings.GetInt("left_front_leg", 0);
          int left_rear_leg = settings.GetInt("left_rear_leg", 0);
          int right_front_leg = settings.GetInt("right_front_leg", 0);
          int right_rear_leg = settings.GetInt("right_rear_leg", 0);

          // Update trim for the specified servo
          if (servo_type == "left_front_leg") {
            left_front_leg = trim_value;
            settings.SetInt("left_front_leg", left_front_leg);
          } else if (servo_type == "left_rear_leg") {
            left_rear_leg = trim_value;
            settings.SetInt("left_rear_leg", left_rear_leg);
          } else if (servo_type == "right_front_leg") {
            right_front_leg = trim_value;
            settings.SetInt("right_front_leg", right_front_leg);
          } else if (servo_type == "right_rear_leg") {
            right_rear_leg = trim_value;
            settings.SetInt("right_rear_leg", right_rear_leg);
          } else {
            return "Error: invalid servo type. Use: left_front_leg, "
                   "left_rear_leg, right_front_leg, right_rear_leg";
          }

          dog_.SetTrims(left_front_leg, left_rear_leg, right_front_leg,
                        right_rear_leg);

          QueueAction(ACTION_HOME, 1, 500, 0, 0);

          return "Servo " + servo_type + " trim set to " +
                 std::to_string(trim_value) + " degrees, saved permanently";
        });

    mcp_server.AddTool(
        "self.dog.get_trims", "Get current servo trim settings", PropertyList(),
        [this](const PropertyList &properties) -> ReturnValue {
          Settings settings("dog_trims", false);

          int left_front_leg = settings.GetInt("left_front_leg", 0);
          int left_rear_leg = settings.GetInt("left_rear_leg", 0);
          int right_front_leg = settings.GetInt("right_front_leg", 0);
          int right_rear_leg = settings.GetInt("right_rear_leg", 0);

          std::string result =
              "{\"left_front_leg\":" + std::to_string(left_front_leg) +
              ",\"left_rear_leg\":" + std::to_string(left_rear_leg) +
              ",\"right_front_leg\":" + std::to_string(right_front_leg) +
              ",\"right_rear_leg\":" + std::to_string(right_rear_leg) + "}";

          ESP_LOGI(TAG, "Get trim settings: %s", result.c_str());
          return result;
        });

    mcp_server.AddTool("self.dog.get_status",
                       "Get robot dog status; returns moving or idle", PropertyList(),
                       [this](const PropertyList &properties) -> ReturnValue {
                         return is_action_in_progress_ ? "moving" : "idle";
                       });

    ESP_LOGI(TAG, "MCP tool registration complete");
  }

  ~EDARobotDogController() {
    if (action_task_handle_ != nullptr) {
      vTaskDelete(action_task_handle_);
      action_task_handle_ = nullptr;
    }
    vQueueDelete(action_queue_);
  }
};

static EDARobotDogController *g_dog_controller = nullptr;

void InitializeEDARobotDogController() {
  if (g_dog_controller == nullptr) {
    g_dog_controller = new EDARobotDogController();
    ESP_LOGI(TAG, "EDA robot dog controller initialized and MCP tools registered");
  }
}