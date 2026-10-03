#pragma once

// The page's JSON (src/native/engineSync.ts) -> the engine's model. Message thread only.

#include "Model.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <map>

namespace ssb
{

/** Samples by audioId, as they arrive from the page (or from the disk cache). */
using SampleLookup = std::function<SamplePtr (const std::string& audioId)>;

/** One sound (or patch layer): { id, audioId, s: settings, zones?, links?, ccDefaults? }.
    Links are left unresolved (Link::osc) -- see buildPerformance. */
std::shared_ptr<Sound> soundFromJson (const juce::var& json, const SampleLookup& samples);

/** Settings, from the page's SoundSettings object. */
Settings settingsFromJson (const juce::var& json);
ModMatrix matrixFromJson (const juce::var& json);

/**
    The performance: { volume, muted, mono, play, mpe, glide, mpeBendRange, bpm, fx, mod,
    playable, padNotes: [[note, id]], pads: [id] }, resolved against `sounds` (which also resolves
    every sound's VCO links).
*/
PerformancePtr buildPerformance (const juce::var& meta, const std::map<std::string, std::shared_ptr<Sound>>& sounds);

} // namespace ssb
