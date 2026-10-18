#pragma once

// Small DSP pieces that reproduce what the page gets from Web Audio, so a sound set up in the
// browser sounds the same in the plugin. Each names the Web Audio behaviour it follows.

#include "Model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ssb::dsp
{

constexpr double pi = 3.14159265358979323846;

inline float semisToRate (double semis) noexcept { return (float) std::pow (2.0, semis / 12.0); }
inline float dbToGain (double db) noexcept { return (float) std::pow (10.0, db / 20.0); }

// ------------------------------------------------------------------------------------ envelope

/** The page's ADSR (audio/envelope.ts): linear attack to 1, linear decay to sustain, and a
    linear release from wherever it was to 0. */
struct Env
{
    float a { 0 }, d { 0 }, s { 1 }, r { 0 };

    static constexpr float minDecay = 0.001f, minRelease = 0.005f;

    /** Level `t` seconds after note-on, before release. */
    float level (double t) const noexcept
    {
        if (t < a)
            return t <= 0 ? 0.0f : (float) (t / a);
        const double td = t - a;
        const double dd = std::max ((double) d, (double) minDecay);
        if (td < dd)
            return (float) (1.0 - (1.0 - s) * (td / dd));
        return s;
    }

    /** Level at `t` for a note released at `tr` (Infinity = not released). */
    float at (double t, double tr) const noexcept
    {
        if (t < tr)
            return level (t);
        const double rr = std::max ((double) r, (double) minRelease);
        const double k = (t - tr) / rr;
        return k >= 1.0 ? 0.0f : (float) (level (tr) * (1.0 - k));
    }

    double releaseLength() const noexcept { return std::max ((double) r, (double) minRelease); }
};

// ------------------------------------------------------------------------------------ biquad

/** BiquadFilterNode, coefficient for coefficient (the Web Audio spec's "Audio EQ Cookbook"
    variant): lowpass / highpass take Q in dB, bandpass / notch / allpass / peaking take it
    linear, and shelves use S = 1. */
struct BiquadCoefs
{
    double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 };

    static BiquadCoefs make (FilterType type, double freq, double q, double gainDb, double sampleRate) noexcept
    {
        const double nyquist = sampleRate * 0.5;
        const double f = std::clamp (freq, 0.0, nyquist);
        BiquadCoefs c;
        if (f <= 0.0 || f >= nyquist)
        {
            // The spec's edge cases: at 0 Hz a lowpass passes nothing, at Nyquist everything.
            if (type == FilterType::lowpass)  { c.b0 = f <= 0.0 ? 0.0 : 1.0; }
            if (type == FilterType::highpass) { c.b0 = f <= 0.0 ? 1.0 : 0.0; }
            return c;
        }
        const double w0 = 2.0 * pi * f / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double alphaQ = sw / (2.0 * std::max (q, 1e-4));
        const double alphaQdB = sw / (2.0 * std::pow (10.0, q / 20.0));
        const double alphaS = sw / 2.0 * std::sqrt (2.0);   // S = 1
        const double sa = 2.0 * alphaS * std::sqrt (A);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch (type)
        {
            case FilterType::lowpass:
                b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + alphaQdB; a1 = -2 * cw; a2 = 1 - alphaQdB; break;
            case FilterType::highpass:
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alphaQdB; a1 = -2 * cw; a2 = 1 - alphaQdB; break;
            case FilterType::bandpass:
                b0 = alphaQ; b1 = 0; b2 = -alphaQ; a0 = 1 + alphaQ; a1 = -2 * cw; a2 = 1 - alphaQ; break;
            case FilterType::notch:
                b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + alphaQ; a1 = -2 * cw; a2 = 1 - alphaQ; break;
            case FilterType::allpass:
                b0 = 1 - alphaQ; b1 = -2 * cw; b2 = 1 + alphaQ; a0 = 1 + alphaQ; a1 = -2 * cw; a2 = 1 - alphaQ; break;
            case FilterType::peaking:
                b0 = 1 + alphaQ * A; b1 = -2 * cw; b2 = 1 - alphaQ * A; a0 = 1 + alphaQ / A; a1 = -2 * cw; a2 = 1 - alphaQ / A; break;
            case FilterType::lowshelf:
                b0 = A * ((A + 1) - (A - 1) * cw + sa); b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - sa); a0 = (A + 1) + (A - 1) * cw + sa;
                a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sa; break;
            case FilterType::highshelf:
                b0 = A * ((A + 1) + (A - 1) * cw + sa); b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - sa); a0 = (A + 1) - (A - 1) * cw + sa;
                a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sa; break;
        }
        c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
        return c;
    }
};

