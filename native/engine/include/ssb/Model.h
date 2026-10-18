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

enum class LfoShape : uint8_t { sine, triangle, square, sawtooth, rampDown, random, smooth, wander };
enum class ModSource : uint8_t { lfo1, lfo2, mod, aftertouch, velocity, key, random, bend, timbre, fenv, vco1, vco2, vco3 };
enum class ModDest : uint8_t { pitch, cutoff, resonance, volume, pan, grainPos, grainSize, grainSkew, grainVar, grainDetune,
                               grainScan, lfo1Rate, lfo1Amt, lfo2Rate, lfo2Amt, delayMix, reverbMix };

struct LfoDef
{
    LfoShape shape { LfoShape::sine };
    float rate { 5.0f };          // Hz when not synced
    bool sync { false };
    float divisionBeats { 0.5f }; // quarter notes per cycle when synced
    // Granulator III's (types.ts Lfo)
    bool ratioMode { false };     // RATIO: ratio × middle C (a pad's LFO follows the note from there)
    float ratio { 1 };
    float amount { 1 };           // depth 0..1, before the routes' amounts
    float stereo { 0 };           // degrees 0..180 between the sides
    float phase { 0 };            // degrees a pad's LFO starts each note at; < 0: FREE
    float quant { 0 };

    static constexpr double ratioHz = 261.63;   // middle C

    /** Cycles per second at `bpm`. */
    double hz (double bpm) const noexcept
    {
        return ratioMode ? ratioHz * ratio : sync ? bpm / 60.0 / (double) divisionBeats : (double) rate;
    }
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
        case ModDest::grainSize: return 24.0f;   // semitones of GRAIN (Granulator II's Grain<LFO)
        case ModDest::grainPos:  return 0.5f;
        case ModDest::grainSkew: return 0.5f;
        case ModDest::grainDetune: return 12.0f;   // semitones
        case ModDest::grainScan: return 2.0f;    // SCAN's speed × (1 + x)
        case ModDest::lfo1Rate:
        case ModDest::lfo2Rate:  return 36.0f;   // semitones of the pad LFO's frequency
        default:                 return 1.0f;
    }
}

/** LFOs, bend and VCO signals swing both ways; the rest are 0..1. */
inline bool isBipolar (ModSource s) noexcept
{
    return s == ModSource::lfo1 || s == ModSource::lfo2 || s == ModSource::bend || s == ModSource::random
        || s == ModSource::vco1 || s == ModSource::vco2 || s == ModSource::vco3;
}

// ---------------------------------------------------------------------------------- sounds

enum class TriggerMode : uint8_t { stop, restart, stack, hold };
// Granulator II's menus (grainMath.ts Symmetry / SpraySign / AmpMode)
enum class GrainSymmetry : uint8_t { std, fall, rise, noiz };
enum class SpraySign : uint8_t { sym, right, left };
enum class GrainAmpMode : uint8_t { flux, drop };   // 'void' in the page
// Granulator III's grain modes (grainMath.ts GrainMode)
enum class GrainMode : uint8_t { classic, loop, cloud };
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
    uint8_t svfType { 0 };          // 0: the biquad above; 1..9: Granulator III's (svfMath.ts SVF_TYPES order)
    float cutoff { 20000 }, resonance { 0.7f };
    float volKey { 0 };             // dB an octave from ROOT (Key > Vol)
    float envKey { 0 }, fEnvKey { 0 };   // 0..1: the ADSRs' times ÷ 2^(it × semitones from ROOT / 12)
    float filterWidth { -12 };      // semitones: Granulator III's second stage from the first
    float filterKey { 0 };          // 0..1: the cutoff follows the note from ROOT
    float fEnvAmount { 0 }, fAttack { 0.001f }, fDecay { 0.3f }, fSustain { 0 }, fRelease { 0.2f };
    // the grain cloud, after Granulator II (types.ts GRAIN_DEFAULTS, grainMath.ts); STRETCH walks grains through
    // the clip at SPEED. Granulator II's parameter names in brackets.
    bool grain { false };
    float grainFreq { 5 };          // [GrainSize] Hz: a grain lasts one period, two overlap
    float grainFreqKey { 1 };       // [Grain<Key] 0..1
    float grainFreqRnd { 0 };       // [Grain<Random] 0..1: up to ±25 semitones
    float grainStereo { 0 };        // [GrainSpread] 0..1: left / right at GRAIN ÷× (1 + s²)
    float grainPos { 0.5f };        // [FilePos] 0..1 of the clip
    float grainPosKey { 0 };        // [FilePos<Key] -1..1: 1 % of the clip per semitone at ±1
    float grainSpray { 0 };         // [Spray] ms
    float grainSpraySlope { 1 };    // [SpraySlope] 1..10
    SpraySign grainSpraySign { SpraySign::sym };
    float grainWindow { 0 };        // [WindowShape] 0..1
    GrainSymmetry grainSymmetry { GrainSymmetry::std };
    float grainTuneKey { 1 };       // [Tune<Key] 0..1
    float grainTuneRnd { 0 };       // [Tune<Rnd] 0..1
    GrainAmpMode grainAmpMode { GrainAmpMode::flux };
    float grainAmp { 0 };           // [FluxusAmount] 0..1
    float grainVoid { 0 };          // [AmpVoidResidual] 0..1
    bool grainFm { false };         // [FMOn]
    float grainFmFreq { 220 };      // [FMFreq] Hz
    float grainFmAmount { 0 };      // [FMAmount] 0..250 (× 0.02 ms)
    float grainFmKey { 1 };         // [FM<Key] 0..2
    bool grainScanOn { false };     // [ScanOn]
    float grainScanTime { 100 };    // [ScanTime] %
    float grainScanDist { 1 };      // [ScanDistance] 0..1
    float grainScanCurve { 1 };     // [ScanCurve]
    // Granulator III's additions, its names in brackets
    GrainMode grainMode { GrainMode::classic };   // [Grain Mode]
    float grainSkew { 0.5f };       // [Shape] 0..1: CLASSIC's window skew, LOOP's crossfade
    float grainDensity { 4 };       // [Density] 2..20: CLOUD's grains a side
    float grainReverse { 0 };       // [Reverse] 0..1: LOOP
    float grainVariation { 0 };     // [Variation] 0..1
    float grainVarPos { 1000 };     // [Position Variation] ms
    float grainVarSize { 0 };       // [Grain Size Variation] semitones
    float grainVarPitch { 0 };      // [Pitch Variation] semitones (CLOUD)
    float grainVarVol { 0 };        // [Volume Variation] dB
    float grainDetune { 0 };        // [Spread] semitones: left lower, right higher
    float grainNoteRnd { 0 };       // [Random > Pitch] semitones
    bool grainScanLoop { false };   // [Loop]
    bool grainScanKey { false };    // [Key > Scan]
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
    float cutoff { 20000 }, resonance { 0.7f };
    // Reverb, delay, chorus, cutoff, resonance; -1 = knob, 0..4 = global surface.
    std::array<int,5> sources {{ -1, -1, -1, -1, -1 }};
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
    // Drummer owns per-contact delay/room buses and never feeds the legacy
    // shared per-sound convolvers. Keep their IR preparation out of that path.
    bool contactEffectsOnly { false };
    bool hold { false };   // HOLD: Granulator III's latch
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
