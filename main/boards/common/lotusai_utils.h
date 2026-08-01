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
#define LOTUSAI_ROW_PAD_Y 2 // Must match lv_obj_set_style_pad_row on lotusai_rows_container_

struct LotusAiRecommendBodyResult {
    std::string body;
    std::string error;
};

using LotusAiStringArray = std::vector<std::string>; // Alias for vector of strings

/*
    Function: LotusAiSplitCsv
    Description: Split a CSV string into a vector of strings
    Parameters:
        csv: The CSV string to split
    Returns:
        A LotusAiStringArray (std::vector<std::string>)
*/
inline LotusAiStringArray LotusAiSplitCsv(const std::string& csv) {
    LotusAiStringArray result;
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
                                  const LotusAiStringArray& items) {
    cJSON* arr = cJSON_CreateArray();
    for (const std::string& item : items)
        cJSON_AddItemToArray(arr, cJSON_CreateString(item.c_str()));
    cJSON_AddItemToObject(root, key, arr);
}

inline LotusAiRecommendBodyResult LotusAiBuildRecommendRequestBody(
        const PropertyList& props, int default_top_k = CONFIG_LOTUSAI_TOP_K) {
    LotusAiRecommendBodyResult out;
    cJSON* root = cJSON_CreateObject();

    LotusAiStringArray ingredients = LotusAiSplitCsv(props["ingredients"].value<std::string>()); // required
    if (ingredients.empty()) {
        cJSON_Delete(root);
        out.error = "No ingredients provided. Please tell me what ingredients you have.";
        return out;
    }
    LotusAiAddStringArray(root, "ingredients", ingredients);

    std::string cond_str = props["conditions"].value<std::string>(); // optional
    if (!cond_str.empty())
        LotusAiAddStringArray(root, "conditions", LotusAiSplitCsv(cond_str));

    std::string meal = props["meal"].value<std::string>();
    if (!meal.empty()) cJSON_AddStringToObject(root, "meal", meal.c_str());
    std::string age = props["age"].value<std::string>();
    if (!age.empty()) cJSON_AddStringToObject(root, "age", age.c_str());
    std::string cuisine = props["cuisine"].value<std::string>();
    if (!cuisine.empty()) cJSON_AddStringToObject(root, "cuisine", cuisine.c_str());

    int top_k = props["top_k"].value<int>();
    cJSON_AddNumberToObject(root, "top_k", top_k);

    std::string tools_str = props["cooking_tools"].value<std::string>();
    if (!tools_str.empty())
        LotusAiAddStringArray(root, "cooking_tools", LotusAiSplitCsv(tools_str));

    std::string allergens_str = props["allergens"].value<std::string>();
    if (!allergens_str.empty())
        LotusAiAddStringArray(root, "allergens", LotusAiSplitCsv(allergens_str));

    std::string excluded_str = props["excluded_ingredients"].value<std::string>();
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

inline int LotusAiRecipeRowHeight(int line_height, int vpad = 8, int border = 1) {
    return 2 * line_height + vpad + border;
}

struct LotusAiHitTestGeometry {
    int row_h;
    int scroll_y = 0;
    int pad_row = LOTUSAI_ROW_PAD_Y;
    int content_y_offset = LOTUSAI_CONTENT_Y_OFFSET;
    int display_height;
};

inline int LotusAiOptionIndexFromPoint(int y, int recipe_count,
                                       const LotusAiHitTestGeometry& geometry) {
    if (recipe_count <= 0 || geometry.row_h <= 0) return -1;
    if (y < geometry.content_y_offset || y >= geometry.display_height) return -1;
    const int local_y = (y - geometry.content_y_offset) + geometry.scroll_y;
    if (local_y < 0) return -1;
    const int stride = geometry.row_h + geometry.pad_row;
    const int idx = local_y / stride;
    if (idx < 0 || idx >= recipe_count) return -1;
    if ((local_y % stride) >= geometry.row_h) return -1;  // pad_row gap
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
