#pragma once

#include "mcp_server.h"
#include "board.h"
#include "application.h"
#include "display/lvgl_display/lvgl_display.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
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
    SemaphoreHandle_t schedule_mutex_ = nullptr;

    // ── Helpers ───────────────────────────────────────────────────────────────

    static std::string FormatTime(int hour, int minute) {
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
        return std::string(buf);
    }

    // ── Alarm sound task — plays for 20s then self-deletes ───────────────────

    static void AlarmSoundTask(void* pvParameters) {
        auto& app = Application::GetInstance();
        TickType_t start_time = xTaskGetTickCount();
        const TickType_t duration = pdMS_TO_TICKS(20000);

        while ((xTaskGetTickCount() - start_time) < duration) {
            app.PlaySound(Lang::Sounds::OGG_SUCCESS);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        vTaskDelete(NULL);
    }

    // ── Time monitor — checks every 5s, nags every 60s up to 50 times ────────

    static void TimeMonitorTask(void* pvParameters) {
        auto* self = static_cast<MedicineReminderController*>(pvParameters);
        struct tm timeinfo;
        int last_minute = -1;

        TickType_t last_nag_tick = 0;
        const TickType_t nag_interval = pdMS_TO_TICKS(60000);
        bool is_alerting = false;
        int nag_count = 0;

        while (true) {
            time_t now;
            time(&now);
            localtime_r(&now, &timeinfo);

            // Only act on valid time (post-2020) and on minute boundary
            if (timeinfo.tm_year > (2020 - 1900) && timeinfo.tm_min != last_minute) {
                last_minute = timeinfo.tm_min;

                xSemaphoreTake(self->schedule_mutex_, portMAX_DELAY);
                int current_day_bit = 1 << timeinfo.tm_wday;

                for (const auto& med : self->schedule_) {
                    if (med.active &&
                        med.hour == timeinfo.tm_hour &&
                        med.minute == timeinfo.tm_min &&
                        (med.days_bitmask & current_day_bit)) {

                        std::string med_name = med.name;
                        self->active_alert_med_ = med_name;
                        xSemaphoreGive(self->schedule_mutex_);

                        self->TriggerAlarm(med_name);
                        last_nag_tick = xTaskGetTickCount();
                        is_alerting = true;
                        nag_count = 1;
                        goto loop_delay;
                    }
                }
                xSemaphoreGive(self->schedule_mutex_);
            }

            // Nagging logic for unconfirmed alerts
            xSemaphoreTake(self->schedule_mutex_, portMAX_DELAY);
            if (!self->active_alert_med_.empty()) {
                if (!is_alerting) {
                    last_nag_tick = xTaskGetTickCount();
                    is_alerting = true;
                    nag_count = 1;
                }

                TickType_t current_tick = xTaskGetTickCount();
                if ((current_tick - last_nag_tick) >= nag_interval) {
                    if (nag_count >= 50) {
                        ESP_LOGW(MED_TAG, "Max nag count reached. Stopping alarm.");
                        self->active_alert_med_.clear();
                        is_alerting = false;
                        nag_count = 0;
                        xSemaphoreGive(self->schedule_mutex_);
                        goto loop_delay;
                    }

                    std::string pending_med = self->active_alert_med_;
                    xSemaphoreGive(self->schedule_mutex_);

                    nag_count++;
                    ESP_LOGI(MED_TAG, "Nagging (#%d/50): %s", nag_count, pending_med.c_str());
                    self->TriggerAlarm(pending_med);
                    last_nag_tick = current_tick;
                    goto loop_delay;
                }
            } else {
                is_alerting = false;
                nag_count = 0;
            }
            xSemaphoreGive(self->schedule_mutex_);

        loop_delay:
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    // ── Trigger a single alarm ────────────────────────────────────────────────

    void TriggerAlarm(const std::string& med_name) {
        xSemaphoreTake(schedule_mutex_, portMAX_DELAY);
        active_alert_med_ = med_name;
        xSemaphoreGive(schedule_mutex_);

        auto& app = Application::GetInstance();

        if (app.GetDeviceState() == kDeviceStateSpeaking ||
            app.GetDeviceState() == kDeviceStateListening) {
            app.AbortSpeaking(kAbortReasonWakeWordDetected);
        }

        xTaskCreate(AlarmSoundTask, "AlarmSoundTask", 2048, NULL, 5, NULL);

        std::string reminder = "Time to take " + med_name;

        auto* display = Board::GetInstance().GetDisplay();
        if (display) {
            display->SetChatMessage("assistant", ("⏰ " + reminder).c_str());
        }

        // Build MCP notification using cJSON — safe, no string injection
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "event",    "medicine_reminder");
        cJSON_AddStringToObject(root, "medicine", med_name.c_str());
        cJSON_AddStringToObject(root, "message",  reminder.c_str());
        char* payload_str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (payload_str) {
            app.SendMcpMessage(std::string(payload_str));
            cJSON_free(payload_str);
            ESP_LOGI(MED_TAG, "Sent medicine_reminder MCP event for: %s", med_name.c_str());
        }
    }

    // ── NVS persistence ───────────────────────────────────────────────────────

    void SaveScheduleToStorage() {
        xSemaphoreTake(schedule_mutex_, portMAX_DELAY);

        cJSON* root = cJSON_CreateObject();
        cJSON* meds = cJSON_AddArrayToObject(root, "medications");

        for (const auto& med : schedule_) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name",         med.name.c_str());
            cJSON_AddNumberToObject(item, "hour",         med.hour);
            cJSON_AddNumberToObject(item, "minute",       med.minute);
            cJSON_AddNumberToObject(item, "days_bitmask", med.days_bitmask);
            cJSON_AddBoolToObject(item,   "active",       med.active);
            cJSON_AddItemToArray(meds, item);
        }

        xSemaphoreGive(schedule_mutex_);

        char* json_text = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (json_text) {
            Settings settings("medicine", true);
            settings.SetString(CONFIG_MED_KEY, json_text);
            cJSON_free(json_text);
        }
    }

    void LoadScheduleFromStorage() {
        Settings settings("medicine", false);
        std::string saved = settings.GetString(CONFIG_MED_KEY, "");

        if (!saved.empty()) {
            UpdateScheduleInRam(saved);
        }
        // Intentionally no default medicine — start empty
    }

    void UpdateScheduleInRam(const std::string& json_str) {
        if (json_str.empty()) return;

        cJSON* root = cJSON_Parse(json_str.c_str());
        if (!root) {
            ESP_LOGE(MED_TAG, "Failed to parse medicine JSON from storage");
            return;
        }

        cJSON* meds = cJSON_GetObjectItem(root, "medications");
        if (!cJSON_IsArray(meds)) {
            cJSON_Delete(root);
            return;
        }

        xSemaphoreTake(schedule_mutex_, portMAX_DELAY);
        schedule_.clear();

        int size = cJSON_GetArraySize(meds);
        for (int i = 0; i < size; i++) {
            cJSON* item   = cJSON_GetArrayItem(meds, i);
            cJSON* name   = cJSON_GetObjectItem(item, "name");
            cJSON* hour   = cJSON_GetObjectItem(item, "hour");
            cJSON* minute = cJSON_GetObjectItem(item, "minute");
            cJSON* mask   = cJSON_GetObjectItem(item, "days_bitmask");
            cJSON* active = cJSON_GetObjectItem(item, "active");

            if (cJSON_IsString(name) &&
                cJSON_IsNumber(hour) &&
                cJSON_IsNumber(minute) &&
                cJSON_IsNumber(mask)) {

                Medication med;
                med.name         = name->valuestring;
                med.hour         = hour->valueint;
                med.minute       = minute->valueint;
                med.days_bitmask = mask->valueint;
                med.active       = active ? cJSON_IsTrue(active) : true;
                schedule_.push_back(med);
            }
        }

        xSemaphoreGive(schedule_mutex_);
        cJSON_Delete(root);
        ESP_LOGI(MED_TAG, "Loaded %d medication(s) from storage", size);
    }

    bool AddMedicationEntry(const std::string& name, int hour, int minute,
                            int days_bitmask = 127, bool active = true) {
        if (name.empty() || hour < 0 || hour > 23 ||
            minute < 0 || minute > 59 ||
            days_bitmask < 0 || days_bitmask > 127) {
            return false;
        }

        xSemaphoreTake(schedule_mutex_, portMAX_DELAY);
        // Reject duplicate names
        for (const auto& med : schedule_) {
            if (med.name == name) {
                xSemaphoreGive(schedule_mutex_);
                ESP_LOGW(MED_TAG, "Rejected duplicate medicine: %s", name.c_str());
                return false;
            }
        }
        schedule_.push_back({name, hour, minute, days_bitmask, active});
        xSemaphoreGive(schedule_mutex_);

        SaveScheduleToStorage();
        return true;
    }

    // ── MCP tool registration ─────────────────────────────────────────────────

    void RegisterMcpTools() {
        auto& mcp = McpServer::GetInstance();

        // Confirm active alert
        mcp.AddTool(
            "self.medicine.confirm",
            "Clear the active medication alert after the user has taken their medicine.",
            PropertyList(std::vector<Property>()),
            [this](const PropertyList&) -> ReturnValue {
                xSemaphoreTake(schedule_mutex_, portMAX_DELAY);
                active_alert_med_.clear();
                xSemaphoreGive(schedule_mutex_);

                auto* display = Board::GetInstance().GetDisplay();
                if (display) display->SetChatMessage("assistant", "✅ Alarm cleared.");

                return "Medication confirmed and alarm cleared.";
            });

        // Add a medicine
        mcp.AddTool(
            "self.medicine.add",
            "Add a new medicine reminder. days_bitmask uses bit-per-weekday (Sun=1,Mon=2,...,Sat=64). 127 = every day.",
            PropertyList(std::vector<Property>{
                Property("name",         kPropertyTypeString),
                Property("hour",         kPropertyTypeInteger, 0,   0, 23),
                Property("minute",       kPropertyTypeInteger, 0,   0, 59),
                Property("days_bitmask", kPropertyTypeInteger, 127, 0, 127),
                Property("active",       kPropertyTypeBoolean, true)
            }),
            [this](const PropertyList& props) -> ReturnValue {
                auto name         = props["name"].value<std::string>();
                int  hour         = props["hour"].value<int>();
                int  minute       = props["minute"].value<int>();
                int  days_bitmask = props["days_bitmask"].value<int>();
                bool active       = props["active"].value<bool>();

                if (!AddMedicationEntry(name, hour, minute, days_bitmask, active)) {
                    throw std::runtime_error(
                        "Invalid parameters or duplicate medicine name: " + name);
                }

                auto* display = Board::GetInstance().GetDisplay();
                if (display) {
                    std::string msg = "✅ Added: " + name + " at " + FormatTime(hour, minute);
                    display->SetChatMessage("assistant", msg.c_str());
                }

                return "Added medicine reminder for " + name +
                       " at " + FormatTime(hour, minute) + ".";
            });

        // Delete one entry by name
        mcp.AddTool(
            "self.medicine.delete_entry",
            "Remove a single medication from the schedule by its exact name.",
            PropertyList(std::vector<Property>{
                Property("name", kPropertyTypeString)
            }),
            [this](const PropertyList& props) -> ReturnValue {
                auto target = props["name"].value<std::string>();

                xSemaphoreTake(schedule_mutex_, portMAX_DELAY);
                auto before = schedule_.size();
                schedule_.erase(
                    std::remove_if(schedule_.begin(), schedule_.end(),
                        [&target](const Medication& m){ return m.name == target; }),
                    schedule_.end());
                bool removed = (schedule_.size() < before);

                if (active_alert_med_ == target) active_alert_med_.clear();
                xSemaphoreGive(schedule_mutex_);

                if (!removed) {
                    return "No scheduled medication found with the name: " + target;
                }

                SaveScheduleToStorage();

                auto* display = Board::GetInstance().GetDisplay();
                if (display) {
                    display->SetChatMessage("assistant",
                        ("🗑️ Removed: " + target).c_str());
                }

                return "Removed " + target + " from the schedule.";
            });

        // Clear entire schedule
        mcp.AddTool(
            "self.medicine.clear_all",
            "Wipe the entire medication schedule from memory and storage.",
            PropertyList(std::vector<Property>()),
            [this](const PropertyList&) -> ReturnValue {
                xSemaphoreTake(schedule_mutex_, portMAX_DELAY);
                schedule_.clear();
                active_alert_med_.clear();
                xSemaphoreGive(schedule_mutex_);

                SaveScheduleToStorage();

                auto* display = Board::GetInstance().GetDisplay();
                if (display) display->SetChatMessage("assistant", "All schedules cleared.");

                return "Medication schedule cleared.";
            });

        // List all medications — returns JSON array for server-side parsing
        mcp.AddTool(
            "self.medicine.list",
            "List all currently scheduled medications. Returns a JSON array.",
            PropertyList(std::vector<Property>()),
            [this](const PropertyList&) -> ReturnValue {
                xSemaphoreTake(schedule_mutex_, portMAX_DELAY);

                if (schedule_.empty()) {
                    xSemaphoreGive(schedule_mutex_);
                    return std::string("[]");
                }

                cJSON* root = cJSON_CreateArray();
                for (const auto& med : schedule_) {
                    cJSON* item = cJSON_CreateObject();
                    cJSON_AddStringToObject(item, "name",         med.name.c_str());
                    cJSON_AddNumberToObject(item, "hour",         med.hour);
                    cJSON_AddNumberToObject(item, "minute",       med.minute);
                    cJSON_AddNumberToObject(item, "days_bitmask", med.days_bitmask);
                    cJSON_AddBoolToObject(item,   "active",       med.active);
                    cJSON_AddItemToArray(root, item);
                }
                xSemaphoreGive(schedule_mutex_);

                char* out = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);

                if (!out) return std::string("[]");
                std::string result(out);
                cJSON_free(out);
                return result;
            });
    }

public:
    MedicineReminderController() {
        schedule_mutex_ = xSemaphoreCreateMutex();
        LoadScheduleFromStorage();
        RegisterMcpTools();

        xTaskCreatePinnedToCore(
            TimeMonitorTask, "MedTimeTask",
            4096, this, 1, nullptr, 1);
    }

    ~MedicineReminderController() {
        if (schedule_mutex_) vSemaphoreDelete(schedule_mutex_);
    }
};