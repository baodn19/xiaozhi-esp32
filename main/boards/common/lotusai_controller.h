#pragma once

#include "mcp_server.h"
#include "board.h"
#include "application.h"
#include "display/lvgl_display/lvgl_display.h"
#include "display/lvgl_display/lvgl_image.h"

#include <esp_log.h>
#include <cJSON.h>
#include <mbedtls/base64.h>
#include <esp_heap_caps.h>

#include <sstream>
#include <vector>
#include <string>
#include <memory>

#ifndef CONFIG_LOTUSAI_BASE_URL
#define CONFIG_LOTUSAI_BASE_URL "https://lotusfoodasmedicine.com"
#endif

#ifndef CONFIG_LOTUSAI_TOP_K
#define CONFIG_LOTUSAI_TOP_K 3
#endif

#define LOTUSAI_TAG "LotusAI"

// Rough Y offset (pixels) where the content area begins below top/status bars
#define LOTUSAI_CONTENT_Y_OFFSET 80

class LotusAiController {
private:
    std::vector<std::string> pdf_keys_;
    std::vector<std::string> recipe_titles_;
    std::vector<std::string> recipe_cuisines_;

    // -------------------------------------------------------------------------
    // HTTP POST using the esp-ml307 / esp-wifi Http abstraction
    // -------------------------------------------------------------------------
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

    // Split a comma-separated string into trimmed tokens, skipping empty ones
    static std::vector<std::string> SplitCsv(const std::string& csv) {
        std::vector<std::string> result;
        if (csv.empty()) return result;
        std::istringstream stream(csv);
        std::string token;
        while (std::getline(stream, token, ',')) {
            size_t s = token.find_first_not_of(" \t");
            size_t e = token.find_last_not_of(" \t");
            if (s != std::string::npos) {
                result.push_back(token.substr(s, e - s + 1));
            }
        }
        return result;
    }

    // Decode a base64 string into heap_caps_malloc'd memory.
    // Sets out_len on success. Caller must NOT free on failure (nullptr returned).
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

    // Show a text list of recipes in the chat area
    static void ShowRecipeMenu(const std::string& formatted) {
        auto* display = Board::GetInstance().GetDisplay();
        if (display) display->SetChatMessage("assistant", formatted.c_str());
    }

