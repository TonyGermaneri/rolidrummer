#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace drummer {
constexpr int patternVariationVersion = 1;
struct PatternVariationOptions {
    juce::String seed = "0", role = "groove", feel = "straight";
    std::vector<int> parts;
    int numerator = 4, denominator = 4, phraseBars = 8, count = 2;
};
// Worker-only. Parents are canonical clips ordered by rhythm-space proximity.
// Results contain actual per-part notes and complete generation provenance.
// Incompatible/identical parents yield fewer results, rather than invented data.
std::vector<juce::var> generatePatternVariations(const std::vector<juce::var>& parents,
                                              const PatternVariationOptions&);
}
