#pragma once
#include "NeuralIndex.h"

namespace drummer {
constexpr int patchResolverVersion = 13;
// Worker diagnostics are returned separately from the reproducible patch.
struct PatchResolveMetrics {
    double intentMilliseconds=0,partCandidateMilliseconds=0,clipLoadMilliseconds=0;
    double selectionMilliseconds=0,documentMilliseconds=0,totalMilliseconds=0;
    int partMasks=0,clipLoads=0;
};
// Resolves outside rendering and returns a self-contained document. The caller
// applies it with the same revision guard as any other prepared patch edit.
juce::var resolveNeuralPatch(PatternLibrary&, NeuralIndex&, const juce::var& document, const juce::var& options,PatchResolveMetrics* metrics=nullptr);
}
