#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_cryptography/juce_cryptography.h>

namespace drummer {
// All asset I/O is performed during import/state preparation, never rendering.
juce::File assetFile(const juce::String& id);
juce::File cacheSound(juce::var& sound);
void appendSessionAssets(juce::ValueTree&, const juce::var& document);
void restoreSessionAssets(const juce::ValueTree&);
void savePortablePatch(const juce::File&, const juce::var& document);
juce::var loadPatch(const juce::File&);
}
