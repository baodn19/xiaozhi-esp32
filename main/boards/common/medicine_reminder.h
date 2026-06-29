#pragma once

#include "mcp_server.h"
#include "board.h"
#include "application.h"
#include "display/lvgl_display/lvgl_display.h"
#include "settings.h"

#include <esp_log.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_http_server.h>
#include <string>
#include <vector>
#include <time.h>
#include <algorithm>

#define MED_TAG "MedReminder"
#define CONFIG_MED_KEY "med_schedule"

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
    std::string active_alert_med_;
    httpd_handle_t server_ = nullptr;
    SemaphoreHandle_t schedule_mutex_ = nullptr;

    static void TimeMonitorTask(void* pvParameters) {
        auto* self = static_cast<MedicineReminderController*>(pvParameters);
        struct tm timeinfo;
        int last_minute = -1;

        while (true) {
            time_t now;
            time(&now);
            localtime_r(&now, &timeinfo);

            if (timeinfo.tm_year > (2020 - 1900) &&
                timeinfo.tm_min != last_minute) {

                last_minute = timeinfo.tm_min;

                xSemaphoreTake(self->schedule_mutex_, portMAX_DELAY);

                if (!self->active_alert_med_.empty()) {
                    self->active_alert_med_.clear();
                }

                int current_day_bit = 1 << timeinfo.tm_wday;

                for (const auto& med : self->schedule_) {
                    if (med.active &&
                        med.hour == timeinfo.tm_hour &&
                        med.minute == timeinfo.tm_min &&
                        (med.days_bitmask & current_day_bit)) {

                        std::string med_name = med.name;
                        xSemaphoreGive(self->schedule_mutex_);
                        self->TriggerAlarm(med_name);
                        goto done;
                    }
                }

                xSemaphoreGive(self->schedule_mutex_);
            }

done:
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    void TriggerAlarm(const std::string& med_name) {
        xSemaphoreTake(schedule_mutex_, portMAX_DELAY);
        active_alert_med_ = med_name;
        xSemaphoreGive(schedule_mutex_);

        auto* display = Board::GetInstance().GetDisplay();
        if (display) {
            std::string msg = "⏰ MED REMINDER: Time for " + med_name + ".";
            display->SetChatMessage("assistant", msg.c_str());
        }
    }

    static std::string FormatTime(int hour, int minute) {
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
        return std::string(buf);
    }

    void SaveScheduleToStorage() {
        xSemaphoreTake(schedule_mutex_, portMAX_DELAY);

        cJSON* root = cJSON_CreateObject();
        cJSON* meds = cJSON_AddArrayToObject(root, "medications");

        for (const auto& med : schedule_) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddItemToArray(meds, item);
            cJSON_AddStringToObject(item, "name", med.name.c_str());
            cJSON_AddNumberToObject(item, "hour", med.hour);
            cJSON_AddNumberToObject(item, "minute", med.minute);
            cJSON_AddNumberToObject(item, "days_bitmask", med.days_bitmask);
            cJSON_AddBoolToObject(item, "active", med.active);
        }

        xSemaphoreGive(schedule_mutex_);

        char* json_text = cJSON_PrintUnformatted(root);

        Settings settings("medicine", true);
        settings.SetString(CONFIG_MED_KEY, json_text ? json_text : "");

        if (json_text) cJSON_free(json_text);
        cJSON_Delete(root);
    }

    bool AddMedicationEntry(const std::string& name, int hour, int minute,
                            int days_bitmask = 127, bool active = true) {
        if (name.empty() || hour < 0 || hour > 23 ||
            minute < 0 || minute > 59 ||
            days_bitmask < 0 || days_bitmask > 127) {
            return false;
        }

        Medication med{name, hour, minute, days_bitmask, active};

        xSemaphoreTake(schedule_mutex_, portMAX_DELAY);
        schedule_.push_back(med);
        xSemaphoreGive(schedule_mutex_);

        SaveScheduleToStorage();
        return true;
    }

    void UpdateScheduleInRam(const std::string& json_str) {
        if (json_str.empty()) return;

        cJSON* root = cJSON_Parse(json_str.c_str());
        if (!root) return;

        cJSON* meds = cJSON_GetObjectItem(root, "medications");

        if (cJSON_IsArray(meds)) {
            xSemaphoreTake(schedule_mutex_, portMAX_DELAY);
            schedule_.clear();

            int size = cJSON_GetArraySize(meds);

            for (int i = 0; i < size; i++) {
                cJSON* item = cJSON_GetArrayItem(meds, i);

                cJSON* name = cJSON_GetObjectItem(item, "name");
                cJSON* hour = cJSON_GetObjectItem(item, "hour");
                cJSON* minute = cJSON_GetObjectItem(item, "minute");
                cJSON* mask = cJSON_GetObjectItem(item, "days_bitmask");
                cJSON* active = cJSON_GetObjectItem(item, "active");

                if (cJSON_IsString(name) &&
                    cJSON_IsNumber(hour) &&
                    cJSON_IsNumber(minute) &&
                    cJSON_IsNumber(mask)) {

                    Medication med;
                    med.name = name->valuestring;
                    med.hour = hour->valueint;
                    med.minute = minute->valueint;
                    med.days_bitmask = mask->valueint;
                    med.active = active ? cJSON_IsTrue(active) : true;

                    schedule_.push_back(med);
                }
            }

            xSemaphoreGive(schedule_mutex_);
            ESP_LOGI(MED_TAG, "Loaded %d medications.", size);
        }

        cJSON_Delete(root);
    }

    static esp_err_t HttpSyncHandler(httpd_req_t* req) {
        size_t total_len = req->content_len;

        if (total_len == 0 || total_len > 4096) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload");
            return ESP_FAIL;
        }

        std::string body;
        body.reserve(total_len);

        char buf[512];
        size_t received = 0;

        while (received < total_len) {
            size_t chunk = std::min((size_t)sizeof(buf),
                                    total_len - received);

            int ret = httpd_req_recv(req, buf, chunk);

            if (ret <= 0) return ESP_FAIL;

            body.append(buf, ret);
            received += ret;
        }

        auto* self =
            static_cast<MedicineReminderController*>(req->user_ctx);

        if (!self) return ESP_FAIL;

        self->UpdateScheduleInRam(body);

        Settings settings("medicine", true);
        settings.SetString(CONFIG_MED_KEY, body);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");

        return ESP_OK;
    }

    void StartLocalWebServer() {
        // Uncomment if you want local syncing enabled
        /*
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 8080;

        if (httpd_start(&server_, &config) == ESP_OK) {
            httpd_uri_t sync_uri = {};
            sync_uri.uri = "/sync";
            sync_uri.method = HTTP_POST;
            sync_uri.handler = HttpSyncHandler;
            sync_uri.user_ctx = this;

            httpd_register_uri_handler(server_, &sync_uri);

            ESP_LOGI(MED_TAG,
                     "HTTP sync endpoint started on port 8080");
        }
        */
    }

