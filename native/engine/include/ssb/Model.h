#pragma once

// What the engine plays: the page's sounds, patches and master settings, as plain data.
//
// Mirrors src/types.ts. The page is the source of truth; it sends these as JSON (ModelJson.cpp)
// and the engine never edits them. Everything here is immutable once built and shared with the
// audio thread through shared_ptr<const ...>, so a voice can keep playing a sound while the page
// replaces it.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ssb
{

// ---------------------------------------------------------------------------------- audio

/** One decoded sample: planar float channels (1 or 2) at its own rate. */
struct Sample
{
    std::vector<std::vector<float>> channels;
    double rate { 44100.0 };

    size_t frames() const noexcept { return channels.empty() ? 0 : channels[0].size(); }
    double duration() const noexcept { return rate > 0 ? (double) frames() / rate : 0.0; }
};
using SamplePtr = std::shared_ptr<const Sample>;

// ---------------------------------------------------------------------------------- modulation

enum class LfoShape : uint8_t { sine, triangle, square, sawtooth, rampDown, random, smooth };
enum class ModSource : uint8_t { lfo1, lfo2, mod, aftertouch, velocity, bend, timbre, vco1, vco2, vco3 };
enum class ModDest : uint8_t { pitch, cutoff, resonance, volume, pan, grainPos, grainSize, delayMix, reverbMix };

struct LfoDef
{
    LfoShape shape { LfoShape::sine };
    float rate { 5.0f };          // Hz when not synced
    bool sync { false };
    float divisionBeats { 0.5f }; // quarter notes per cycle when synced

    /** Cycles per second at `bpm`. */
    double hz (double bpm) const noexcept { return sync ? bpm / 60.0 / (double) divisionBeats : (double) rate; }
};

struct ModRoute
{
    ModSource source { ModSource::lfo1 };
    ModDest dest { ModDest::pitch };
    float amount { 0.0f };   // -1..1 of the destination's scale
};

struct ModMatrix
{
    LfoDef lfo1 { LfoShape::sine, 5.0f, false, 0.5f };
    LfoDef lfo2 { LfoShape::triangle, 0.5f, false, 4.0f };
    std::vector<ModRoute> routes;
};

/** Destination units at amount ±1 (MOD_DESTS in types.ts). */
inline float modScale (ModDest d) noexcept
{
    switch (d)
    {
        case ModDest::pitch:     return 12.0f;   // semitones
        case ModDest::cutoff:    return 5.0f;    // octaves
        case ModDest::resonance: return 10.0f;   // Q
        case ModDest::grainSize: return 250.0f;  // ms
        case ModDest::grainPos:  return 0.5f;
        default:                 return 1.0f;
    }
}

/** LFOs, bend and VCO signals swing both ways; the rest are 0..1. */
inline bool isBipolar (ModSource s) noexcept
{
    return s == ModSource::lfo1 || s == ModSource::lfo2 || s == ModSource::bend
        || s == ModSource::vco1 || s == ModSource::vco2 || s == ModSource::vco3;
}

// ---------------------------------------------------------------------------------- sounds

enum class TriggerMode : uint8_t { stop, restart, stack, hold };
enum class FilterType : uint8_t { lowpass, highpass, bandpass, lowshelf, highshelf, peaking, notch, allpass };

/** SoundSettings (types.ts), minus the purely visual fields. */
struct Settings
{
    float volume { 1 }, pan { 0 };
    int repeat { 1 };      // 0 = loop forever
    int choke { 0 };
    TriggerMode mode { TriggerMode::stop };
    float pitch { 0 }, fine { 0 }, speed { 1 };
    bool reverse { false };
    bool stretch { false };     // timeMode 'stretch': speed and pitch independent (grains)
    float clipIn { 0 }, clipOut { 1 };
    float attack { 0.001f }, decay { 0.2f }, sustain { 1 }, release { 0.02f };
    FilterType filterType { FilterType::lowpass };
    float cutoff { 20000 }, resonance { 0.7f };
    float fEnvAmount { 0 }, fAttack { 0.001f }, fDecay { 0.3f }, fSustain { 0 }, fRelease { 0.2f };
    // grains (a cloud when `grain` is on; STRETCH walks grains through the clip at SPEED). As types.ts's
    // GRAIN_DEFAULTS: RATE is grains per second per stream, independent of SIZE; SHAPE the window's taper
    bool grain { false };
    float grainSize { 80 };     // ms, at least one sample
    float grainPos { 0.5f }, grainWidth { 0.1f }, grainRate { 30 }, grainShape { 1 }, grainJitter { 0 }, grainReverse { 0 };
    float grainSpread { 0.3f }, grainStreams { 1 }, grainScatter { 0.5f }, grainDrift { 0 };
    float delayTime { 0.25f }, delayFeedback { 0.35f }, delayMix { 0 };
    float reverbSize { 2 }, reverbDecay { 3 }, reverbMix { 0 };
    float eqLow { 0 }, eqMid { 0 }, eqHigh { 0 };
    float rootNote { 60 }, velAmount { 1 }, bendRange { 2 };
    ModMatrix mod;
};

enum class LoopMode : uint8_t { noLoop, oneShot, loopContinuous, loopSustain };

struct ZoneEnv
{
    std::optional<float> a, d, s, r;
};

struct ZoneCcMod
{
    enum class Target : uint8_t { volume, amplitude, pan, pitch, cutoff, resonance };
    int cc { 0 };
    Target target { Target::volume };
    float amount { 0 };   // dB, %, %, cents, cents, dB
};

struct ZoneFilter
{
    FilterType type { FilterType::lowpass };
    float cutoff { 20000 }, resonance { 0 };   // Hz, dB
    float keytrack { 0 }, keycenter { 60 }, veltrack { 0 };
    struct Env { float a { 0 }, d { 0 }, s { 1 }, r { 0 }, depth { 0 }; };
    std::optional<Env> env;
};

/** An SFZ region LFO (lfoN_*): an oscillator into one destination, starting after its delay. */
struct ZoneLfo
{
    enum class Target : uint8_t { pitch, cutoff, volume, pan };
    Target target { Target::pitch };
    LfoShape wave { LfoShape::sine };
    float freq { 1 }, depth { 0 }, delay { 0 };   // depth: cents, cents, dB, %
    bool invert { false };
};

/** One sample of a multi-sample (SFZ) instrument. Times in seconds of its sample. */
struct Zone
{
    SamplePtr sample;
    std::string audioId;
    int lokey { 0 }, hikey { 127 }, lovel { 1 }, hivel { 127 };
    float keycenter { 60 }, keytrack { 100 }, transpose { 0 }, tune { 0 };
    float volume { 0 }, pan { 0 };             // dB, -100..100
    float offset { 0 }, end { 0 };
    LoopMode loopMode { LoopMode::noLoop };
    float loopStart { 0 }, loopEnd { 0 };
    int seqLength { 1 }, seqPosition { 1 };
    float lorand { 0 }, hirand { 1 };
    ZoneEnv env;
    std::optional<float> amplitude;            // % when amplitude_onccN drives it
    bool releaseTrigger { false };
    float rtDecay { 0 };                       // dB per second held
    struct CcRange { int cc; int lo; int hi; };
    std::vector<CcRange> ccRange;
    std::vector<ZoneCcMod> ccMods;
    std::optional<ZoneFilter> filter;
    std::vector<ZoneLfo> lfos;

    /** Semitones to shift this zone's sample to play `note`. */
    float semisFor (float note) const noexcept { return (note - keycenter) * keytrack / 100.0f + transpose + tune / 100.0f; }
};

struct Sound;

/** Another sound layered onto this one as an oscillator (a patch's other slots). */
struct Link
{
    int index { 0 };          // VCO n in the matrix = slot n (0-based)
    std::string soundId;
    std::shared_ptr<const Sound> osc;   // resolved when the performance is built
    float level { 1 };
    bool audible { true }, track { true };
    float transpose { 0 }, fine { 0 };
    int fixedNote { 60 };
};

/** Absolute parameter mappings for a held Seaboard contact. Sources 0..4 are
    strike/glide/slide/press/lift; 5..9 are the five global controllers. */
struct ExpressionRoute
{
    int source { 0 }, target { -1 };
    float minimum { 0 }, maximum { 1 }, curve { 1 }, smoothingMs { 8 };
};

struct Sound
{
    std::string id, audioId;
    SamplePtr sample;
    Settings s;
    std::array<ExpressionRoute, 16> expressionRoutes {};
    int expressionRouteCount { 0 };
    std::vector<Zone> zones;
    std::vector<Link> links;
    std::vector<std::pair<int, float>> ccDefaults;   // SFZ set_ccN: cc -> 0..1
};
using SoundPtr = std::shared_ptr<const Sound>;

// ---------------------------------------------------------------------------------- master

struct GlobalFx
{
    float chorusRate { 0.6f }, chorusDepth { 0.5f }, chorusMix { 0 };
    float delayTime { 0.375f }, delayFeedback { 0.4f }, delayMix { 0 };   // seconds, when not synced
    bool delaySync { false };
    float delayBeats { 0.75f };   // synced: quarter notes (resolved against the host's tempo)
    float reverbSize { 3 }, reverbDecay { 3 }, reverbMix { 0 };
};

/** Everything the engine needs to answer MIDI on its own, with or without the editor open. */
struct Performance
{
    float volume { 0.8f };
    bool muted { false }, mono { false }, play { false }, mpe { false };
    float glide { 0 }, mpeBendRange { 48 }, bpm { 120 };
    GlobalFx fx;
    ModMatrix globalMod;

    /** What keyboard play (and the patch buttons) plays: the selected patch's main slot, or the
        selected sound. */
    SoundPtr playable;
    /** Pads that answer a MIDI note (explicit MIDI notes, or auto-assigned from a base note). */
    std::vector<std::pair<int, SoundPtr>> padNotes;
    /** The board's pads (choke groups act among these). */
    std::vector<SoundPtr> pads;
    /** Every sound and patch layer, by id (for presses from the editor). */
    std::unordered_map<std::string, SoundPtr> byId;

    SoundPtr find (const std::string& id) const
    {
        auto it = byId.find (id);
        return it == byId.end() ? nullptr : it->second;
    }
};
using PerformancePtr = std::shared_ptr<const Performance>;

} // namespace ssb
