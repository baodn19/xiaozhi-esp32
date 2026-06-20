#pragma once

#include "mcp_server.h"
#include "board.h"
#include "application.h"
#include "display/lvgl_display/lvgl_display.h"
#include "config.h"
#include "settings.h"

#include <esp_log.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_http_server.h> // Include standard ESP32 light web server layer
#include <string>
#include <vector>
#include <time.h>
#include <mdns.h> // Include ESP-IDF's native mDNS service header

#define MED_TAG "MedReminder"
#define CONFIG_MED_KEY "med_schedule"

/*
Defines a MedicineReminderController
Stores a schedule of Medication entries
Runs a FreeRTOS task that checks the clock and triggers reminders
Displays reminders on the board display
Exposes a local HTTP POST /sync endpoint for schedule updates
Persists schedule JSON in NVS under medicine namespace
Adds MCP tools for clearing active alerts and voice-driven medicine scheduling
*/

struct Medication {
    std::string name;
    int hour;
    int minute;
    int days_bitmask;
    bool active;
};

class MedicineReminderController {
private:
    std::vector<Medication> schedule_;
    std::string active_alert_med_ = "";
    httpd_handle_t server_ = nullptr;

    static void TimeMonitorTask(void* pvParameters) {
        auto* self = static_cast<MedicineReminderController*>(pvParameters);
        struct tm timeinfo;
        int last_minute = -1;

        for (;;) {
            time_t now;
            time(&now);
            localtime_r(&now, &timeinfo);

            if (timeinfo.tm_year > 120) { 
                if (!self->active_alert_med_.empty() && timeinfo.tm_min != last_minute) {
                    self->active_alert_med_ = "";
                }
                if (self->active_alert_med_.empty() && timeinfo.tm_min != last_minute) {
                    int current_day_bit = 1 << timeinfo.tm_wday;
                    for (const auto& med : self->schedule_) {
                        if (med.active && med.hour == timeinfo.tm_hour && med.minute == timeinfo.tm_min && (med.days_bitmask & current_day_bit)) {
                            last_minute = timeinfo.tm_min;
                            self->TriggerAlarm(med.name);
                            break;
                        }
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    void TriggerAlarm(const std::string& med_name) {
        active_alert_med_ = med_name;
        auto* display = Board::GetInstance().GetDisplay();
        if (display) {
            std::string msg = "⏰ MED REMINDER: Time for " + med_name + ".";
            display->SetChatMessage("assistant", msg.c_str());
        }
    }

    static std::string FormatTime(int hour, int minute) {
        std::string time_string = std::to_string(hour) + ":";
        if (minute < 10) {
            time_string += "0";
        }
        time_string += std::to_string(minute);
        return time_string;
    }

    void SaveScheduleToStorage() {
        cJSON* root = cJSON_CreateObject();
        cJSON* meds = cJSON_AddArrayToObject(root, "medications");
        for (const auto& med : schedule_) {
            cJSON* item = cJSON_AddObjectToArray(meds);
            cJSON_AddStringToObject(item, "name", med.name.c_str());
            cJSON_AddNumberToObject(item, "hour", med.hour);
            cJSON_AddNumberToObject(item, "minute", med.minute);
            cJSON_AddNumberToObject(item, "days_bitmask", med.days_bitmask);
            cJSON_AddBoolToObject(item, "active", med.active ? 1 : 0);
        }
        char* json_text = cJSON_PrintUnformatted(root);
        Settings settings("medicine", true);
        settings.SetString(CONFIG_MED_KEY, json_text ? json_text : "");
        if (json_text) {
            cJSON_free(json_text);
        }
        cJSON_Delete(root);
    }

    bool AddMedicationEntry(const std::string& name, int hour, int minute, int days_bitmask = 127, bool active = true) {
        if (name.empty() || hour < 0 || hour > 23 || minute < 0 || minute > 59 || days_bitmask < 0 || days_bitmask > 127) {
            return false;
        }
        Medication med;
        med.name = name;
        med.hour = hour;
        med.minute = minute;
        med.days_bitmask = days_bitmask;
        med.active = active;
        schedule_.push_back(med);
        SaveScheduleToStorage();
        return true;
    }

    void UpdateScheduleInRam(const std::string& json_str) {
        if (json_str.empty()) return;
        cJSON* root = cJSON_Parse(json_str.c_str());
        if (!root) return;
        cJSON* meds = cJSON_GetObjectItem(root, "medications");
        if (cJSON_IsArray(meds)) {
            schedule_.clear();
            int size = cJSON_GetArraySize(meds);
            for (int i = 0; i < size; i++) {
                cJSON* item = cJSON_GetArrayItem(meds, i);
                Medication med;
                cJSON* name = cJSON_GetObjectItem(item, "name");
                cJSON* hour = cJSON_GetObjectItem(item, "hour");
                cJSON* min  = cJSON_GetObjectItem(item, "minute");
                cJSON* mask = cJSON_GetObjectItem(item, "days_bitmask");
                cJSON* active = cJSON_GetObjectItem(item, "active");
                if (name && hour && min && mask) {
                    med.name = name->valuestring;
                    med.hour = hour->valueint;
                    med.minute = min->valueint;
                    med.days_bitmask = mask->valueint;
                    med.active = cJSON_IsBool(active) ? active->valueint == 1 : true;
                    schedule_.push_back(med);
                }
            }
            ESP_LOGI(MED_TAG, "Loaded %d medications into runtime RAM.", size);
        }
        cJSON_Delete(root);
    }

    // Direct HTTP endpoint callback processing function
    static esp_err_t HttpSyncHandler(httpd_req_t *req) {
        size_t total_len = req->content_len;
        if (total_len == 0 || total_len > 4096) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload size");
            return ESP_FAIL;
        }

        std::string body;
        body.reserve(total_len);

        char buf[512];
        size_t received = 0;
        while (received < total_len) {
            int ret = httpd_req_recv(req, buf, std::min(sizeof(buf), total_len - received));
            if (ret <= 0) {
                return ESP_FAIL;
            }
            body.append(buf, ret);
            received += ret;
        }

        auto* self = static_cast<MedicineReminderController*>(req->user_ctx);
        if (!self) {
            return ESP_FAIL;
        }

        self->UpdateScheduleInRam(body);
        Settings settings("medicine", true);
        settings.SetString(CONFIG_MED_KEY, body);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
        return ESP_OK;
    }

    void StartLocalWebServer() {
        // -------------------------------------------------------------
        // INITIALIZE MDNS BROADCASTER
        // -------------------------------------------------------------
        ESP_ERROR_CHECK(mdns_init());
        // Set the device URL name to: xiaozhi.local
        ESP_ERROR_CHECK(mdns_hostname_set("xiaozhi")); 
        ESP_ERROR_CHECK(mdns_instance_name_set("XiaoZhi AI Med Reminder"));

        // Advertise our custom medicine endpoint service over the network
        mdns_service_add(NULL, "_http", "_tcp", 8080, NULL, 0);
        ESP_LOGI(MED_TAG, "mDNS active: Accessible at http://xiaozhi.local:8080/sync");
        // -------------------------------------------------------------

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 8080; 
        if (httpd_start(&server_, &config) == ESP_OK) {
            httpd_uri_t sync_uri = { .uri = "/sync", .method = HTTP_POST, .handler = HttpSyncHandler, .user_ctx = this };
            httpd_register_uri_handler(server_, &sync_uri);
            ESP_LOGI(MED_TAG, "Local endpoint listener ready at port 8080");
        }
    }

public:
    MedicineReminderController() {
        Settings settings("medicine", false);
        std::string saved_json = settings.GetString(CONFIG_MED_KEY, "");
        if (!saved_json.empty()) {
            UpdateScheduleInRam(saved_json);
        } else {
            ESP_LOGI(MED_TAG, "No existing schedule in storage. Loading fallback test medicine.");
            
            // -----------------------------------------------------------------
            // HARDCODED TESTING MEDICINE INJECTION
            // -----------------------------------------------------------------
            Medication test_med;
            test_med.name = "Test Aspirin 50mg";
            test_med.hour = 8;      // Set your target testing hour (0-23 military time)
            test_med.minute = 30;   // Set your target testing minute (0-59)
            test_med.days_bitmask = 127; // 127 means every single day of the week
            test_med.active = true;
            
            schedule_.push_back(test_med);
            // -----------------------------------------------------------------
        }

        McpServer::GetInstance().AddTool(
            "self.medicine.confirm", "Clear active medication alert.", PropertyList({}), 
            [this](const PropertyList&) -> ReturnValue {
                active_alert_med_ = "";
                auto* display = Board::GetInstance().GetDisplay();
                if (display) display->SetChatMessage("assistant", "Alarm cleared.");
                return "Medication logged.";
            }
        );


        // Need to ensure the mcp tool is properly configured on the llm side
        McpServer::GetInstance().AddTool(
            "self.medicine.add",
            "Add a medicine reminder. Useful for voice-driven medicine scheduling. Required properties: name, hour, minute. Optional: days_bitmask (0-127), active.",
            PropertyList({
                Property("name", kPropertyTypeString),
                Property("hour", kPropertyTypeInteger, 0, 0, 23),
                Property("minute", kPropertyTypeInteger, 0, 0, 59),
                Property("days_bitmask", kPropertyTypeInteger, 127, 0, 127),
                Property("active", kPropertyTypeBoolean, true)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                auto name = properties["name"].value<std::string>();
                int hour = properties["hour"].value<int>();
                int minute = properties["minute"].value<int>();
                int days_bitmask = properties["days_bitmask"].value<int>();
                bool active = properties["active"].value<bool>();

                if (!AddMedicationEntry(name, hour, minute, days_bitmask, active)) {
                    throw std::runtime_error("Invalid medication parameters.");
                }

                auto* display = Board::GetInstance().GetDisplay();
                if (display) {
                    std::string msg = "✅ Added medicine reminder for " + name + " at " + FormatTime(hour, minute) + ".";
                    display->SetChatMessage("assistant", msg.c_str());
                }
                return std::string("Added medicine reminder for " + name + " at " + FormatTime(hour, minute));
            }
        );

        StartLocalWebServer();
        xTaskCreatePinnedToCore(TimeMonitorTask, "MedTimeTask", 4096, this, 1, nullptr, 1);
    }
};