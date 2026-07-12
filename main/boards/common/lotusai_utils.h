#pragma once

#include "mcp_server.h"

#include <cJSON.h>

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifndef CONFIG_LOTUSAI_TOP_K
#define CONFIG_LOTUSAI_TOP_K 3
#endif

#define LOTUSAI_CONTENT_Y_OFFSET 80 // For Wifi icon and status label

struct LotusAiRecommendBodyResult {
    std::string body;
    std::string error;
};

inline std::vector<std::string> LotusAiSplitCsv(const std::string& csv) {
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

inline void LotusAiAddStringArray(cJSON* root, const char* key,
                                  const std::vector<std::string>& items) {
    cJSON* arr = cJSON_CreateArray();
    for (const auto& item : items)
        cJSON_AddItemToArray(arr, cJSON_CreateString(item.c_str()));
    cJSON_AddItemToObject(root, key, arr);
}

inline LotusAiRecommendBodyResult LotusAiBuildRecommendRequestBody(
        const PropertyList& props, int default_top_k = CONFIG_LOTUSAI_TOP_K) {
    LotusAiRecommendBodyResult out;
    cJSON* root = cJSON_CreateObject();

    auto ingredients = LotusAiSplitCsv(props["ingredients"].value<std::string>());
    if (ingredients.empty()) {
        cJSON_Delete(root);
        out.error = "No ingredients provided. Please tell me what ingredients you have.";
        return out;
    }
    LotusAiAddStringArray(root, "ingredients", ingredients);

    auto cond_str = props["conditions"].value<std::string>();
    if (!cond_str.empty())
        LotusAiAddStringArray(root, "conditions", LotusAiSplitCsv(cond_str));

    auto meal = props["meal"].value<std::string>();
    if (!meal.empty()) cJSON_AddStringToObject(root, "meal", meal.c_str());
    auto age = props["age"].value<std::string>();
    if (!age.empty()) cJSON_AddStringToObject(root, "age", age.c_str());
    auto cuisine = props["cuisine"].value<std::string>();
    if (!cuisine.empty()) cJSON_AddStringToObject(root, "cuisine", cuisine.c_str());

    int top_k = props["top_k"].value<int>();
    cJSON_AddNumberToObject(root, "top_k", top_k);

    auto tools_str = props["cooking_tools"].value<std::string>();
    if (!tools_str.empty())
        LotusAiAddStringArray(root, "cooking_tools", LotusAiSplitCsv(tools_str));

    auto allergens_str = props["allergens"].value<std::string>();
    if (!allergens_str.empty())
        LotusAiAddStringArray(root, "allergens", LotusAiSplitCsv(allergens_str));

    auto excluded_str = props["excluded_ingredients"].value<std::string>();
    if (!excluded_str.empty())
        LotusAiAddStringArray(root, "excluded_ingredients", LotusAiSplitCsv(excluded_str));

    bool plant_based = props["plant_based"].value<bool>();
    if (plant_based) cJSON_AddBoolToObject(root, "plant_based", true);

    char* body_str = cJSON_PrintUnformatted(root);
    out.body = std::string(body_str);
    cJSON_free(body_str);
    cJSON_Delete(root);
    return out;
}

inline int LotusAiOptionIndexFromPoint(int y, int display_height, int recipe_count,
                                       int content_y_offset = LOTUSAI_CONTENT_Y_OFFSET) {
    if (recipe_count == 0) return -1;
    if (y < content_y_offset) return -1;
    int row_h = (display_height - content_y_offset) / recipe_count;
    if (row_h <= 0) return -1;
    int idx = (y - content_y_offset) / row_h;
    if (idx < 0 || idx >= recipe_count) return -1;
    return idx;
}

inline bool LotusAiIsValidRecipeIndex(int idx, int count) {
    return count > 0 && idx >= 0 && idx < count;
}

inline std::string LotusAiSelectIndexErrorMessage(int idx, int count) {
    if (count == 0)
        return "No recipes loaded yet. Please search for recipes first.";
    if (!LotusAiIsValidRecipeIndex(idx, count))
        return "Invalid selection. Please choose a number between 1 and " +
               std::to_string(count) + ".";
    return {};
}
