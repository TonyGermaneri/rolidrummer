#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <stdexcept>

namespace drummer::json {
inline juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject;
    for (const auto& [name, value] : fields) result->setProperty(name, value);
    return result;
}
inline juce::var get(const juce::var& value, const char* name, juce::var fallback = {}) {
    return value.getProperty(name, fallback);
}
inline double number(const juce::var& value, const char* name, double fallback) {
    const auto raw = get(value, name, fallback);
    const double result = (double) raw;
    return std::isfinite(result) ? result : fallback;
}
inline void set(juce::var& value, const char* name, const juce::var& field) {
    if (auto* object = value.getDynamicObject()) object->setProperty(name, field);
    else throw std::runtime_error("Expected a JSON object");
}
}