/** Transposed direct form II, one channel. */
struct Biquad
{
    double z1 { 0 }, z2 { 0 };

    float process (const BiquadCoefs& c, float x) noexcept
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }
    void reset() noexcept { z1 = z2 = 0; }
};

// ------------------------------------------------------------------------------------ LFO

/** lfoValue (lib/modulation.ts): OscillatorNode's shapes, sine / triangle / sawtooth starting at
    0 rising and square starting high. */
inline float lfoValue (LfoShape shape, double phase) noexcept
{
    const double p = phase - std::floor (phase);
    switch (shape)
    {
        case LfoShape::sine:     return (float) std::sin (2 * pi * p);
        case LfoShape::triangle: return (float) (p < 0.25 ? 4 * p : p < 0.75 ? 2 - 4 * p : 4 * p - 4);
        case LfoShape::square:   return p < 0.5 ? 1.0f : -1.0f;
        case LfoShape::sawtooth: return (float) (p < 0.5 ? 2 * p : 2 * p - 2);
        case LfoShape::rampDown: return (float) (p < 0.5 ? -2 * p : 2 - 2 * p);
        default:                 return 0.0f;
    }
}

// ------------------------------------------------------------------------------------ Granulator III's filter
// src/audio/svfMath.ts: two 12 dB trapezoidal state-variable stages in series.

/** A stage: 0 off (passes, state held), 1 LP, 2 NOTCH (Granulator III's: x − bp + (1 − k)·lp), 3 HP. */
struct SvfType { uint8_t stage1, stage2; bool width, q2Res; };

/** SVF_TABLE, by svfType 1..9 (index 0 unused). */
inline const SvfType& svfType (uint8_t t) noexcept
{
    static const SvfType table[] {
        { 0, 0, false, false },
        { 1, 0, false, false }, { 1, 1, false, false }, { 1, 3, true, true }, { 1, 2, true, false }, { 2, 0, false, false },
        { 2, 2, true, true }, { 3, 2, true, false }, { 3, 0, false, false }, { 3, 3, false, false },
    };
    return table[t < 10 ? t : 0];
}

/** stage 2's Q: 0.707, or RES mapped 0.7..7 → 0.7..3 for LP+HP and NO+NO */
inline double svfQ2 (uint8_t t, double q) noexcept { return svfType (t).q2Res ? 0.7 + (q - 0.7) * 2.3 / 6.3 : 0.707; }

constexpr double svfSmooth = 0.02;   // the knob's cutoff follows it by this much a sample

/** g for a cutoff, clamped to 10 Hz .. 0.45 × the sample rate */
inline double svfG (double fc, double sampleRate) noexcept
{
    return std::tan (pi * std::clamp (fc, 10.0, 0.45 * sampleRate) / sampleRate);
}

struct SvfStage
{
    double s1 { 0 }, s2 { 0 };

    float tick (uint8_t mode, double g, double k, float x) noexcept
    {
        if (mode == 0) return x;
        const double hp = (x - s2 - (k + g) * s1) / (1 + g * (g + k));
        const double bp = s1 + g * hp;
        const double lp = s2 + g * bp;
        s1 = bp + g * hp;
        s2 = lp + g * bp;
        return (float) (mode == 1 ? lp : mode == 3 ? x - k * bp - lp : x - bp + (1 - k) * lp);
    }
    void reset() noexcept { s1 = s2 = 0; }
};

