#pragma once

#include "mcp_server.h"
#include "board.h"
#include "application.h"
#include "display/lvgl_display/lvgl_display.h"
#include "display/lvgl_display/lvgl_image.h"
#include "lotusai_utils.h"

#include <esp_log.h>
#include <cJSON.h>
#include <mbedtls/base64.h>
#include <esp_heap_caps.h>

#include <memory>
#include <string>
#include <vector>

// nanabot/bin/uvicorn backend.app.main:app --host 0.0.0.0 --port 8006
#ifndef CONFIG_LOTUSAI_BASE_URL
#define CONFIG_LOTUSAI_BASE_URL "https://lotusfoodasmedicine.com"
#endif

#define LOTUSAI_TAG "LotusAI"

class LotusAiController {
private:
    std::vector<std::string> pdf_keys_;
    std::vector<std::string> recipe_titles_;
    std::vector<std::string> recipe_cuisines_;

    static std::string HttpPost(const std::string& url, const std::string& body) {
        auto& board = Board::GetInstance();
        auto http = board.GetNetwork()->CreateHttp(0);
        http->SetHeader("Content-Type", "application/json");
        http->SetContent(std::string(body));
        if (!http->Open("POST", url)) {
            ESP_LOGE(LOTUSAI_TAG, "Failed to open %s", url.c_str());
            return "";
        }
        int status = http->GetStatusCode();
        if (status != 200) {
            ESP_LOGE(LOTUSAI_TAG, "HTTP %d from %s", status, url.c_str());
            http->Close();
            return "";
        }
        std::string resp = http->ReadAll();
        http->Close();
        return resp;
    }

    static uint8_t* DecodeBase64(const std::string& b64, size_t& out_len) {
        size_t required = 0;
        mbedtls_base64_decode(nullptr, 0, &required,
            reinterpret_cast<const unsigned char*>(b64.data()), b64.size());
        if (required == 0) return nullptr;

        uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(required, MALLOC_CAP_8BIT));
        if (!buf) {
            ESP_LOGE(LOTUSAI_TAG, "OOM for QR decode (%u bytes)", (unsigned)required);
            return nullptr;
        }
        size_t written = 0;
        int ret = mbedtls_base64_decode(buf, required, &written,
            reinterpret_cast<const unsigned char*>(b64.data()), b64.size());
        if (ret != 0) {
            ESP_LOGE(LOTUSAI_TAG, "base64 decode error %d", ret);
            heap_caps_free(buf);
            return nullptr;
        }
        out_len = written;
        return buf;
    }

    static void ShowRecipeMenu(const std::string& formatted) {
        auto* display = Board::GetInstance().GetDisplay();
        if (display) display->SetLotusContent(formatted.c_str());
    }

    static void ShowQrCode(const std::string& qr_base64, const std::string& title) {
        auto* display = Board::GetInstance().GetDisplay();
        if (!display) return;

        display->SetLotusContent(("QR: " + title).c_str());

        size_t png_len = 0;
        uint8_t* png_data = DecodeBase64(qr_base64, png_len);
        if (!png_data) return;

        auto* lvgl_disp = dynamic_cast<LvglDisplay*>(display);
        if (!lvgl_disp) {
            heap_caps_free(png_data);
            return;
        }
        try {
            auto image = std::make_unique<LvglAllocatedImage>(png_data, png_len);
            lvgl_disp->SetPreviewImage(std::move(image));
        } catch (const std::exception& e) {
            ESP_LOGE(LOTUSAI_TAG, "QR image error: %s", e.what());
            heap_caps_free(png_data);
        }
    }

    static void DismissQr() {
        auto* display = Board::GetInstance().GetDisplay();
        if (!display) return;
        display->SetLotusContent("");
        auto* lvgl_disp = dynamic_cast<LvglDisplay*>(display);
        if (lvgl_disp) lvgl_disp->SetPreviewImage(nullptr);
    }

    std::string DoRecommend(const PropertyList& props) {
        auto built = LotusAiBuildRecommendRequestBody(props, CONFIG_LOTUSAI_TOP_K);
        if (!built.error.empty()) return built.error;

        std::string url = std::string(CONFIG_LOTUSAI_BASE_URL) + "/api/xiaozhi/recommend";
        std::string resp = HttpPost(url, built.body);
        if (resp.empty()) {
            return "Sorry, I could not reach LotusAI. Please check your connection and try again.";
        }

        cJSON* json = cJSON_Parse(resp.c_str());
        if (!json) {
            ESP_LOGE(LOTUSAI_TAG, "Failed to parse recommend response");
            return "Sorry, I received an unexpected response from LotusAI.";
        }

        std::string spoken_menu;
        auto* spoken = cJSON_GetObjectItem(json, "spoken_menu");
        if (cJSON_IsString(spoken)) spoken_menu = spoken->valuestring;

        pdf_keys_.clear();
        recipe_titles_.clear();
        recipe_cuisines_.clear();

        std::string formatted;
        auto* recipes = cJSON_GetObjectItem(json, "recipes");
        if (cJSON_IsArray(recipes)) {
            int n = cJSON_GetArraySize(recipes);
            for (int i = 0; i < n; i++) {
                auto* item = cJSON_GetArrayItem(recipes, i);
                std::string title, cuisine, pdf_key;
                auto* t = cJSON_GetObjectItem(item, "title");
                auto* c = cJSON_GetObjectItem(item, "cuisine");
                auto* k = cJSON_GetObjectItem(item, "pdf_key");
                if (cJSON_IsString(t)) title   = t->valuestring;
                if (cJSON_IsString(c)) cuisine = c->valuestring;
                if (cJSON_IsString(k)) pdf_key = k->valuestring;

                pdf_keys_.push_back(pdf_key);
                recipe_titles_.push_back(title);
                recipe_cuisines_.push_back(cuisine);

                formatted += std::to_string(i + 1) + ". " + title;
                if (!cuisine.empty()) formatted += " (" + cuisine + ")";
                formatted += "\n";
            }
        }
        cJSON_Delete(json);

        ShowRecipeMenu(formatted);
        return spoken_menu.empty() ? "Here are your recipes." : spoken_menu;
    }