    // Decode qr_base64 PNG and show it with the recipe title
    static void ShowQrCode(const std::string& qr_base64, const std::string& title) {
        auto* display = Board::GetInstance().GetDisplay();
        if (!display) return;

        // Show recipe title while QR loads
        display->SetChatMessage("assistant", ("QR: " + title).c_str());

        size_t png_len = 0;
        uint8_t* png_data = DecodeBase64(qr_base64, png_len);
        if (!png_data) return;

        auto* lvgl_disp = dynamic_cast<LvglDisplay*>(display);
        if (!lvgl_disp) {
            // Non-LVGL display — can't show image
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

    // Build the recommend request body, call the API, cache results, return spoken_menu
    std::string DoRecommend(const PropertyList& props) {
        cJSON* root = cJSON_CreateObject();

        // --- ingredients (required) ---
        auto ingredients = SplitCsv(props["ingredients"].value<std::string>());
        if (ingredients.empty()) {
            cJSON_Delete(root);
            return "No ingredients provided. Please tell me what ingredients you have.";
        }
        cJSON* ing_arr = cJSON_CreateArray();
        for (auto& item : ingredients)
            cJSON_AddItemToArray(ing_arr, cJSON_CreateString(item.c_str()));
        cJSON_AddItemToObject(root, "ingredients", ing_arr);

        // --- conditions (optional, comma-sep) ---
        auto cond_str = props["conditions"].value<std::string>();
        if (!cond_str.empty()) {
            auto conditions = SplitCsv(cond_str);
            cJSON* cond_arr = cJSON_CreateArray();
            for (auto& c : conditions)
                cJSON_AddItemToArray(cond_arr, cJSON_CreateString(c.c_str()));
            cJSON_AddItemToObject(root, "conditions", cond_arr);
        }

        // --- meal, age, cuisine (optional single strings) ---
        auto meal = props["meal"].value<std::string>();
        if (!meal.empty()) cJSON_AddStringToObject(root, "meal", meal.c_str());
        auto age = props["age"].value<std::string>();
        if (!age.empty()) cJSON_AddStringToObject(root, "age", age.c_str());
        auto cuisine = props["cuisine"].value<std::string>();
        if (!cuisine.empty()) cJSON_AddStringToObject(root, "cuisine", cuisine.c_str());

        // --- top_k: use LLM value (or Kconfig default) ---
        int top_k = props["top_k"].value<int>();
        cJSON_AddNumberToObject(root, "top_k", top_k);

        // --- Phase 2: cooking_tools (optional, comma-sep) ---
        auto tools_str = props["cooking_tools"].value<std::string>();
        if (!tools_str.empty()) {
            auto tools = SplitCsv(tools_str);
            cJSON* tools_arr = cJSON_CreateArray();
            for (auto& t : tools)
                cJSON_AddItemToArray(tools_arr, cJSON_CreateString(t.c_str()));
            cJSON_AddItemToObject(root, "cooking_tools", tools_arr);
        }

        // --- Phase 2: allergens (optional, comma-sep) ---
        auto allergens_str = props["allergens"].value<std::string>();
        if (!allergens_str.empty()) {
            auto allergens = SplitCsv(allergens_str);
            cJSON* alrg_arr = cJSON_CreateArray();
            for (auto& a : allergens)
                cJSON_AddItemToArray(alrg_arr, cJSON_CreateString(a.c_str()));
            cJSON_AddItemToObject(root, "allergens", alrg_arr);
        }

        // --- Phase 2: plant_based (optional bool) ---
        bool plant_based = props["plant_based"].value<bool>();
        if (plant_based) cJSON_AddBoolToObject(root, "plant_based", true);

        char* body_str = cJSON_PrintUnformatted(root);
        std::string body(body_str);
        cJSON_free(body_str);
        cJSON_Delete(root);

        std::string url = std::string(CONFIG_LOTUSAI_BASE_URL) + "/api/xiaozhi/recommend";
        std::string resp = HttpPost(url, body);
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

        // Cache recipe data and build display text
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

        // -----------------------------------------------------------------------
        // Tool: lotusai.recommend
        // Phase 2 fields (cooking_tools, allergens, plant_based) are included now;
        // the backend needs to be updated to act on them (see xiaozhi.py Phase 2).
        // -----------------------------------------------------------------------
        mcp.AddTool(
            "lotusai.recommend",
            "Search LotusAI for healthy recipes. Extract ingredients the user mentioned, "
            "plus any conditions (e.g. 'type 2 diabetes'), meal type, age group, cuisine, "
            "allergies (allergens, comma-separated), available cooking equipment (cooking_tools), "
            "and whether the user wants plant-based options only (plant_based). "
            "Call this whenever the user asks for recipe suggestions.",
            PropertyList({
                // v1 required
                Property("ingredients",   kPropertyTypeString),
                // v1 optional
                Property("conditions",    kPropertyTypeString, std::string{}),
                Property("meal",          kPropertyTypeString, std::string{}),
                Property("age",           kPropertyTypeString, std::string{}),
                Property("cuisine",       kPropertyTypeString, std::string{}),
                // top_k: LLM can pass explicit count (3-12); Kconfig default used otherwise
                Property("top_k",         kPropertyTypeInteger, CONFIG_LOTUSAI_TOP_K, 3, 12),
                // Phase 2 fields
                Property("cooking_tools", kPropertyTypeString, std::string{}),
                Property("allergens",     kPropertyTypeString, std::string{}),
                Property("plant_based",   kPropertyTypeBoolean, false),
            }),
            [this](const PropertyList& props) -> ReturnValue {
                return DoRecommend(props);
            }
        );

        // -----------------------------------------------------------------------
        // Tool: lotusai.select
        // -----------------------------------------------------------------------
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
    }

    // -------------------------------------------------------------------------
    // Public: called by touch handler or directly by the MCP select callback
    // idx is 0-based (props["option"] - 1)
    // -------------------------------------------------------------------------
    std::string SelectByIndex(int idx) {
        if (pdf_keys_.empty()) {
            return "No recipes loaded yet. Please search for recipes first.";
        }
        int count = static_cast<int>(pdf_keys_.size());
        if (idx < 0 || idx >= count) {
            return "Invalid selection. Please choose a number between 1 and " +
                   std::to_string(count) + ".";
        }

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

        return spoken_confirm.empty()
            ? ("Here is the QR code for " + title + ".")
            : spoken_confirm;
    }

    // -------------------------------------------------------------------------
    // Map a touch point to a 0-based recipe index.
    // Returns -1 when there are no recipes or the tap is outside the list area.
    // -------------------------------------------------------------------------
    int OptionFromPoint(int /*x*/, int y) const {
        int count = static_cast<int>(pdf_keys_.size());
        if (count == 0) return -1;
        auto* display = Board::GetInstance().GetDisplay();
        int display_h = display ? display->height() : 320;
        int row_h = (display_h - LOTUSAI_CONTENT_Y_OFFSET) / count;
        if (row_h <= 0) return -1;
        int idx = (y - LOTUSAI_CONTENT_Y_OFFSET) / row_h;
        if (idx < 0 || idx >= count) return -1;
        return idx;
    }
};