/** lfoQuant (lfoMath.ts): QUANT's steps, halves rounding away from 0. */
inline float lfoQuant (float v, float quant) noexcept
{
    if (quant <= 0) return v;
    const double q = quant + 1e-6;
    return (float) ((v < 0 ? -1.0 : v > 0 ? 1.0 : 0.0) * std::round (std::abs (v) / q) * q);
}

/** lfoMath.ts LfoState: a running LFO, two sides STEREO apart. Random shapes draw two streams a and b at each step
    (S&H and SMOOTH once a cycle, RAND four times); with k = STEREO / 180 a side is a·(1 − k) ± b·k. RAND closes in
    on its step by 10 × the cycles a sample. */
class Lfo
{
public:
    void start (const LfoDef& def, double bpm, uint32_t seed, double phase = 0) noexcept
    {
        shape = def.shape;
        stereo = def.stereo;
        quant = def.quant;
        hz = std::max (0.0, def.hz (bpm));
        rng = seed ? seed : 0x9e3779b9u;
        reset (phase);
    }

    /** Follow changed settings, keeping the phase running. */
    void set (const LfoDef& def, double bpm) noexcept
    {
        hz = std::max (0.0, def.hz (bpm));
        stereo = def.stereo;
        quant = def.quant;
        if (def.shape != shape)
        {
            shape = def.shape;
            reset (phase);
        }
    }

    void restart() noexcept { reset (0); }

    /** Start from `p` cycles: random shapes draw afresh, RAND sits on its first value. */
    void reset (double p) noexcept
    {
        phase = p;
        step = (long) std::floor (phase * stepsPerCycle());
        prevA = prevB = 0;
        nextA = draw();
        nextB = draw();
        if (shape == LfoShape::random || shape == LfoShape::wander) { prevA = nextA; prevB = nextB; }
        y[0] = target (0);
        y[1] = target (1);
    }

    /** Jump to an absolute phase (cycles), e.g. the host's position / the LFO's division, so a
        synced LFO stays locked to the song. Random shapes step when a step boundary is crossed. */
    void syncTo (double cycles) noexcept { advance (cycles - phase, 1); }

    /** The raw shape on `side` (0 left, 1 right), QUANT applied: -1..1. */
    float value (int side = 0) const noexcept
    {
        float v;
        switch (shape)
        {
            case LfoShape::random: v = target (side); break;
            case LfoShape::wander: v = y[side ? 1 : 0]; break;
            case LfoShape::smooth:
            {
                const float k = std::clamp (stereo / 180.0f, 0.0f, 1.0f);
                const float f = (float) (phase - std::floor (phase));
                const float a = prevA + (nextA - prevA) * f, b = prevB + (nextB - prevB) * f;
                v = a * (1 - k) + (side ? -k : k) * b;
                break;
            }
            default:
            {
                const double s = stereo / 720.0;
                v = lfoValue (shape, phase + (side ? -s : s));
            }
        }
        return lfoQuant (v, quant);
    }

    /** Move on `inc` cycles over `n` samples. */
    void advance (double inc, int n) noexcept
    {
        phase += inc;
        const int spc = stepsPerCycle();
        const auto s = (long) std::floor (phase * spc);
        if (s != step)
        {
            const long crossed = std::min (4L, std::abs (s - step));
            step = s;
            for (long i = 0; i < crossed; ++i)
            {
                // S&H and RAND land on the value they hold; SMOOTH has ramped into it
                prevA = shape == LfoShape::smooth ? nextA : draw();
                prevB = shape == LfoShape::smooth ? nextB : draw();
                nextA = draw();
                nextB = draw();
            }
        }
        if (shape == LfoShape::wander)
        {
            const double c = std::min (1.0, 10.0 * std::abs (inc) / std::max (1, n));
            const double g = n <= 1 ? c : 1.0 - std::pow (1.0 - c, n);
            for (int side = 0; side < 2; ++side)
                y[side] += (float) (g * (target (side) - y[side]));
        }
        if (phase > 1e6 || phase < -1e6)
        {
            phase -= std::floor (phase);
            step = (long) std::floor (phase * spc);
        }
    }

