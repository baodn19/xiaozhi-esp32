#pragma once

// Minimal PropertyList types for host-side tests (mirrors main/mcp_server.h).

#include <cJSON.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

enum PropertyType {
    kPropertyTypeBoolean,
    kPropertyTypeInteger,
    kPropertyTypeString
};

class Property {
private:
    std::string name_;
    PropertyType type_;
    std::variant<bool, int, std::string> value_;
    bool has_default_value_;
    std::optional<int> min_value_;
    std::optional<int> max_value_;

public:
    Property(const std::string& name, PropertyType type)
        : name_(name), type_(type), has_default_value_(false) {}

    template<typename T>
    Property(const std::string& name, PropertyType type, const T& default_value)
        : name_(name), type_(type), has_default_value_(true) {
        value_ = default_value;
    }

    Property(const std::string& name, PropertyType type, int default_value, int min_value, int max_value)
        : name_(name), type_(type), has_default_value_(true), min_value_(min_value), max_value_(max_value) {
        if (default_value < min_value || default_value > max_value) {
            throw std::invalid_argument("Default value must be within the specified range");
        }
        value_ = default_value;
    }

    inline const std::string& name() const { return name_; }

    template<typename T>
    inline T value() const {
        return std::get<T>(value_);
    }

    template<typename T>
    inline void set_value(const T& value) {
        if constexpr (std::is_same_v<T, int>) {
            if (min_value_.has_value() && value < min_value_.value()) {
                throw std::invalid_argument("Value is below minimum allowed");
            }
            if (max_value_.has_value() && value > max_value_.value()) {
                throw std::invalid_argument("Value exceeds maximum allowed");
            }
        }
        value_ = value;
    }
};

class PropertyList {
private:
    std::vector<Property> properties_;

public:
    PropertyList() = default;
    PropertyList(const std::vector<Property>& properties) : properties_(properties) {}

    const Property& operator[](const std::string& name) const {
        for (const auto& property : properties_) {
            if (property.name() == name) {
                return property;
            }
        }
        throw std::runtime_error("Property not found: " + name);
    }
};
