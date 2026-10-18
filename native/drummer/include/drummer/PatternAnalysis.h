#pragma once
#include <juce_core/juce_core.h>

namespace drummer {
constexpr int patternFeatureVersion = 1;
constexpr int drumMappingVersion = 2;
// Returned descriptions are independent of archive/container locations and IDs.
juce::String describePattern(const juce::String& relativePath, const juce::var& metadata = {});
juce::String patternFamily(const juce::String& relativePath);
bool patternUsesVendorArticulations(const juce::String& description);
void validatePatternMapping(const juce::var& mapping);
juce::var selectPatternMapping(const juce::String& relativePath, const juce::var& mapping);
// mapping: {id: "auto"|"gm"|"vdrums"|"custom", notes: {sourceNote: role}}.
// Custom entries override the selected map; null explicitly leaves a note unknown.
// measure=false still maps and fingerprints the complete performance; the
// caller must restore committed features rather than publishing empty ones.
juce::var analyzePattern(const juce::var& clip, const juce::String& relativePath, const juce::var& mapping = {}, int mappingVersion = drumMappingVersion,bool measure = true);
juce::var drumMapDefinitions();
}