    /** Advance by `seconds` at its frequency × `factor` (RATE's routes, RATIO's note), `n` samples. */
    void run (double seconds, int n, double factor = 1) noexcept { advance (seconds * hz * factor, n); }

    double frequency() const noexcept { return hz; }
    double phaseNow() const noexcept { return phase; }

private:
    int stepsPerCycle() const noexcept { return shape == LfoShape::wander ? 4 : 1; }

    float target (int side) const noexcept
    {
        const float k = std::clamp (stereo / 180.0f, 0.0f, 1.0f);
        return prevA * (1 - k) + (side ? -k : k) * prevB;
    }

    float draw() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) ((rng & 0xffffff) / (double) 0x800000 - 1.0);
    }

    LfoShape shape { LfoShape::sine };
    double hz { 1 }, phase { 0 };
    float stereo { 0 }, quant { 0 };
    uint32_t rng { 1 };
    long step { 0 };
    float prevA { 0 }, nextA { 0 }, prevB { 0 }, nextB { 0 };
    float y[2] { 0, 0 };
};

// ------------------------------------------------------------------------------------ grains
// src/audio/grainMath.ts, line for line: Granulator II's grain voice, as the page's worklet plays it.

constexpr double stretchGrain = 0.09;   // seconds: STRETCH's grains
constexpr int stretchOverlap = 4;
constexpr float freqRndSemis = 25;      // G<RND at 1
constexpr double freqRndRate = 8;       // its random target changes 8 × GRAIN times a second (rand~)
constexpr double maxPhaseStep = 0.5;
constexpr int windowPoints = 512;
using WindowTable = std::array<float, windowPoints + 1>;

