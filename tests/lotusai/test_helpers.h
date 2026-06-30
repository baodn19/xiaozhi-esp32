#pragma once

#include "host_property_list.h"

#include <string>

inline PropertyList MakeRecommendProps(
        const std::string& ingredients,
        const std::string& conditions = "",
        const std::string& meal = "",
        const std::string& age = "",
        const std::string& cuisine = "",
        int top_k = 3,
        const std::string& cooking_tools = "",
        const std::string& allergens = "",
        const std::string& excluded_ingredients = "",
        bool plant_based = false) {
    Property p_ing("ingredients", kPropertyTypeString);
    p_ing.set_value<std::string>(ingredients);

    Property p_cond("conditions", kPropertyTypeString, std::string{});
    if (!conditions.empty()) p_cond.set_value<std::string>(conditions);

    Property p_meal("meal", kPropertyTypeString, std::string{});
    if (!meal.empty()) p_meal.set_value<std::string>(meal);

    Property p_age("age", kPropertyTypeString, std::string{});
    if (!age.empty()) p_age.set_value<std::string>(age);

    Property p_cuisine("cuisine", kPropertyTypeString, std::string{});
    if (!cuisine.empty()) p_cuisine.set_value<std::string>(cuisine);

    Property p_top_k("top_k", kPropertyTypeInteger, top_k, 3, 12);

    Property p_tools("cooking_tools", kPropertyTypeString, std::string{});
    if (!cooking_tools.empty()) p_tools.set_value<std::string>(cooking_tools);

    Property p_allergens("allergens", kPropertyTypeString, std::string{});
    if (!allergens.empty()) p_allergens.set_value<std::string>(allergens);

    Property p_excluded("excluded_ingredients", kPropertyTypeString, std::string{});
    if (!excluded_ingredients.empty())
        p_excluded.set_value<std::string>(excluded_ingredients);

    Property p_plant("plant_based", kPropertyTypeBoolean, false);
    if (plant_based) p_plant.set_value<bool>(true);

    return PropertyList({
        p_ing, p_cond, p_meal, p_age, p_cuisine, p_top_k,
        p_tools, p_allergens, p_excluded, p_plant,
    });
}

inline bool JsonHasKey(const std::string& json, const char* key) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) return false;
    bool found = cJSON_GetObjectItem(root, key) != nullptr;
    cJSON_Delete(root);
    return found;
}

inline std::string JsonGetStringArrayJoined(const std::string& json, const char* key) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) return {};
    auto* arr = cJSON_GetObjectItem(root, key);
    std::string out;
    if (cJSON_IsArray(arr)) {
        int n = cJSON_GetArraySize(arr);
        for (int i = 0; i < n; i++) {
            auto* item = cJSON_GetArrayItem(arr, i);
            if (!cJSON_IsString(item)) continue;
            if (!out.empty()) out += "|";
            out += item->valuestring;
        }
    }
    cJSON_Delete(root);
    return out;
}
