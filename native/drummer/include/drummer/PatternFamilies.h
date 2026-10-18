#pragma once
#include "NeuralIndex.h"

namespace drummer {
constexpr int familyPolicyVersion=6;
// Pure, versioned identity. Explicit meter and feel are never text-embedded
// away. A family is independent of patch copies and MIDI program assignment.
juce::var familyDefinition(const juce::var& options);
juce::String familyIdentity(const juce::String& query,int numerator=0,int denominator=0,const juce::String& feel={});

// Worker-only catalog operations. Ready builds are immutable and pinned to a
// complete neural index plus the registry's decision revision.
class PatternFamilies {
public:
    explicit PatternFamilies(juce::File database);
    juce::var registerFamily(const juce::var& options) const;
    juce::var update(const juce::var& operation) const;
    juce::var reconcile(NeuralIndex&,const NeuralBuildOptions& options={}) const;
    juce::var status() const;
    juce::var members(const juce::var& options) const;
    juce::var decisions(const juce::var& options) const;
    juce::var suggestions(int offset=0,int limit=25) const;
    juce::var decideSuggestion(const juce::var& options) const;
    // parts selects one mask. partMasks selects up to 128 masks in one pinned
    // query and returns pools of {parts,rows}; their feature values are read-only.
    juce::var selection(NeuralIndex&,const juce::var& options,const SemanticAffinities* prepared=nullptr) const;
    juce::var lookup(const juce::var& options) const;
private:
    juce::File database;
};
}
