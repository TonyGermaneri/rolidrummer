#pragma once
#include "Scheduler.h"
#include <ssb/Model.h>
#include <juce_audio_formats/juce_audio_formats.h>
namespace drummer {
struct Runtime {
 Configuration config;
 ssb::PerformancePtr performance;
 std::array<ssb::SoundPtr,128> sounds;
 std::array<int,5> cc {{107,109,111,113,114}};
 juce::String json;
 uint64_t revision=0;
};
juce::var defaultDocument();
std::unique_ptr<Runtime> compileDocument(const juce::var&);
juce::var waveform(const juce::var&);
// channel: -1 auto (channel 10 or the only channel), 0 all, 1–16 explicit.
// track: -1 all, otherwise a zero-based file track.
juce::var inspectMidi(const juce::File&);
juce::var importMidi(const juce::File&, int channel = -1, int track = -1);
juce::var listDirectory(const juce::File&,bool patterns);
juce::File dataDirectory();
}
