#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace drummer {
constexpr int midiImportVersion = 2;
// MIDI parsing is shared by direct imports, archive indexing and headless tools.
// Call only from a worker. The returned document owns all notes and metadata.
juce::MidiFile readMidi(juce::InputStream&);
juce::var inspectMidi(const juce::MidiFile&);
juce::var inspectMidi(const juce::File&);
// channel: -1 auto (channel 10 or the only channel), 0 all, 1–16 explicit.
// track: -1 all, otherwise a zero-based file track.
juce::var importMidi(const juce::MidiFile&, const juce::String& source, int channel = -1, int track = -1);
juce::var importMidi(const juce::File&, int channel = -1, int track = -1);
}
