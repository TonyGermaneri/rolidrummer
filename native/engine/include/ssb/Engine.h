#pragma once

// SSB's sound engine for the plugin: plays the page's sounds and patches from MIDI, on the audio
// thread, with no page running. A port of src/audio (voice.ts, engine.ts, masterFx.ts) and the
// note logic in src/stores/board.ts.
//
// Threads: `setPerformance`, `prepare` and `collectGarbage` are for the message thread; `process`
// is the audio thread; `post` (commands from the editor) is safe from either. Nothing on the audio
// thread allocates or locks.

#include "Dsp.h"
#include "Model.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace juce::dsp { class Convolution; }

namespace ssb
{

/** A command from the editor (a pad clicked, a key played on the page's keyboard). */
struct Command
{
    enum class Type : uint8_t { press, release, noteOn, noteOff, panic, cc, midi };
    Type type { Type::panic };
    char id[64] {};          // press / release: the sound (or patch layer) id
    float velocity { 1 };
    uint8_t note { 60 }, channel { 0 }, cc { 0 };
    float value { 0 };       // cc value 0..1
    uint8_t bytes[3] {};     // midi: a raw short message (the page's bend wheel, mod knob, pedal)

    static Command make (Type t, std::string_view soundId = {}, float vel = 1);
};

/** What the editor shows of a playing voice (pad LEDs, progress rings, waveform playheads). */
struct VoiceView
{
    uint32_t id { 0 };            // unique per note-on
    uint64_t sound { 0 };         // idHash of the sound it plays
    uint64_t group { 0 };         // idHash of the patch sound it plays for (VCOs), 0 = none
    int midiNote { -1 };
    float velocity { 1 };         // 0..1
    float age { 0 };              // seconds since note-on
    float end { -1 };             // seconds after note-on it stops; -1 = until released
    float position { 0 };         // seconds into its sample
    float rate { 1 };             // sample seconds per second, now
    float duration { 0 };         // of its sample, seconds
    float clipIn { 0 }, clipOut { 0 };
    bool loops { false };
    bool grains { false };        // a grain cloud: no single playhead
};

/** A grain as it started, for the editor's waveform (types: grainMath.ts GrainEvent). */
struct GrainView
{
    uint32_t voice { 0 };         // VoiceView::id
    double when { 0 };            // engine time, seconds
    float pos { 0 }, len { 0 };   // seconds of the sample it plays, forward
    float dur { 0 };              // seconds it sounds
    float rate { 1 };             // playback rate (pitch)
    float pan { 0 }, gain { 1 };
    bool reverse { false };
    uint8_t stream { 0 };
};

/** FNV-1a: how a VoiceView names its sound without the audio thread touching a string. */
uint64_t idHash (std::string_view id) noexcept;

/** Where the host's transport is: its tempo, and (when it knows) its position in quarter notes and
    whether it is playing. A default HostClock = no host (the page's tempo). */
struct HostClock
{
    double bpm { 0 };
    double ppq { -1 };      // < 0 = unknown
    bool playing { false };
};

/** Short MIDI message at a sample offset within the block. */
struct MidiEvent
{
    int offset { 0 };
    uint8_t status { 0 }, data1 { 0 }, data2 { 0 };
};

class Engine
{
public:
    static constexpr int maxVoices = 64;

    Engine();
    ~Engine();

    // ---- message thread ------------------------------------------------------------------
    void prepare (double sampleRate, int maxBlockSize);

    /** Swap in what to play. Voices already sounding follow the new settings of their sound (by
        id), as the page's voices follow knob changes. Reverb impulses the new settings need are
        loaded here, off the audio thread. */
    void setPerformance (PerformancePtr next);
    void preparePerformance(const PerformancePtr& next); // non-realtime resource preparation
    bool canActivatePerformance() const noexcept;
    bool activatePerformance(const PerformancePtr& next) noexcept; // audio-thread atomic document adoption

    /** Free what the audio thread has finished with (old performances, sounds, samples). */
    void collectGarbage();

    // ---- any thread ----------------------------------------------------------------------
    bool post (const Command& c) noexcept;

    // ---- audio thread ----------------------------------------------------------------------
    using HostClock = ssb::HostClock;

    /** Render `n` samples into `left` / `right` (overwritten), following the host's clock. */
    void process (float* left, float* right, int n, const MidiEvent* midi, int midiCount, HostClock host = {}) noexcept;

    // Explicit drum events, audio thread only, after process(..., n=0) adopts pending state.
    void drumHit(const SoundPtr&, uint64_t owner, uint64_t token, float velocity, const std::array<float,5>&) noexcept;
    void drumExpression(uint64_t owner, const std::array<float,5>&) noexcept;
    void drumRelease(uint64_t owner, const std::array<float,5>&, bool hard) noexcept;
    void drumEnd(uint64_t token) noexcept;
    void drumPanic() noexcept;
    void drumGlobal(int index, float value) noexcept;

    // ---- meters (any thread) ---------------------------------------------------------------
    float peak (int channel) const noexcept { return peaks[(size_t) (channel & 1)].load (std::memory_order_relaxed); }
    int activeVoices() const noexcept { return active.load (std::memory_order_relaxed); }

    /** The playing voices as of the last block, and the engine's clock then (seconds). Any thread;
        false if a block was being published at that moment (just ask again next time). */
    bool readVoices (std::vector<VoiceView>& out, double& time) const;

    /** Grains started since the last call, oldest first: the latest `max` of them (the engine holds up to
        2048 until they are read, dropping later ones meanwhile). One reader thread. */
    void readGrains (std::vector<GrainView>& out, size_t max);

    double sampleRate() const noexcept { return rate; }

private:
    struct Voice;
    struct Impl;
    std::unique_ptr<Impl> impl;

    double rate { 48000 };
    std::array<std::atomic<float>, 2> peaks {};
    std::atomic<int> active { 0 };
};

/** Web Audio's ConvolverNode normalisation (on by default): scales an impulse so reverbs of any
    length come out at a similar level. From the spec's calculateNormalizationScale. */
float convolverNormalisation (const std::vector<std::vector<float>>& ir, double sampleRate) noexcept;

} // namespace ssb