public:
    LotusAiController() {
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool(
            "lotusai.recommend",
            "Search LotusAI for healthy recipes. Extract ingredients the user mentioned, "
            "plus any conditions (e.g. 'type 2 diabetes'), meal type, age group, cuisine, "
            "allergies (allergens, comma-separated, e.g. peanuts,dairy), ingredients the user "
            "wants to avoid (excluded_ingredients, comma-separated, e.g. cilantro,mushrooms) "
            "— distinct from allergens, available cooking equipment (cooking_tools), "
            "and whether the user wants plant-based options only (plant_based). "
            "Call this whenever the user asks for recipe suggestions.",
            PropertyList({
                Property("ingredients",           kPropertyTypeString),
                Property("conditions",            kPropertyTypeString, std::string{}),
                Property("meal",                  kPropertyTypeString, std::string{}),
                Property("age",                   kPropertyTypeString, std::string{}),
                Property("cuisine",               kPropertyTypeString, std::string{}),
                Property("top_k",                 kPropertyTypeInteger, CONFIG_LOTUSAI_TOP_K, 3, 12),
                Property("cooking_tools",         kPropertyTypeString, std::string{}),
                Property("allergens",             kPropertyTypeString, std::string{}),
                Property("excluded_ingredients",    kPropertyTypeString, std::string{}),
                Property("plant_based",           kPropertyTypeBoolean, false),
            }),
            [this](const PropertyList& props) -> ReturnValue {
                return DoRecommend(props);
            }
        );

        mcp.AddTool(
            "lotusai.select",
            "Select one of the recipe options the user chose (by number or name). "
            "Use the index to look up the pdf_key and fetch the QR code.",
            PropertyList({
                Property("option", kPropertyTypeInteger, 1, 12),
            }),
            [this](const PropertyList& props) -> ReturnValue {
                return SelectByIndex(props["option"].value<int>() - 1);
            }
        );

        mcp.AddTool(
            "lotusai.confirm_qr",
            "Call this when the user confirms they have successfully scanned the QR code. "
            "Hides the QR overlay and clears the recipe display.",
            PropertyList(std::vector<Property>{}),
            [](const PropertyList&) -> ReturnValue {
                DismissQr();
                return "The QR code has been dismissed. Enjoy your recipe!";
            }
        );
    }

    std::string SelectByIndex(int idx) {
        int count = static_cast<int>(pdf_keys_.size());
        std::string err = LotusAiSelectIndexErrorMessage(idx, count);
        if (!err.empty()) return err;

        const std::string& pdf_key = pdf_keys_[idx];
        std::string title = (idx < static_cast<int>(recipe_titles_.size()))
                                ? recipe_titles_[idx] : "";

        cJSON* body_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(body_obj, "pdf_key", pdf_key.c_str());
        char* body_str = cJSON_PrintUnformatted(body_obj);
        std::string body(body_str);
        cJSON_free(body_str);
        cJSON_Delete(body_obj);

        std::string url = std::string(CONFIG_LOTUSAI_BASE_URL) + "/api/xiaozhi/select";
        std::string resp = HttpPost(url, body);
        if (resp.empty()) {
            return "Sorry, I could not fetch the QR code. Please check your connection.";
        }

        cJSON* json = cJSON_Parse(resp.c_str());
        if (!json) {
            return "Sorry, I received an unexpected response for the recipe.";
        }

        std::string spoken_confirm;
        auto* confirm_node = cJSON_GetObjectItem(json, "spoken_confirm");
        if (cJSON_IsString(confirm_node)) spoken_confirm = confirm_node->valuestring;

        auto* title_node = cJSON_GetObjectItem(json, "title");
        if (cJSON_IsString(title_node)) title = title_node->valuestring;

        std::string qr_base64;
        auto* qr_node = cJSON_GetObjectItem(json, "qr_base64");
        if (cJSON_IsString(qr_node)) qr_base64 = qr_node->valuestring;

        cJSON_Delete(json);

        if (!qr_base64.empty()) {
            ShowQrCode(qr_base64, title);
        }

        std::string response = spoken_confirm.empty()
            ? ("Here is the QR code for " + title + ".")
            : spoken_confirm;
        return response + " Please let me know when you have finished scanning the QR code.";
    }

    int OptionFromPoint(int /*x*/, int y) const {
        int count = static_cast<int>(pdf_keys_.size());
        auto* display = Board::GetInstance().GetDisplay();
        int display_h = display ? display->height() : 320;
        return LotusAiOptionIndexFromPoint(y, display_h, count);
    }
};
