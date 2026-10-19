#pragma once
#include "Scheduler.h"
#include "MidiImport.h"
#include <ssb/Model.h>
#include <juce_audio_formats/juce_audio_formats.h>
namespace drummer {
struct Runtime {
 Configuration config;
 std::vector<juce::String> clipNames;
 ssb::PerformancePtr performance;
 std::array<ssb::SoundPtr,128> sounds;
 std::array<int,5> cc {{107,109,111,113,114}};
 juce::String json;
 uint64_t revision=0;
 std::array<uint8_t,32> patternHash{};
 double activationGrid=0;
};
juce::var defaultDocument();
std::unique_ptr<Runtime> compileDocument(const juce::var&);
juce::var waveform(const juce::var&);
juce::var listDirectory(const juce::File&,bool patterns);
juce::File dataDirectory();
}
