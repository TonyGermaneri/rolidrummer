#pragma once
#include <juce_core/juce_core.h>
namespace drummer {
// Worker-only filename/kit-name embeddings, shared with the pattern model.
// Returns sound assignments; never edits patterns or the running instrument.
juce::var matchSoundKit(const juce::File& vectorCache,const juce::var& options);
}