/** xorshift32 (grainMath.ts xorshift): both engines draw the same NOIZ table. */
inline uint32_t xorshift (uint32_t& s) noexcept
{
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

/** Granulator II's "table noiz": 514 values 0..127. */
inline const std::array<int, windowPoints + 2>& noizTable() noexcept
{
    static const std::array<int, windowPoints + 2> table = []
    {
        std::array<int, windowPoints + 2> t {};
        uint32_t s = 0x5eed;
        for (auto& v : t) v = (int) (xorshift (s) % 128);
        return t;
    }();
    return table;
}

// Shared quarter-sine table, initialized during Engine::prepare rather than
// the callback. Live window modulation otherwise computes 513 sines per voice
// per control tick. Linear interpolation stays within 1e-7 of the formula.
inline const std::array<float, 4097>& windowSineTable() noexcept
{
    static const auto table = [] {
        std::array<float, 4097> t{};
        for (size_t i = 0; i < t.size(); ++i)
            t[i] = (float) std::sin (pi * 0.5 * (double) i / 4096);
        return t;
    }();
    return table;
}

/** The grain window (grainMath.ts windowTable): Granulator II's Window patch. */
inline void windowTable (float shape, GrainSymmetry sym, WindowTable& t) noexcept
{
    const double F = 1.0 + std::pow (0.04 * std::clamp (shape * 100.0, 0.0, 100.0), 3.0);
    const auto& noiz = noizTable();
    const auto& sine = windowSineTable();
    for (int i = 0; i <= windowPoints; ++i)
    {
        double x;
        switch (sym)
        {
            case GrainSymmetry::fall: x = 0.5 * i + 256; break;
            case GrainSymmetry::rise: x = 512 - 0.5 * i; break;
            case GrainSymmetry::noiz: x = noiz[(size_t) i] + 256; break;
            case GrainSymmetry::std:
            default:                  x = i; break;
        }
        const double y = std::clamp (x, 0.0, 513.0);
        const double u = y <= 256 ? y - 128 : 385 - y;
        const double v = std::clamp (u * F + 128, 0.0, 256.0);
        const double index = v * 16;
        const auto j = std::min ((size_t) index, size_t (4095));
        t[(size_t) i] = sine[j] + (sine[j + 1] - sine[j]) * (float) (index - (double) j);
    }
}

/** The window at phase 0..1, linear between points. */
inline float windowAt (const WindowTable& t, double phase) noexcept
{
    const double x = (phase - std::floor (phase)) * windowPoints;
    const auto i = std::min ((size_t) x, (size_t) windowPoints - 1);
    return t[i] + (t[i + 1] - t[i]) * (float) (x - (double) i);
}

/** SPRAY: a grain's offset in ms from two uniform numbers in [0, 1). */
inline double sprayOffset (float spray, float slope, SpraySign sign, float u1, float u2) noexcept
{
    if (spray <= 0) return 0;
    const double mag = std::pow (std::abs (2.0 * u1 - 1.0), (double) std::max (1.0f, slope));
    const double side = sign == SpraySign::right ? 1.0 : sign == SpraySign::left ? -1.0 : u2 < 0.5f ? -1.0 : 1.0;
    return spray * mag * side;
}

/** T<RND: a grain's rate factor, 1 ± 0.5 · amount². */
inline double tuneRndRatio (float amount, float u) noexcept { return 1.0 + 0.5 * amount * amount * (2.0 * u - 1.0); }

/** A grain's level: FLUX (random, made up by 6 dB × amount) or VOID (√amount of them dropped to VOID⁴). */
inline float grainLevel (GrainAmpMode mode, float amount, float voidLevel, float u) noexcept
{
    if (amount <= 0) return 1.0f;
    const double n = 2.0 * u - 1.0;
    if (mode == GrainAmpMode::drop)
        return n < 2.0 * std::sqrt ((double) amount) - 1.0 ? (float) std::pow ((double) voidLevel, 4.0) : 1.0f;
    const double g = 1.0 - amount * std::abs (n);
    return (float) (g * g * std::pow (10.0, 6.0 * amount / 20.0));
}

/** SCAN's speed, as the share of its distance it travels a second: 100 / S.TIME of real time, × `speed`. */
inline double scanRate (float timePct, float dist, double clipLen, double speed = 1) noexcept
{
    const double d = dist * clipLen;
    return d > 0 ? speed * (100.0 / timePct) / d : 0.0;
}

/** How far SCAN has got, as a share of its distance: held at either end, or (S.LOOP) around again. */
inline double scanWrap (double x, bool loop) noexcept { return loop ? x - std::floor (x) : std::clamp (x, 0.0, 1.0); }

/** SCAN's offset from POS (seconds), `t` seconds after the note; S.LOOP starts it over, S.KEY's ratio is `speed`. */
inline double scanOffset (double t, float timePct, float dist, float curve, double clipLen,
                          bool loop = false, double speed = 1) noexcept
{
    const double d = dist * clipLen;
    if (d <= 0) return 0;
    return d * std::pow (scanWrap (t * scanRate (timePct, dist, clipLen, speed), loop), (double) curve);
}

// ------------------------------------------------------------------------- Granulator III (grainMath.ts)

constexpr int densityMin = 2, densityMax = 20;   // CLOUD's grains a side

/** SKEW as an exponent on a CLASSIC window's phase: (9.9 s / (10 − 9 s) + 0.1)^1.5, 1 at 0.5. */
inline double skewExponent (float skew) noexcept
{
    const double s = std::clamp ((double) skew, 0.0, 1.0);
    return std::pow (9.9 * s / (10.0 - 9.0 * s) + 0.1, 1.5);
}

/** A window's phase (0..1) skewed by exponent `e`. */
inline double skewPhase (double phase, double e) noexcept { return e == 1.0 || phase <= 0 ? phase : std::pow (phase, e); }

/** CLOUD: a side's grain `i` lasts 1 + √i grains. */
inline double cloudLength (int i) noexcept { return 1.0 + std::sqrt ((double) i); }

/** CLOUD's level for `n` grains a side: 0.9 · 2^(1.0573 / n). */
inline float cloudGain (int n) noexcept { return (float) (0.9 * std::pow (2.0, 1.0573 / n)); }

/** VARIATION on a grain's level: VOL dB × amount × (n − 0.75), n = 2u − 1. */
inline float varGain (float db, float amount, float u) noexcept
{
    if (db <= 0 || amount <= 0) return 1.0f;
    return (float) std::pow (10.0, db * amount * (2.0 * u - 1.0 - 0.75) / 20.0);
}

/** VARIATION on a grain's length, as a factor: up to ±SIZE semitones × amount. */
inline double varSizeRatio (float semis, float amount, float u) noexcept
{
    return semis > 0 && amount > 0 ? std::pow (2.0, (2.0 * u - 1.0) * semis * amount / 12.0) : 1.0;
}

/** VARIATION on a CLOUD grain's pitch, as a rate factor: 1 + amount · (2^(±PITCH / 12) − 1). */
inline double varPitchRatio (float semis, float amount, float u) noexcept
{
    return semis > 0 && amount > 0 ? 1.0 + amount * (std::pow (2.0, (2.0 * u - 1.0) * semis / 12.0) - 1.0) : 1.0;
}

/** VARIATION on where a grain starts, seconds, always later: |2u − 1| · amount² · ms. */
inline double varPosOffset (float ms, float amount, float u) noexcept
{
    return ms > 0 && amount > 0 ? std::abs (2.0 * u - 1.0) * amount * amount * ms / 1000.0 : 0.0;
}

/** A LOOP segment's crossfade, frames: SKEW of its span, no longer than the outgoing head's room. */
inline double loopFade (float skew, double span, double room) noexcept
{
    return std::min (std::max (0.0, skew * span), std::max (0.0, room));
}

/** Where a LOOP segment goes (grainMath.ts loopSegment), frames. */
struct LoopSegment { double head, end, room; };
inline LoopSegment loopSegment (double at, double span, double fade, double lo, double hi, int dir) noexcept
{
    const double len = std::min (span, hi - lo);
    if (dir > 0)
    {
        const double head = std::max (lo, std::min (at, hi - fade - len));
        return { head, head + len, hi - (head + len) };
    }
    const double start = std::max (lo, std::min (std::max (at, lo + fade), hi - len));
    return { start + len, start, start - lo };
}

/** 4-point, 3rd-order Hermite interpolation: `f` (0..1) of the way from y0 to y1. */
inline float hermite (float ym1, float y0, float y1, float y2, float f) noexcept
{
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
}

/** `x` at frame position `p`, silent outside [lo, hi) (grainMath.ts readAt). */
inline float readAt (const float* x, double p, long lo, long hi) noexcept
{
    const double fl = std::floor (p);
    const auto i0 = (long) fl;
    if (i0 < lo || i0 + 1 >= hi) return 0.0f;
    const auto f = (float) (p - fl);
    const auto k = (size_t) i0;
    if (i0 - 1 >= lo && i0 + 2 < hi) return hermite (x[k - 1], x[k], x[k + 1], x[k + 2], f);
    return x[k] + (x[k + 1] - x[k]) * f;
}

/** STRETCH's window: Hann at sample i of n, at sample centres. */
inline float hann (int i, int n) noexcept
{
    if (n < 2) return 1.0f;
    const double x = std::clamp ((i + 0.5) / n, 0.0, 1.0);
    return (float) (0.5 - 0.5 * std::cos (2 * pi * x));
}

// ------------------------------------------------------------------------------------ panning

/** StereoPannerNode: a mono source is panned equal-power; a stereo source keeps both channels
    and folds the far one into the near one as it moves. */
inline void pan (float pan, bool monoSource, float inL, float inR, float& outL, float& outR) noexcept
{
    const float p = std::clamp (pan, -1.0f, 1.0f);
    if (monoSource)
    {
        const double x = (p + 1.0) * 0.5;
        outL = (float) (inL * std::cos (x * pi / 2));
        outR = (float) (inL * std::sin (x * pi / 2));
        return;
    }
    if (p <= 0)
    {
        const double x = p + 1.0;
        outL = (float) (inL + inR * std::cos (x * pi / 2));
        outR = (float) (inR * std::sin (x * pi / 2));
    }
    else
    {
        const double x = p;
        outL = (float) (inL * std::cos (x * pi / 2));
        outR = (float) (inR + inL * std::sin (x * pi / 2));
    }
}

/** Cached coefficients for an unchanged pan position. The algebra and double
    intermediates match pan(), including Web Audio's stereo folding law. */
struct PreparedPan
{
    bool ready=false,mono=false;float position=0;double cosine=0,sine=1;
    void set(float value,bool monoSource) noexcept {
        value=std::clamp(value,-1.f,1.f);
        if(ready && value==position && mono==monoSource)return;
        ready=true;position=value;mono=monoSource;
        const double x=mono?(position+1.0)*.5:position<=0?position+1.0:double(position);
        cosine=std::cos(x*pi/2);sine=std::sin(x*pi/2);
    }
    void process(float left,float right,float& outLeft,float& outRight) const noexcept {
        if(mono){outLeft=float(left*cosine);outRight=float(left*sine);}
        else if(position<=0){outLeft=float(left+right*cosine);outRight=float(right*sine);}
        else{outLeft=float(left*cosine);outRight=float(right+left*sine);}
    }
};

// ------------------------------------------------------------------------------------ delay

/** A delay line with a fractional (linearly interpolated) read, like DelayNode. */
class DelayLine
{
public:
    void prepare (double sampleRate, double maxSeconds)
    {
        buffer.assign ((size_t) std::ceil (sampleRate * maxSeconds) + 4, 0.0f);
        write = 0;
    }
    void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    /** The sample `delaySamples` ago (then push `x`). */
    float process (float x, double delaySamples) noexcept
    {
        const auto n = buffer.size();
        const double d = std::clamp (delaySamples, 1.0, (double) n - 2);
        double r = (double) write - d;
        while (r < 0)
            r += (double) n;
        const auto i0 = (size_t) r;
        const auto i1 = (i0 + 1) % n;
        const auto frac = (float) (r - (double) i0);
        const float y = buffer[i0] + (buffer[i1] - buffer[i0]) * frac;
        buffer[write] = x;
        write = (write + 1) % n;
        return y;
    }

private:
    std::vector<float> buffer;
    size_t write { 0 };
};

// ------------------------------------------------------------------------------------ reverb IR

/** getImpulse (audio/impulse.ts): stereo decaying noise, `size` seconds long with a (1 - t)^decay
    envelope. Rounded the same way, so equal settings share one impulse. */
inline std::vector<std::vector<float>> makeImpulse (double sampleRate, float size, float decay, uint32_t seed = 12345)
{
    const double s = std::max (0.1, std::round (size * 20.0) / 20.0);
    const double d = std::max (0.5, std::round (decay * 4.0) / 4.0);
    const auto len = (size_t) std::floor (sampleRate * s);
    std::vector<std::vector<float>> ir (2, std::vector<float> (len));
    uint32_t rng = seed;
    for (auto& ch : ir)
        for (size_t i = 0; i < len; ++i)
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            const double noise = (rng & 0xffffff) / (double) 0x800000 - 1.0;
            ch[i] = (float) (noise * std::pow (1.0 - (double) i / (double) len, d));
        }
    return ir;
}

/** The (size, decay) key an impulse is shared under. */
inline uint32_t impulseKey (float size, float decay) noexcept
{
    const auto s = (uint32_t) std::max (2.0, std::round (size * 20.0));
    const auto d = (uint32_t) std::max (2.0, std::round (decay * 4.0));
    return (s << 12) | d;
}

} // namespace ssb::dsp