public:
    MedicineReminderController() {
        schedule_mutex_ = xSemaphoreCreateMutex();

        Settings settings("medicine", false);
        std::string saved_json =
            settings.GetString(CONFIG_MED_KEY, "");

        if (!saved_json.empty()) {
            UpdateScheduleInRam(saved_json);
        } else {
            Medication test_med{
                "Test Aspirin 50mg",
                8,
                30,
                127,
                true
            };

            schedule_.push_back(test_med);
            SaveScheduleToStorage();
        }

        McpServer::GetInstance().AddTool(
            "self.medicine.confirm",
            "Clear active medication alert.",
            PropertyList(std::vector<Property>()),
            [this](const PropertyList&) -> ReturnValue {
                xSemaphoreTake(schedule_mutex_, portMAX_DELAY);
                active_alert_med_.clear();
                xSemaphoreGive(schedule_mutex_);

                auto* display = Board::GetInstance().GetDisplay();
                if (display)
                    display->SetChatMessage("assistant",
                                            "Alarm cleared.");

                return "Medication logged.";
            });

        McpServer::GetInstance().AddTool(
            "self.medicine.add",
            "Add a medicine reminder.",
            PropertyList(std::vector<Property>{
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
                int days_bitmask =
                    properties["days_bitmask"].value<int>();
                bool active =
                    properties["active"].value<bool>();

                if (!AddMedicationEntry(
                        name, hour, minute,
                        days_bitmask, active)) {
                    throw std::runtime_error(
                        "Invalid medication parameters.");
                }

                auto* display =
                    Board::GetInstance().GetDisplay();

                if (display) {
                    std::string msg =
                        "✅ Added medicine reminder for " +
                        name + " at " +
                        FormatTime(hour, minute) + ".";
                    display->SetChatMessage("assistant",
                                            msg.c_str());
                }

                return std::string(
                    "Added medicine reminder for " +
                    name + " at " +
                    FormatTime(hour, minute));
            });

        StartLocalWebServer();

        xTaskCreatePinnedToCore(
            TimeMonitorTask,
            "MedTimeTask",
            4096,
            this,
            1,
            nullptr,
            1);
    }

    ~MedicineReminderController() {
        if (server_) httpd_stop(server_);
        if (schedule_mutex_) vSemaphoreDelete(schedule_mutex_);
    }
};
