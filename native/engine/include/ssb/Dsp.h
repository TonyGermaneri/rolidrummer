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

/** A running LFO (audio/lfo.ts). Random shapes draw a new value at every whole cycle: `random`
    jumps to it (sample & hold), `smooth` ramps to it across the cycle before. */
class Lfo
{
public:
    void start (const LfoDef& def, double bpm, uint32_t seed) noexcept
    {
        shape = def.shape;
        hz = std::max (0.001, def.hz (bpm));
        phase = 0;
        rng = seed ? seed : 0x9e3779b9u;
        prev = 0;
        next = nextRandom();
    }

    /** Follow a changed shape or rate, keeping phase continuous. */
    void set (const LfoDef& def, double bpm) noexcept
    {
        hz = std::max (0.001, def.hz (bpm));
        shape = def.shape;
    }

    void restart() noexcept { phase = 0; }

    /** Jump to an absolute phase (cycles), e.g. the host's position / the LFO's division, so a
        synced LFO stays locked to the song. Random shapes step when a cycle boundary is crossed. */
    void syncTo (double cycles) noexcept
    {
        if (std::floor (cycles) != std::floor (phase))
        {
            prev = shape == LfoShape::random ? nextRandom() : next;
            next = nextRandom();
        }
        phase = cycles;
    }

    float value() const noexcept
    {
        if (shape == LfoShape::random)
            return prev;
        if (shape == LfoShape::smooth)
            return (float) (prev + (next - prev) * (phase - std::floor (phase)));
        return lfoValue (shape, phase);
    }

    void advance (double seconds) noexcept
    {
        const double before = std::floor (phase);
        phase += seconds * hz;
        const auto crossed = (long) (std::floor (phase) - before);
        for (long i = 0; i < std::min (crossed, 4L); ++i)
        {
            // S&H lands on the value it will hold; smooth has ramped into it.
            prev = shape == LfoShape::random ? nextRandom() : next;
            next = nextRandom();
        }
        if (phase > 1e6)
            phase -= std::floor (phase);
    }

private:
    float nextRandom() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) ((rng & 0xffffff) / (double) 0x800000 - 1.0);
    }

    LfoShape shape { LfoShape::sine };
    double hz { 1 }, phase { 0 };
    uint32_t rng { 1 };
    float prev { 0 }, next { 0 };
};

// ------------------------------------------------------------------------------------ grains
// src/audio/grainMath.ts, line for line: the page's grain worklet and this engine make the same cloud.

constexpr double stretchGrain = 0.09;   // seconds: STRETCH's grains
constexpr int stretchOverlap = 4;

/** Tukey window at sample i of n: taper 0 = square .. 1 = Hann, sampled at sample centres (so a
    one-sample grain plays). */
inline float grainWindow (int i, int n, float taper) noexcept
{
    if (taper <= 0 || n < 2) return 1.0f;
    const double x = (i + 0.5) / n;
    const double edge = taper / 2.0;
    if (x < edge) return (float) (0.5 - 0.5 * std::cos (pi * x / edge));
    if (x > 1 - edge) return (float) (0.5 - 0.5 * std::cos (pi * (1 - x) / edge));
    return 1.0f;
}

/** The same Tukey/Hann envelope using a table prepared off the audio thread.
    Interpolation error is below float-scale audio precision, while avoiding
    a cosine for every sample of every overlapping grain. */
struct GrainWindowTable
{
    static constexpr size_t resolution = 8192;
    std::array<float, resolution + 1> values {};
    GrainWindowTable() {
        for(size_t i=0;i<=resolution;++i)values[i]=float(.5-.5*std::cos(pi*double(i)/resolution));
    }
    float at(int sample,int length,float taper) const noexcept {
        if(taper<=0 || length<2)return 1.f;
        return atPhase((sample+.5)/length,2.0/taper);
    }
    float atPhase(double phase,double inverseEdge) const noexcept {
        if(inverseEdge<=0)return 1.f;
        const double normalized=std::min(phase,1-phase)*inverseEdge;
        if(normalized>=1)return 1.f;
        const double position=std::max(0.,normalized)*resolution;
        const size_t index=std::min(size_t(position),resolution-1);
        return values[index]+(values[index+1]-values[index])*float(position-double(index));
    }
};

/** Seconds to the next grain at `rate` a second: steady at scatter 0, exponential (Poisson) at 1,
    the same average. u uniform in [0, 1). */
inline double grainInterval (float rate, float scatter, float u) noexcept
{
    const double mean = 1.0 / std::max (1e-3f, rate);
    if (scatter <= 0) return mean;
    return mean * (1.0 - scatter + scatter * -std::log (1.0 - std::min ((double) u, 0.999999)));
}

/** Each grain's level when `overlap` sound at once on average: full when sparse, 1/sqrt when dense. */
inline float grainGain (float overlap) noexcept
{
    return std::min (1.0f, std::sqrt (2.0f / std::max (1e-6f, overlap)));
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
