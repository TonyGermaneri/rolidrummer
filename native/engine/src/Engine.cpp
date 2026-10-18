#include <ssb/Engine.h>
#include <ssb/Parameters.h>
#include <ssb/ContactEffects.h>

#include <juce_dsp/juce_dsp.h>

#include <cstring>
#include <limits>

namespace ssb
{

namespace
{
constexpr double inf = std::numeric_limits<double>::infinity();
constexpr int chunk = 32;          // samples between MIDI / command checks
constexpr int controlEvery = 16;   // samples between control-rate updates (LFOs, coefficients)
constexpr int delayBuses = 8;
constexpr int reverbSlots = 6;
constexpr float chorusBase = 0.018f, chorusMaxDepth = 0.008f;

/** A fixed-capacity single-producer / single-consumer ring. */
template <typename T, size_t N>
class Ring
{
public:
    size_t available() const noexcept { return N - (head.load(std::memory_order_relaxed) - tail.load(std::memory_order_acquire)); }
    bool push (T&& v) noexcept
    {
        const auto h = head.load (std::memory_order_relaxed);
        if (h - tail.load (std::memory_order_acquire) >= N)
            return false;
        items[h % N] = std::move (v);
        head.store (h + 1, std::memory_order_release);
        return true;
    }
    bool pop (T& out) noexcept
    {
        const auto t = tail.load (std::memory_order_relaxed);
        if (t == head.load (std::memory_order_acquire))
            return false;
        out = std::move (items[t % N]);
        tail.store (t + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, N> items {};
    std::atomic<size_t> head { 0 }, tail { 0 };
};

bool sameSound (const SoundPtr& a, const SoundPtr& b) noexcept
{
    return a && b && (a == b || a->id == b->id);
}

} // namespace

uint64_t idHash (std::string_view id) noexcept
{
    uint64_t h = 1469598103934665603ull;
    for (const char c : id)
    {
        h ^= (uint8_t) c;
        h *= 1099511628211ull;
    }
    return h;
}

Command Command::make (Type t, std::string_view soundId, float vel)
{
    Command c;
    c.type = t;
    c.velocity = vel;
    const auto n = std::min (soundId.size(), sizeof (c.id) - 1);
    std::memcpy (c.id, soundId.data(), n);
    c.id[n] = 0;
    return c;
}

float convolverNormalisation (const std::vector<std::vector<float>>& ir, double sampleRate) noexcept
{
    constexpr double gainCalibration = 0.00125, calibrationRate = 44100, minPower = 0.000125;
    double sum = 0;
    size_t count = 0;
    for (const auto& ch : ir)
    {
        for (float x : ch)
            sum += (double) x * x;
        count += ch.size();
    }
    double power = count ? std::sqrt (sum / (double) count) : 0.0;
    if (! std::isfinite (power) || power < minPower)
        power = minPower;
    double scale = 1.0 / power * gainCalibration;
    if (sampleRate > 0)
        scale *= calibrationRate / sampleRate;
    if (ir.size() == 4)
        scale *= 0.5;
    return (float) scale;
}

// =================================================================================== voice

struct Engine::Voice
{
    // identity
    bool alive { false };
    uint32_t generation { 0 };
    uint32_t order { 0 };            // start order, for stealing
    SoundPtr sound;                  // re-pointed when the page updates the sound
    SamplePtr sample;
    int zone { -1 };                 // index into sound->zones
    SoundPtr group;                  // the patch sound this plays for, when a VCO
    bool silent { false };           // a mod-only VCO: heard through its tap only
    std::array<int16_t, 3> taps { { -1, -1, -1 } };
    std::array<uint32_t, 3> tapGen {};

    uint64_t owner { 0 }, eventToken { 0 };
    std::array<float,5> expression {};
    std::array<float,16> smoothed {};
    Settings effective;

    // performance
    float velocity { 1 }, level { 1 };
    int midiNote { -1 }, channel { -1 };
    float pressure { 0 }, bend { 0 }, noteBend { 0 }, timbre { 0 };

    // time, in seconds since note-on
    double t { 0 };
    double releaseT { inf };         // release starts
    double endT { inf };             // the source stops
    double killT { inf };            // hard stop fade starts
    double killFade { 0.03 };

    // playback, in frames of the sample
    double pos { 0 }, clipIn { 0 }, clipOut { 0 };
    double iterations { 0 }, lastExpressionT { 0 };
    bool contactReleased { false }, contactEnded { false };
    bool loops { false };
    double loopStart { 0 }, loopEnd { 0 };
    bool oneShot { false };
    float kRate { 1 }, speed { 1 };
    float glideFrom { 0 }, glideTo { 0 }, glideDur { 0 }, glideStart { 0 };
    bool monoSource { false };

    // zone, resolved for this note
    float zoneGain { 1 }, zonePan { 0 };
    bool zoneFilter { false };
    FilterType zfType { FilterType::lowpass };
    float zfFreq { 20000 }, zfRes { 0 };
    float ccGain { 1 }, ccPan { 0 }, ccRes { 0 }, ccPitch { 0 }, ccCut { 0 };

    dsp::Env ampEnv, filtEnv;
    float filtPeak { 0 };            // cents

    // DSP state
    std::array<dsp::Biquad, 2> filt, lo, mid, hi;
    dsp::BiquadCoefs fc, loC, midC, hiC;
    dsp::PreparedPan outputPan;
    float eqKey[3] { 1e9f, 1e9f, 1e9f };
    std::array<dsp::Lfo, 2> padLfo;

    // modulation routes, flattened from the global and pad matrices
    struct Route
    {
        bool pad { false };
        ModSource source { ModSource::lfo1 };
        ModDest dest { ModDest::pitch };
        float gain { 0 };
    };
    std::array<Route, 32> routes {};
    int routeCount { 0 };
    float tremBase { 1 };
    bool vcoRoutes { false };
    const ModMatrix* routeFrom[2] { nullptr, nullptr };   // what `routes` was built from

    // control-rate values (refreshed every `controlEvery` samples)
    float cPitch { 0 }, cCut { 0 }, cRes { 0 }, cTrem { 1 }, cPan { 0 }, cDelay { 0 }, cReverb { 0 };
    int countdown { 0 };

    // grains (voice.ts): 'cloud' is Granulator II's voice (grains.worklet.ts renderCloud), 'stretch' walks
    // grains through the clip at SPEED
    enum class Kind : uint8_t { sample, stretch, cloud };
    Kind kind { Kind::sample };
    // cloud: a phasor per side (left, right) at GRAIN; each runs two slots, A from the wrap and B from half a
    // period later, windowed by the phasor's phase, so a grain lasts one period and two overlap
    struct Slot { bool on { false }; double pos { 0 }; double step { 1 }; float amp { 1 }; };
    std::array<Slot, 4> slots {};          // left A, left B, right A, right B
    std::array<double, 2> phase {};
    bool cloudStarted { false };
    float rndFrom { 0 }, rndTo { 0 };
    double rndPhase { 1 };                 // G<RND (rand~): a line to a new target 8 × GRAIN times a second
    double fmPhase { 0 };
    dsp::WindowTable window {};
    float windowShape { -1 };
    GrainSymmetry windowSym { GrainSymmetry::std };
    // stretch: steady grains, four overlapping
    struct Grain
    {
        double pos { 0 };        // read position, frames
        double step { 1 };       // frames per output sample at the voice's base pitch
        float k { 1 };           // the voice's kRate when it started: sounding grains follow PITCH / FINE
        int length { 0 }, index { 0 };
    };
    std::array<Grain, 64> grainPool {};    // the sounding ones are [0, grainCount)
    int grainCount { 0 };
    double nextStretch { 0 };
    float cGrainPos { 0 }, cGrainSize { 0 };

    // VCO -> pitch, held for Web Audio's 128-sample render quantum: in the page it drives an
    // AudioBufferSourceNode's detune, which is a k-rate parameter, so that is the FM the page makes
    float quantumPitch { 0 };
    int quantumLeft { 0 };

    // this voice's post-EQ mono signal for the current chunk, when a patch reads it as a VCO
    std::vector<float> tap;

    const Settings& s() const noexcept { return owner ? effective : sound->s; }
    const Zone* z() const noexcept { return zone >= 0 && sound && zone < (int) sound->zones.size() ? &sound->zones[(size_t) zone] : nullptr; }
    bool held() const noexcept { return alive && t < releaseT && t < killT; }

    float semisAt (double time) const noexcept
    {
        if (glideDur <= 0 || time >= glideStart + glideDur)
            return glideTo;
        if (time <= glideStart)
            return glideFrom;
        return glideFrom + (glideTo - glideFrom) * (float) ((time - glideStart) / glideDur);
    }
};

// =================================================================================== engine state

struct Engine::Impl
{
    Engine& engine;
    explicit Impl (Engine& e) : engine (e) {}

    // ---- threads ----
    std::atomic<PerformancePtr*> incoming { nullptr };
    Ring<Command, 1024> commands;
    Ring<std::shared_ptr<const void>, 8192> trash;
    Ring<PerformancePtr*, 64> trashHolders;

    PerformancePtr perf;
    std::array<Voice, maxVoices> voices;
    uint32_t nextOrder { 1 };
    int maxBlock { 512 };

    // ---- controllers ----
    std::array<float, 128> cc {};
    float modWheel { 0 }, bend { 0 }, pressure { 0 }, timbre { 0 };
    bool sustain { false };
    double now { 0 };               // seconds since prepare
    double bpm { 120 };
    std::array<dsp::Lfo, 2> globalLfo;

    // ---- note bookkeeping (board.ts) ----
    struct VoiceRef { int16_t index { -1 }; uint32_t generation { 0 }; };
    struct VoiceSet
    {
        std::array<VoiceRef, 24> refs {};
        int count { 0 };
        void clear() noexcept { count = 0; }
        void add (int index, uint32_t gen) noexcept
        {
            if (count < (int) refs.size())
                refs[(size_t) count++] = { (int16_t) index, gen };
        }
    };
    std::array<VoiceSet, 16 * 128> poly;     // key: channel * 128 + note (channel 0 when not MPE)
    std::array<bool, 16 * 128> sustainedKeys {};
    VoiceSet mono;
    bool monoSustained { false };
    std::array<int8_t, 128> heldOrder {};     // held notes, oldest first
    int heldCount { 0 };
    int lastNote { -1 };

    struct MpeState { float bend { 0 }, pressure { 0 }, timbre { 0 }; };
    std::array<MpeState, 16> mpeCh {};

    // pads pressed from the editor, and pads whose release the sustain pedal is holding back
    std::array<SoundPtr, 64> pressed {}, sustainedPads {};

    // SFZ round robin counters and note-on times (for release triggers), by sound
    struct Counter { const void* key { nullptr }; uint32_t n { 0 }; };
    std::array<Counter, 256> roundRobin {};
    struct NoteStart { const void* key { nullptr }; int note { -1 }; float velocity { 1 }; double at { 0 }; };
    std::array<NoteStart, 512> noteStarts {};
    uint32_t rng { 0x1234567u };

    // ---- buses ----
    std::vector<float> mainL, mainR;

    struct DelayBus
    {
        bool used { false };
        float time { 0 }, feedback { 0 };
        std::vector<float> inL, inR;
        dsp::DelayLine lineL, lineR;
        float lastL { 0 }, lastR { 0 };
        double idle { 0 };
        bool fed { false };
    };
    std::array<DelayBus, delayBuses> delays;
    ContactEffects contactEffects;

    struct ReverbSlot
    {
        std::atomic<uint32_t> key { 0 };       // set by the message thread once the IR is loading
        std::unique_ptr<juce::dsp::Convolution> conv;
        std::vector<float> inL, inR;
        bool fed { false };
        double idle { 0 };
    };
    std::array<ReverbSlot, reverbSlots> reverbs;
    uint32_t loadedKeys[reverbSlots] {};       // message thread's view

    // master effects (masterFx.ts)
    std::array<dsp::DelayLine, 2> chorusLine;
    dsp::Lfo chorusLfo[2];
    std::array<dsp::DelayLine, 2> masterDelay;
    std::array<dsp::Biquad, 2> damp;
    dsp::BiquadCoefs dampC;
    float masterFbL { 0 }, masterFbR { 0 };
    std::array<dsp::Biquad,2> masterFilter;
    dsp::BiquadCoefs masterFilterC;
    std::array<float,5> masterControls {{0,0,0,1, (.7f-.1f)/19.9f}};
    int masterFilterTick = 0;
    std::unique_ptr<juce::dsp::Convolution> masterReverb;
    uint32_t masterReverbKey { 0 };
    std::vector<float> wetL, wetR;
    float peakL { 0 }, peakR { 0 };

    // published voice views (seqlock: odd = being written)
    std::atomic<uint32_t> viewSeq { 0 };
    std::array<VoiceView, maxVoices> views {};
    std::atomic<int> viewCount { 0 };
    double viewTime { 0 };
    Ring<GrainView, 2048> grainViews;   // grains started, for the editor (audio thread -> readGrains)

    void publishViews() noexcept
    {
        viewSeq.fetch_add (1, std::memory_order_acq_rel);
        int n = 0;
        for (const auto& v : voices)
        {
            if (! v.alive || ! v.sound || ! v.sample) continue;
            auto& w = views[(size_t) n++];
            const double sr = v.sample->rate;
            w.id = v.order;
            w.sound = idHash (v.sound->id);
            w.group = v.group ? idHash (v.group->id) : 0;
            w.midiNote = v.midiNote;
            w.velocity = v.velocity;
            w.age = (float) v.t;
            w.end = std::isfinite (v.endT) ? (float) v.endT : -1.0f;
            w.position = (float) (v.pos / sr);
            w.rate = (float) (v.kRate * dsp::semisToRate (v.semisAt (v.t) + v.cPitch / 100.0f) * v.speed);
            w.duration = (float) v.sample->duration();
            w.clipIn = (float) (v.clipIn / sr);
            w.clipOut = (float) (v.clipOut / sr);
            w.loops = v.loops;
            w.grains = v.kind == Voice::Kind::cloud;
        }
        viewCount.store (n, std::memory_order_relaxed);
        viewTime = now;
        viewSeq.fetch_add (1, std::memory_order_acq_rel);
    }

    // =============================================================================== helpers

    // Stop producing new references before the retirement ring fills. The reserve
    // covers every live voice's three references plus pressed/sustain references.
    bool canRetireMore() const noexcept { return trash.available() >= 512; }
    bool pinned(const SoundPtr& sound) const noexcept {
        if(!sound || !perf)return false;
        const auto it=perf->byId.find(sound->id);
        return it!=perf->byId.end() && it->second==sound;
    }
    template<typename T> void discard(std::shared_ptr<T>&& reference) noexcept
    {
        if(!reference)return;
        if constexpr(std::is_same_v<std::remove_const_t<T>,Sound>) {
            // The audio-owned immutable performance guarantees another strong
            // reference throughout reset: no destructor can run here.
            if(pinned(reference)){reference.reset();return;}
        }
        if(trash.available()==0)return;
        std::shared_ptr<const void> erased=std::move(reference);
        const bool pushed=trash.push(std::move(erased));
        jassert(pushed);juce::ignoreUnused(pushed);
    }

    void kill (Voice& v) noexcept
    {
        v.alive = false;
        if(pinned(v.sound) && (v.sample==v.sound->sample || (v.z() && v.sample==v.z()->sample)))v.sample.reset();
        discard (std::move (v.sound));
        discard (std::move (v.sample));
        discard (std::move (v.group));
    }

    float random01() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) ((rng & 0xffffff) / (double) 0x1000000);
    }

    uint32_t& counterFor (const void* key) noexcept
    {
        auto h = (size_t) (reinterpret_cast<uintptr_t> (key) >> 4) % roundRobin.size();
        for (size_t i = 0; i < roundRobin.size(); ++i, h = (h + 1) % roundRobin.size())
            if (roundRobin[h].key == key || roundRobin[h].key == nullptr)
            {
                roundRobin[h].key = key;
                return roundRobin[h].n;
            }
        return roundRobin[0].n;
    }

    bool ofSound (const Voice& v, const SoundPtr& s) const noexcept
    {
        return v.alive && (sameSound (v.sound, s) || sameSound (v.group, s));
    }

    // =============================================================================== voices

    int allocateVoice() noexcept
    {
        if(!canRetireMore())return -1;
        int best = -1;
        for (int i = 0; i < maxVoices; ++i)
            if (! voices[(size_t) i].alive) { kill(voices[(size_t)i]); return i; }
        // steal: the oldest released voice, else the oldest voice
        uint32_t oldest = std::numeric_limits<uint32_t>::max();
        for (int i = 0; i < maxVoices; ++i)
        {
            const auto& v = voices[(size_t) i];
            if (! v.held() && v.order < oldest) { oldest = v.order; best = i; }
        }
        if (best < 0)
            for (int i = 0; i < maxVoices; ++i)
                if (voices[(size_t) i].order < oldest) { oldest = voices[(size_t) i].order; best = i; }
        if (best >= 0)
            kill (voices[(size_t) best]);
        return best;
    }

    std::array<float,5> globalExpression {};

    struct Start
    {
        uint64_t owner { 0 }, token { 0 };
        std::array<float,5> expression {};
        float semis { 0 }, glideFrom { 0 }, glideTime { 0 };
        float velocity { 1 };
        int midiNote { -1 }, channel { -1 };
        float noteBend { 0 }, pressure { 0 }, timbre { 0 };
        bool hasMpe { false };
        SoundPtr group;
        float level { 1 }, detune { 0 };
        bool silent { false };
        std::array<int16_t, 3> taps { { -1, -1, -1 } };
        std::array<uint32_t, 3> tapGen {};
        // zone
        int zone { -1 };
        float extraDb { 0 };
        bool noLoop { false };
        float zoneFreq { 0 };
    };

    void buildRoutes (Voice& v) noexcept
    {
        v.routeCount = 0;
        v.tremBase = 1;
        v.vcoRoutes = false;
        v.routeFrom[0] = &perf->globalMod;
        v.routeFrom[1] = &v.sound->s.mod;
        for (int scope = 0; scope < 2; ++scope)
            for (const auto& r : v.routeFrom[scope]->routes)
            {
                if (r.amount == 0)
                    continue;
                if (v.routeCount >= (int) v.routes.size())
                    break;
                float gain = r.amount * modScale (r.dest)
                           * (r.dest == ModDest::pitch ? 100.0f : r.dest == ModDest::cutoff ? 1200.0f : 1.0f);
                if (r.dest == ModDest::volume && isBipolar (r.source))
                {
                    gain /= 2;
                    v.tremBase += gain;
                }
                if (r.source == ModSource::vco1 || r.source == ModSource::vco2 || r.source == ModSource::vco3)
                    v.vcoRoutes = true;
                v.routes[(size_t) v.routeCount++] = { scope == 1, r.source, r.dest, gain };
            }
    }

    int startVoice (const SoundPtr& sound, const Start& o) noexcept
    {
        const Zone* zone = o.zone >= 0 ? &sound->zones[(size_t) o.zone] : nullptr;
        const SamplePtr& sample = zone ? zone->sample : sound->sample;
        if (! sample || sample->frames() < 2)
            return -1;
        const int index = allocateVoice();
        if (index < 0)
            return -1;
        auto& v = voices[(size_t) index];
        v.owner = o.owner; v.eventToken = o.token; v.expression = o.expression;
        v.contactReleased = false; v.contactEnded = false; v.iterations = 0;
        v.alive = true;
        ++v.generation;
        v.order = nextOrder++;
        v.sound = sound;
        v.sample = sample;
        v.t=0;v.lastExpressionT=0;
        if(v.owner) mapExpression(v, true);
        const auto& s = v.s();
        v.zone = o.zone;
        v.group = o.group;
        v.silent = o.silent;
        v.taps = o.taps;
        v.tapGen = o.tapGen;
        v.velocity = o.velocity;
        v.level = o.level;
        v.midiNote = o.midiNote;
        v.channel = o.channel;
        v.pressure = o.hasMpe ? o.pressure : pressure;
        v.bend = bend;
        v.noteBend = o.hasMpe ? o.noteBend : 0;
        v.timbre = o.hasMpe ? o.timbre : timbre;
        v.t = 0;
        v.releaseT = v.endT = v.killT = inf;
        v.monoSource = sample->channels.size() < 2;

        const double dur = sample->duration();
        const double sr = sample->rate;
        if (zone)
        {
            const double in = std::min (std::max (0.0, (double) zone->offset), std::max (0.0, dur - 0.005));
            const double out = zone->end > in ? std::min ((double) zone->end, dur) : dur;
            v.clipIn = in * sr;
            v.clipOut = out * sr;
            const bool loop = ! o.noLoop && (zone->loopMode == LoopMode::loopContinuous || zone->loopMode == LoopMode::loopSustain)
                              && zone->loopEnd > zone->loopStart;
            v.loops = loop;
            v.loopStart = zone->loopStart * sr;
            v.loopEnd = std::min ((double) zone->loopEnd * sr, (double) sample->frames());
            v.oneShot = zone->loopMode == LoopMode::oneShot;
            v.zoneGain = dsp::dbToGain (zone->volume + o.extraDb);
            v.zonePan = zone->pan / 100.0f;
            v.zoneFilter = zone->filter.has_value();
            if (v.zoneFilter)
            {
                v.zfType = zone->filter->type;
                v.zfFreq = o.zoneFreq;
                v.zfRes = zone->filter->resonance;
            }
        }
        else
        {
            const double a = std::min (s.clipIn, s.clipOut) * dur, b = std::max (s.clipIn, s.clipOut) * dur;
            const double in = std::min (a, std::max (0.0, dur - 0.005));
            const double out = std::max (in + 0.005, std::min (b, dur));
            v.clipIn = in * sr;
            v.clipOut = out * sr;
            v.loops = true;                // pads loop the clip; the repeat count ends them
            v.loopStart = v.clipIn;
            v.loopEnd = v.clipOut;
            v.oneShot = false;
            v.zoneGain = 1;
            v.zonePan = 0;
            v.zoneFilter = false;
        }
        v.pos = v.clipIn;

        v.kRate = dsp::semisToRate (s.pitch + s.fine / 100.0);
        v.speed = s.speed;
        const float detune = o.detune / 100.0f;
        v.glideTo = o.semis + detune + (v.owner && !zone ? sound->s.rootNote-s.rootNote : 0);
        v.glideFrom = (o.glideTime > 0 ? o.glideFrom : o.semis) + detune;
        v.glideDur = o.glideTime;
        v.glideStart = 0;

        // an SFZ region's own envelope wins over the pad's ADSR, field by field
        v.ampEnv = { s.attack, s.decay, s.sustain, s.release };
        if (zone)
        {
            if (zone->env.a) v.ampEnv.a = *zone->env.a;
            if (zone->env.d) v.ampEnv.d = *zone->env.d;
            if (zone->env.s) v.ampEnv.s = *zone->env.s;
            if (zone->env.r) v.ampEnv.r = *zone->env.r;
        }
        v.filtEnv = { s.fAttack, s.fDecay, s.fSustain, s.fRelease };
        v.filtPeak = s.fEnvAmount * 1200.0f;
        if (zone && zone->filter && zone->filter->env)
        {
            const auto& e = *zone->filter->env;
            v.filtEnv = { e.a, e.d, e.s, e.r };
            v.filtPeak = e.depth;
        }

        v.kind = s.grain ? Voice::Kind::cloud : s.stretch ? Voice::Kind::stretch : Voice::Kind::sample;
        v.grainCount = 0;
        v.nextStretch = 0;
        v.cloudStarted = false;
        v.phase = {};
        for (auto& sl : v.slots) sl.on = false;
        v.rndFrom = v.rndTo = 0;
        v.rndPhase = 1;
        v.fmPhase = 0;
        v.windowShape = -1;
        // a cloud's sides are its own left and right grains
        if (v.kind == Voice::Kind::cloud)
            v.monoSource = false;

        // how long it plays: the repeat count for pads, the sample for zones (loops: until note-off)
        const double rate0 = v.kRate * dsp::semisToRate (v.glideTo) * v.speed * (sr / engine.rate);
        const double clipSeconds = (v.clipOut - v.clipIn) / sr;
        if (zone)
            v.endT = v.loops ? inf : (v.clipOut - v.clipIn) / (rate0 * engine.rate);
        else if (v.kind != Voice::Kind::sample)   // grains: only SPEED changes the length
            v.endT = s.repeat > 0 ? s.repeat * clipSeconds / v.speed : inf;
        else
            v.endT = s.repeat > 0 ? s.repeat * clipSeconds / (v.kRate * dsp::semisToRate (v.glideTo) * v.speed) : inf;
        if (std::isfinite (v.endT))
            v.releaseT = std::max (0.0, v.endT - v.ampEnv.r);

        for (auto& f : v.filt) f.reset();
        for (auto& f : v.lo) f.reset();
        for (auto& f : v.mid) f.reset();
        for (auto& f : v.hi) f.reset();
        v.eqKey[0] = v.eqKey[1] = v.eqKey[2] = 1e9f;
        v.padLfo[0].start (s.mod.lfo1, bpm, rng ^ 0xa5a5u);
        v.padLfo[1].start (s.mod.lfo2, bpm, rng ^ 0x5a5au);
        random01();
        buildRoutes (v);
        applyCc (v);
        v.countdown = 0;
        v.quantumLeft = 0;
        v.quantumPitch = 0;
        return index;
    }

    /** SFZ *_onccN offsets from the current CC values (Voice.applyCC). */
    void applyCc (Voice& v) noexcept
    {
        v.ccGain = 1; v.ccPan = 0; v.ccRes = 0; v.ccPitch = 0; v.ccCut = 0;
        const Zone* z = v.z();
        if (! z || z->ccMods.empty())
            return;
        float db = 0, amp = z->amplitude.value_or (100.0f) / 100.0f;
        for (const auto& m : z->ccMods)
        {
            const float x = cc[(size_t) (m.cc & 127)];
            switch (m.target)
            {
                case ZoneCcMod::Target::volume:    db += m.amount * x; break;
                case ZoneCcMod::Target::amplitude: amp *= m.amount / 100.0f * x; break;
                case ZoneCcMod::Target::pan:       v.ccPan += m.amount / 100.0f * x; break;
                case ZoneCcMod::Target::pitch:     v.ccPitch += m.amount * x; break;
                case ZoneCcMod::Target::cutoff:    v.ccCut += m.amount * x; break;
                case ZoneCcMod::Target::resonance: v.ccRes += m.amount * x; break;
            }
        }
        v.ccGain = dsp::dbToGain (db) * std::max (0.0f, amp);
    }

    void releaseVoice (Voice& v) noexcept
    {
        const Zone* z = v.z();
        if (z && z->loopMode == LoopMode::oneShot)
            return;   // one_shot regions ignore note-off
        if(v.owner && v.alive)v.contactReleased=true;
        if (! v.alive || v.t >= v.releaseT)
            return;
        v.contactReleased = true;
        v.releaseT = v.t;
        v.endT = std::min (v.endT, v.releaseT + v.ampEnv.releaseLength());
    }

    void stopVoice (Voice& v, double fade = 0.03) noexcept
    {
        if (! v.alive)
            return;
        v.contactReleased = true;
        v.killT = std::min (v.killT, v.t);
        v.killFade = fade;
        v.releaseT = std::min (v.releaseT, v.t);
    }

    void releaseSet (VoiceSet& set) noexcept
    {
        for (int i = 0; i < set.count; ++i)
        {
            const auto& r = set.refs[(size_t) i];
            auto& v = voices[(size_t) r.index];
            if (v.alive && v.generation == r.generation)
                releaseVoice (v);
        }
    }

    bool isHeld (const SoundPtr& s) const noexcept
    {
        for (const auto& v : voices)
            if (ofSound (v, s) && v.held())
                return true;
        return false;
    }
    void releaseSound (const SoundPtr& s) noexcept
    {
        for (auto& v : voices)
            if (ofSound (v, s))
                releaseVoice (v);
    }
    void stopSound (const SoundPtr& s, double fade = 0.03) noexcept
    {
        for (auto& v : voices)
            if (ofSound (v, s))
                stopVoice (v, fade);
    }

    // =============================================================================== notes (board.ts)

    float semisFor (const Sound& sound, const Zone* zone, float note) const noexcept
    {
        return zone ? zone->semisFor (note) : note - sound.s.rootNote;
    }

    bool zoneMatches (const Zone& z, int note, int vel127, uint32_t counter, float rand, bool release) const noexcept
    {
        if (z.releaseTrigger != release) return false;
        for (const auto& r : z.ccRange)
        {
            const int x = (int) std::lround (cc[(size_t) (r.cc & 127)] * 127.0f);
            if (x < r.lo || x > r.hi) return false;
        }
        return note >= z.lokey && note <= z.hikey && vel127 >= z.lovel && vel127 <= z.hivel
            && (z.seqLength <= 1 || (int) (counter % (uint32_t) z.seqLength) + 1 == z.seqPosition)
            && rand >= z.lorand && rand < z.hirand;
    }

    float zoneFilterFreq (const Zone& z, float note, float velocity) const noexcept
    {
        if (! z.filter) return 0;
        const auto& f = *z.filter;
        return f.cutoff * (float) std::pow (2.0, ((note - f.keycenter) * f.keytrack + velocity * f.veltrack) / 1200.0);
    }

    /** startOwn: the sound's own voice(s) -- several when SFZ regions layer. */
    int startOwn (const SoundPtr& sound, float note, float velocity, Start base, float fromNote, bool hasFrom,
                  int* out, int outCap) noexcept
    {
        int n = 0;
        if (sound->zones.empty())
        {
            base.semis = semisFor (*sound, nullptr, note);
            base.glideFrom = base.glideTime > 0 && hasFrom ? semisFor (*sound, nullptr, fromNote) : base.semis;
            base.velocity = velocity;
            const int i = startVoice (sound, base);
            if (i >= 0 && n < outCap) out[n++] = i;
            return n;
        }
        auto& counter = counterFor (sound.get());
        const uint32_t rr = counter++;
        rememberStart (sound.get(), (int) note, velocity);
        const float rand = random01();
        const int vel127 = std::clamp ((int) std::lround (velocity * 127.0f), 1, 127);
        for (int zi = 0; zi < (int) sound->zones.size(); ++zi)
        {
            const auto& z = sound->zones[(size_t) zi];
            if (! zoneMatches (z, (int) note, vel127, rr, rand, false))
                continue;
            Start o = base;
            o.zone = zi;
            o.semis = semisFor (*sound, &z, note);
            o.glideFrom = base.glideTime > 0 && hasFrom ? semisFor (*sound, &z, fromNote) : o.semis;
            o.velocity = velocity;
            o.zoneFreq = zoneFilterFreq (z, note, velocity);
            const int i = startVoice (sound, o);
            if (i >= 0 && n < outCap) out[n++] = i;
        }
        return n;
    }

    /** startNote: the sound's linked VCOs first (their signals feed its matrix), then its own. */
    int startNote (const SoundPtr& sound, float note, float velocity, Start base, float fromNote, bool hasFrom,
                   bool asVco, int* out, int outCap) noexcept
    {
        int n = 0;
        int vcoVoices[16];
        int vcoCount = 0;
        if (! asVco)
        {
            for (const auto& link : sound->links)
            {
                if (! link.osc || link.osc->id == sound->id)
                    continue;
                const auto& osc = *link.osc;
                const float vNote = (link.track ? note - sound->s.rootNote + osc.s.rootNote : (float) link.fixedNote) + link.transpose;
                const float vFrom = fromNote - sound->s.rootNote + osc.s.rootNote + link.transpose;
                Start o = base;
                o.group = sound;
                o.level = link.level;
                o.detune = link.fine;
                o.silent = ! link.audible;
                int started[8];
                const int k = startNote (link.osc, vNote, velocity, o, vFrom, link.track && hasFrom, true, started, 8);
                if (k > 0 && link.index >= 0 && link.index < 3)
                {
                    base.taps[(size_t) link.index] = (int16_t) started[0];
                    base.tapGen[(size_t) link.index] = voices[(size_t) started[0]].generation;
                }
                for (int i = 0; i < k && vcoCount < 16; ++i)
                    vcoVoices[vcoCount++] = started[i];
            }
        }
        n = startOwn (sound, note, velocity, base, fromNote, hasFrom, out, outCap);
        for (int i = 0; i < vcoCount && n < outCap; ++i)
            out[n++] = vcoVoices[i];
        return n;
    }

    void rememberStart (const void* key, int note, float velocity) noexcept
    {
        auto h = ((size_t) (reinterpret_cast<uintptr_t> (key) >> 4) * 131 + (size_t) note) % noteStarts.size();
        for (size_t i = 0; i < noteStarts.size(); ++i, h = (h + 1) % noteStarts.size())
            if (noteStarts[h].key == nullptr || (noteStarts[h].key == key && noteStarts[h].note == note))
            {
                noteStarts[h] = { key, note, velocity, now };
                return;
            }
    }

    bool takeStart (const void* key, int note, NoteStart& out) noexcept
    {
        auto h = ((size_t) (reinterpret_cast<uintptr_t> (key) >> 4) * 131 + (size_t) note) % noteStarts.size();
        for (size_t i = 0; i < noteStarts.size(); ++i, h = (h + 1) % noteStarts.size())
        {
            if (noteStarts[h].key == nullptr) return false;
            if (noteStarts[h].key == key && noteStarts[h].note == note)
            {
                out = noteStarts[h];
                // leave a tombstone so later probes keep walking
                noteStarts[h].note = -2;
                return true;
            }
        }
        return false;
    }

    /** Note-off for SFZ release regions: quieter the longer the note was held (rt_decay). */
    void triggerRelease (const SoundPtr& sound, int note) noexcept
    {
        if (! sound) return;
        bool any = false;
        for (const auto& z : sound->zones)
            any = any || z.releaseTrigger;
        if (! any) return;
        NoteStart st;
        const bool had = takeStart (sound.get(), note, st);
        const float velocity = had ? st.velocity : 1.0f;
        const double held = had ? now - st.at : 0.0;
        const uint32_t rr = counterFor (sound.get());
        const float rand = random01();
        const int vel127 = std::clamp ((int) std::lround (velocity * 127.0f), 1, 127);
        for (int zi = 0; zi < (int) sound->zones.size(); ++zi)
        {
            const auto& z = sound->zones[(size_t) zi];
            if (! zoneMatches (z, note, vel127, rr, rand, true))
                continue;
            Start o;
            o.zone = zi;
            o.semis = semisFor (*sound, &z, (float) note);
            o.glideFrom = o.semis;
            o.velocity = velocity;
            o.midiNote = note;
            o.extraDb = -z.rtDecay * (float) held;
            o.noLoop = true;   // release voices never get a note-off, so they must not loop
            o.zoneFreq = zoneFilterFreq (z, (float) note, velocity);
            startVoice (sound, o);
        }
    }

    void setPressed (std::array<SoundPtr, 64>& set, const SoundPtr& s, bool on) noexcept
    {
        if(on && !canRetireMore())return;
        for (auto& p : set)
            if (sameSound (p, s))
            {
                if (! on) discard (std::move (p));
                return;
            }
        if (on)
            for (auto& p : set)
                if (! p) { p = s; return; }
    }
    bool isIn (const std::array<SoundPtr, 64>& set, const SoundPtr& s) const noexcept
    {
        for (const auto& p : set)
            if (sameSound (p, s)) return true;
        return false;
    }

    void press (const SoundPtr& sound, float velocity) noexcept
    {
        if (! sound) return;
        setPressed (pressed, sound, true);
        const auto& s = sound->s;
        if (isHeld (sound))
        {
            if (s.mode == TriggerMode::stop) { releaseSound (sound); return; }
            if (s.mode != TriggerMode::stack) stopSound (sound);
        }
        if (s.choke > 0 && perf)
            for (const auto& other : perf->pads)
                if (other && other->id != sound->id && other->s.choke == s.choke)
                    stopSound (other);
        int out[32];
        startNote (sound, s.rootNote, velocity, Start {}, 0, false, false, out, 32);
    }

    void release (const SoundPtr& sound) noexcept
    {
        if (! sound) return;
        setPressed (pressed, sound, false);
        if (sound->s.mode != TriggerMode::hold) return;
        if (sustain) setPressed (sustainedPads, sound, true);
        else
        {
            releaseSound (sound);
            triggerRelease (sound, (int) sound->s.rootNote);
        }
    }

    bool isMember (int ch) const noexcept { return perf && perf->mpe && ch > 0; }
    int voiceKey (int note, int ch) const noexcept { return (isMember (ch) ? ch : 0) * 128 + (note & 127); }

    void holdNote (int note) noexcept
    {
        dropNote (note);
        if (heldCount < 128) heldOrder[(size_t) heldCount++] = (int8_t) note;
    }
    void dropNote (int note) noexcept
    {
        int w = 0;
        for (int i = 0; i < heldCount; ++i)
            if (heldOrder[(size_t) i] != note) heldOrder[(size_t) w++] = heldOrder[(size_t) i];
        heldCount = w;
    }

    void noteOn (int note, float velocity, int channel) noexcept
    {
        if (! perf || ! perf->playable) return;
        const auto& sound = perf->playable;
        holdNote (note);
        Start base;
        base.midiNote = note;
        base.glideTime = perf->glide;
        if (isMember (channel))
        {
            base.channel = channel;
            base.hasMpe = true;
            base.noteBend = mpeCh[(size_t) channel].bend;
            base.pressure = mpeCh[(size_t) channel].pressure;
            base.timbre = mpeCh[(size_t) channel].timbre;
        }
        const int from = lastNote;
        lastNote = note;
        int out[32];
        if (perf->mono && ! isMember (channel))
        {
            // legato: slide the held voice (single-sample sounds; SFZ zones retrigger)
            if (mono.count == 1 && sound->zones.empty())
            {
                auto& v = voices[(size_t) mono.refs[0].index];
                if (v.alive && v.generation == mono.refs[0].generation && v.held() && sameSound (v.sound, sound))
                {
                    v.glideFrom = v.semisAt (v.t);
                    v.glideTo = semisFor (*sound, nullptr, (float) note);
                    v.glideStart = (float) v.t;
                    v.glideDur = perf->glide;
                    v.midiNote = note;
                    return;
                }
            }
            releaseSet (mono);
            mono.clear();
            const int n = startNote (sound, (float) note, velocity, base, (float) from, from >= 0, false, out, 32);
            for (int i = 0; i < n; ++i) mono.add (out[i], voices[(size_t) out[i]].generation);
        }
        else
        {
            const int key = voiceKey (note, channel);
            releaseSet (poly[(size_t) key]);
            poly[(size_t) key].clear();
            sustainedKeys[(size_t) key] = false;
            const int n = startNote (sound, (float) note, velocity, base, (float) from, from >= 0, false, out, 32);
            for (int i = 0; i < n; ++i) poly[(size_t) key].add (out[i], voices[(size_t) out[i]].generation);
        }
    }

    void noteOff (int note, int channel) noexcept
    {
        dropNote (note);
        if (! perf) return;
        if (perf->mono && ! isMember (channel))
        {
            if (mono.count == 0) return;
            const auto& sound = perf->playable;
            if (heldCount == 0)
            {
                if (sustain) monoSustained = true;
                else
                {
                    releaseSet (mono);
                    mono.clear();
                    triggerRelease (sound, note);
                }
            }
            else if (sound)
            {
                const int top = heldOrder[(size_t) (heldCount - 1)];
                const auto& first = voices[(size_t) mono.refs[0].index];
                if (first.midiNote == top) return;
                const int from = lastNote;
                lastNote = top;
                if (mono.count == 1 && sound->zones.empty() && first.alive && first.generation == mono.refs[0].generation)
                {
                    auto& v = voices[(size_t) mono.refs[0].index];
                    v.glideFrom = v.semisAt (v.t);
                    v.glideTo = semisFor (*sound, nullptr, (float) top);
                    v.glideStart = (float) v.t;
                    v.glideDur = perf->glide;
                    v.midiNote = top;
                }
                else
                {
                    releaseSet (mono);
                    mono.clear();
                    Start base;
                    base.midiNote = top;
                    base.glideTime = perf->glide;
                    int out[32];
                    const int n = startNote (sound, (float) top, 1.0f, base, (float) from, from >= 0, false, out, 32);
                    for (int i = 0; i < n; ++i) mono.add (out[i], voices[(size_t) out[i]].generation);
                }
            }
        }
        else
        {
            const int key = voiceKey (note, channel);
            if (sustain) { sustainedKeys[(size_t) key] = true; return; }
            releaseSet (poly[(size_t) key]);
            poly[(size_t) key].clear();
            triggerRelease (perf->playable, note);
        }
    }

    void releaseSustained() noexcept
    {
        for (auto& p : sustainedPads)
        {
            if (! p) continue;
            if (! isIn (pressed, p))
            {
                releaseSound (p);
                triggerRelease (p, (int) p->s.rootNote);
            }
            discard (std::move (p));
        }
        for (int key = 0; key < (int) sustainedKeys.size(); ++key)
        {
            if (! sustainedKeys[(size_t) key]) continue;
            sustainedKeys[(size_t) key] = false;
            auto& set = poly[(size_t) key];
            int note = -1;
            if (set.count > 0 && voices[(size_t) set.refs[0].index].generation == set.refs[0].generation)
                note = voices[(size_t) set.refs[0].index].midiNote;
            releaseSet (set);
            set.clear();
            if (note >= 0 && perf) triggerRelease (perf->playable, note);
        }
        if (monoSustained && heldCount == 0)
        {
            releaseSet (mono);
            mono.clear();
        }
        monoSustained = false;
    }

    void allNotesOff() noexcept
    {
        heldCount = 0;
        for (auto& p : poly) p.clear();
        sustainedKeys.fill (false);
        mono.clear();
        monoSustained = false;
        mpeCh.fill ({});
        for (auto& p : sustainedPads) { discard (std::move (p)); }
    }

    void panic() noexcept
    {
        for (auto& v : voices)
            stopVoice (v, 0.02);
        for (auto& p : pressed) { discard (std::move (p)); }
        allNotesOff();
        bend = pressure = 0;
        flushTails();
    }

    void flushTails() noexcept
    {
        contactEffects.reset();
        for (auto& d : delays)
        {
            d.lineL.clear(); d.lineR.clear();
            d.lastL = d.lastR = 0;
        }
        for (auto& r : reverbs)
            if (r.conv) r.conv->reset();
        for (auto& l : masterDelay) l.clear();
        masterFbL = masterFbR = 0;
        if (masterReverb) masterReverb->reset();
        for (auto& f : masterFilter) f.reset();
        for (auto& l : chorusLine) l.clear();
    }

    // =============================================================================== MIDI

    void midi (uint8_t status, uint8_t d1, uint8_t d2) noexcept
    {
        switch (status)
        {
            case 0xfa: globalLfo[0].restart(); globalLfo[1].restart(); return;   // MIDI Start
            case 0xf8: case 0xfb: case 0xfc: return;
            default: break;
        }
        const int cmd = status & 0xf0, ch = status & 0x0f;
        switch (cmd)
        {
            case 0x90:
                if (d2 > 0) { noteOnMidi (d1, d2 / 127.0f, ch); return; }
                noteOffMidi (d1, ch);
                return;
            case 0x80: noteOffMidi (d1, ch); return;
            case 0xa0:
                for (auto& v : voices)
                    if (v.alive && v.midiNote == d1) v.pressure = d2 / 127.0f;
                return;
            case 0xd0:
            {
                const float x = d1 / 127.0f;
                if (isMember (ch))
                {
                    mpeCh[(size_t) ch].pressure = x;
                    for (auto& v : voices) if (v.alive && v.channel == ch) v.pressure = x;
                    return;
                }
                pressure = x;
                for (auto& v : voices) if (v.alive) v.pressure = x;
                return;
            }
            case 0xe0:
            {
                const int raw = (d2 << 7) | d1;
                const float x = raw >= 8192 ? (raw - 8192) / 8191.0f : (raw - 8192) / 8192.0f;
                if (isMember (ch))
                {
                    mpeCh[(size_t) ch].bend = x;
                    for (auto& v : voices) if (v.alive && v.channel == ch) v.noteBend = x;
                    return;
                }
                bend = x;
                for (auto& v : voices) if (v.alive) v.bend = x;
                return;
            }
            case 0xb0: controlChange (d1, d2 / 127.0f, ch); return;
            default: return;
        }
    }

    void noteOnMidi (int note, float velocity, int ch) noexcept
    {
        if (! perf) return;
        if (perf->play) { noteOn (note, velocity, ch); return; }
        for (const auto& [n, s] : perf->padNotes)
            if (n == note) press (s, velocity);
    }

    void noteOffMidi (int note, int ch) noexcept
    {
        if (! perf) return;
        if (perf->play) { noteOff (note, ch); return; }
        for (const auto& [n, s] : perf->padNotes)
            if (n == note) release (s);
    }

    void controlChange (int number, float x, int ch) noexcept
    {
        cc[(size_t) (number & 127)] = x;
        if (number == 1) modWheel = x;
        else if (number == 64)
        {
            const bool on = x >= 0.5f;
            if (sustain && ! on) { sustain = false; releaseSustained(); }
            sustain = on;
        }
        else if (number == 74)
        {
            if (isMember (ch))
            {
                mpeCh[(size_t) ch].timbre = x;
                for (auto& v : voices) if (v.alive && v.channel == ch) v.timbre = x;
            }
            else
            {
                timbre = x;
                for (auto& v : voices) if (v.alive) v.timbre = x;
            }
        }
        else if (number == 120) { panic(); return; }                        // all sound off
        else if (number == 123)                                              // all notes off
        {
            for (auto& v : voices) releaseVoice (v);
            allNotesOff();
            return;
        }
        for (auto& v : voices)
            if (v.alive) applyCc (v);
    }

    void command (const Command& c) noexcept
    {
        switch (c.type)
        {
            case Command::Type::press:   if (perf) press (perf->find (c.id), c.velocity); return;
            case Command::Type::release: if (perf) release (perf->find (c.id)); return;
            case Command::Type::noteOn:  noteOn (c.note, c.velocity, c.channel); return;
            case Command::Type::noteOff: noteOff (c.note, c.channel); return;
            case Command::Type::panic:   panic(); return;
            case Command::Type::cc:      controlChange (c.cc, c.value, c.channel); return;
            case Command::Type::midi:    midi (c.bytes[0], c.bytes[1], c.bytes[2]); return;
        }
    }

    // =============================================================================== performance swap

    void adopt (PerformancePtr next) noexcept
    {
        const bool wasPlay = perf && perf->play;
        auto old = std::move (perf);
        perf = std::move (next);
        // sounding voices follow their sound's new settings, like the page's live knobs
        for (auto& v : voices)
        {
            if (! v.alive) continue;
            if (auto fresh = perf->find (v.sound->id))
            {
                if (fresh != v.sound)
                {
                    const bool sameZones = fresh->zones.size() == v.sound->zones.size();
                    discard (std::move (v.sound));
                    v.sound = fresh;
                    if (! sameZones) v.zone = -1;
                    const auto& s = v.s();
                    v.kRate = dsp::semisToRate (s.pitch + s.fine / 100.0);
                    v.speed = s.speed;
                    if (v.zone < 0 && v.sample)
                    {
                        // clip edits move the loop of a sounding pad
                        const double dur = v.sample->duration(), sr = v.sample->rate;
                        const double a = std::min (s.clipIn, s.clipOut) * dur, b = std::max (s.clipIn, s.clipOut) * dur;
                        const double in = std::min (a, std::max (0.0, dur - 0.005));
                        const double out = std::max (in + 0.005, std::min (b, dur));
                        v.clipIn = v.loopStart = in * sr;
                        v.clipOut = v.loopEnd = out * sr;
                    }
                }
            }
            buildRoutes (v);
        }
        for (int i = 0; i < 2; ++i)
            globalLfo[(size_t) i].set (i ? perf->globalMod.lfo2 : perf->globalMod.lfo1, bpm);
        // leaving keyboard play releases what is still held (board.ts watches master.play)
        if (wasPlay && ! perf->play)
            for (int i = heldCount - 1; i >= 0; --i)
                noteOff (heldOrder[(size_t) i], 0);
        // the selected instrument's CC defaults
        if (perf->playable && (! old || ! sameSound (old->playable, perf->playable)))
            for (const auto& [n, x] : perf->playable->ccDefaults)
                cc[(size_t) (n & 127)] = x;
        discard (std::move (old));
    }

    // =============================================================================== rendering

    float sourceValue (const Voice& v, bool pad, ModSource src) const noexcept
    {
        switch (src)
        {
            case ModSource::lfo1:       return pad ? v.padLfo[0].value() : globalLfo[0].value();
            case ModSource::lfo2:       return pad ? v.padLfo[1].value() : globalLfo[1].value();
            case ModSource::mod:        return modWheel;
            case ModSource::aftertouch: return v.pressure;
            case ModSource::velocity:   return v.velocity;
            case ModSource::bend:       return v.bend;
            case ModSource::timbre:     return v.timbre;
            default:                    return 0.0f;
        }
    }

    const float* tapOf (const Voice& v, int slot) const noexcept
    {
        const int i = v.taps[(size_t) slot];
        if (i < 0) return nullptr;
        const auto& src = voices[(size_t) i];
        if (! src.alive || src.generation != v.tapGen[(size_t) slot]) return nullptr;
        return src.tap.data();
    }

    void mapExpression(Voice& v, bool immediate) noexcept
    {
        copyScalars(v.effective,v.sound->s);
        for(int i=0;i<v.sound->expressionRouteCount;++i) {
            const auto& r=v.sound->expressionRoutes[(size_t)i];
            float x=r.source<5?v.expression[(size_t)r.source]:globalExpression[(size_t)(r.source-5)];
            x=std::pow(std::clamp(x,0.f,1.f),r.curve);
            const float alpha=immediate || r.smoothingMs<=0 ? 1.f : float(1-std::exp(-std::max(0.,v.t-v.lastExpressionT)/(r.smoothingMs*.001)));
            v.smoothed[(size_t)i]+=(x-v.smoothed[(size_t)i])*alpha;
            const float value=r.minimum+(r.maximum-r.minimum)*v.smoothed[(size_t)i];
            if(r.target==-2) v.effective.reverse=value>=.5f;
            else if(r.target>=0 && r.target<(int)std::size(parameters)) {
                const auto& p=parameters[r.target];
                setParameter(v.effective,p,value,v.sample->duration()*std::abs(v.effective.clipOut-v.effective.clipIn));
            }
        }
        v.lastExpressionT=v.t;
    }

    void applyExpression(Voice& v, bool immediate) noexcept
    {
        mapExpression(v, immediate);
        const auto& s=v.effective;
        v.kRate=dsp::semisToRate(s.pitch+s.fine/100.0);v.speed=s.speed;
        if(v.zone<0) {
            v.ampEnv={s.attack,s.decay,s.sustain,s.release};v.filtEnv={s.fAttack,s.fDecay,s.fSustain,s.fRelease};v.filtPeak=s.fEnvAmount*1200.f;
            const double dur=v.sample->duration(),sr=v.sample->rate;
            const double in=std::min(std::min(s.clipIn,s.clipOut)*dur,std::max(0.,dur-.005));
            const double out=std::max(in+.005,std::min(std::max(s.clipIn,s.clipOut)*dur,dur));
            v.clipIn=v.loopStart=in*sr;v.clipOut=v.loopEnd=out*sr;
            if(v.pos<v.clipIn || v.pos>=v.clipOut)v.pos=v.clipIn+std::fmod(std::max(0.,v.pos-v.clipIn),v.clipOut-v.clipIn);
            v.glideTo=float(v.midiNote)-s.rootNote;
            const auto kind=s.grain?Voice::Kind::cloud:s.stretch?Voice::Kind::stretch:Voice::Kind::sample;
            if(v.kind!=kind){
                v.grainCount=0;v.nextStretch=v.t;v.cloudStarted=false;v.phase={};
                for(auto& slot:v.slots)slot.on=false;
                v.rndFrom=v.rndTo=0;v.rndPhase=1;v.fmPhase=0;v.windowShape=-1;
            }
            v.kind=kind;v.monoSource=kind==Voice::Kind::cloud?false:v.sample->channels.size()<2;
            const double speed=s.speed*(kind==Voice::Kind::sample?v.kRate*dsp::semisToRate(v.glideTo):1.);
            const double sourceEnd=s.repeat>0?v.t+std::max(0.,s.repeat-v.iterations)*(out-in)/speed:inf;
            if(!v.contactReleased){v.endT=sourceEnd;v.releaseT=std::max(0.,sourceEnd-v.ampEnv.r);}
            else v.endT=std::min(sourceEnd,v.releaseT+v.ampEnv.releaseLength());
        }
    }

    void controlUpdate (Voice& v) noexcept
    {
        if(v.owner) applyExpression(v,false);
        const auto& s = v.s();
        float pitch = s.bendRange * 100.0f * v.bend + (perf ? perf->mpeBendRange : 48.0f) * 100.0f * v.noteBend + v.ccPitch;
        if(v.owner){
            bool assigned=false;const int rangeTarget=parameterIndex("bendRange");
            for(int i=0;i<v.sound->expressionRouteCount;++i){const auto& route=v.sound->expressionRoutes[size_t(i)];if(route.source==1&&route.target!=rangeTarget)assigned=true;}
            // An explicit Glide route replaces conventional pitch bend. This
            // leaves Bend useful without also bending a cutoff/reverse gesture.
            if(!assigned)pitch+=(v.expression[1]*2.f-1.f)*s.bendRange*100.f;
        }
        float cut = v.ccCut, res = 0, trem = v.tremBase, panMod = 0, dly = 0, rev = 0, gPos = 0, gSize = 0;
        for (int i = 0; i < v.routeCount; ++i)
        {
            const auto& r = v.routes[(size_t) i];
            if (r.source == ModSource::vco1 || r.source == ModSource::vco2 || r.source == ModSource::vco3)
                continue;   // audio rate, per sample
            const float x = r.gain * sourceValue (v, r.pad, r.source);
            switch (r.dest)
            {
                case ModDest::pitch:     pitch += x; break;
                case ModDest::cutoff:    cut += x; break;
                case ModDest::resonance: res += x; break;
                case ModDest::volume:    trem += x; break;
                case ModDest::pan:       panMod += x; break;
                case ModDest::delayMix:  dly += x; break;
                case ModDest::reverbMix: rev += x; break;
                case ModDest::grainPos:  gPos += x; break;
                case ModDest::grainSize: gSize += x; break;
                default: break;
            }
        }
        // SFZ region LFOs (Voice.startZoneLfos): oscillators straight into their destinations
        if (const Zone* z = v.z())
            for (const auto& l : z->lfos)
            {
                if (v.t < l.delay) continue;
                const float x = (l.invert ? -1.0f : 1.0f) * dsp::lfoValue (l.wave, (v.t - l.delay) * l.freq);
                switch (l.target)
                {
                    case ZoneLfo::Target::pitch:  pitch += x * l.depth; break;
                    case ZoneLfo::Target::cutoff: cut += x * l.depth; break;
                    case ZoneLfo::Target::volume: trem += x * (std::pow (10.0f, std::abs (l.depth) / 20.0f) - 1.0f); break;
                    case ZoneLfo::Target::pan:    panMod += x * l.depth / 100.0f; break;
                }
            }
        v.cGrainPos = gPos;
        v.cGrainSize = gSize;
        v.cPitch = pitch; v.cCut = cut; v.cRes = res; v.cTrem = trem; v.cPan = panMod; v.cDelay = dly; v.cReverb = rev;

        // EQ, when its knobs moved
        if (s.eqLow != v.eqKey[0]) { v.loC = dsp::BiquadCoefs::make (FilterType::lowshelf, 120, 1, s.eqLow, engine.rate); v.eqKey[0] = s.eqLow; }
        if (s.eqMid != v.eqKey[1]) { v.midC = dsp::BiquadCoefs::make (FilterType::peaking, 1000, 0.9, s.eqMid, engine.rate); v.eqKey[1] = s.eqMid; }
        if (s.eqHigh != v.eqKey[2]) { v.hiC = dsp::BiquadCoefs::make (FilterType::highshelf, 8000, 1, s.eqHigh, engine.rate); v.eqKey[2] = s.eqHigh; }
    }

    void updateFilter (Voice& v, float vcoCut, float vcoRes) noexcept
    {
        const auto& s = v.s();
        const float env = v.filtPeak != 0 ? v.filtPeak * v.filtEnv.at (v.t, v.releaseT) : 0.0f;
        const double cents = env + v.cCut + vcoCut;
        if (v.zoneFilter)
        {
            const double q = std::max (0.1, 0.707 * std::pow (10.0, (v.zfRes + v.ccRes) / 20.0)) + v.cRes + vcoRes;
            v.fc = dsp::BiquadCoefs::make (v.zfType, std::min (20000.0, std::max (10.0, (double) v.zfFreq)) * std::pow (2.0, cents / 1200.0), q, 0, engine.rate);
        }
        else
        {
            const double q = std::max (0.1, s.resonance * std::pow (10.0, v.ccRes / 20.0)) + v.cRes + vcoRes;
            v.fc = dsp::BiquadCoefs::make (s.filterType, s.cutoff * std::pow (2.0, cents / 1200.0), q, 0, engine.rate);
        }
    }

    void report (const Voice& v, double when, double pos, double len, double dur, double rate, float pan, float gain, int stream) noexcept
    {
        GrainView gv;
        gv.voice = v.order;
        gv.when = when;
        gv.pos = (float) pos;
        gv.len = (float) len;
        gv.dur = (float) dur;
        gv.rate = (float) rate;
        gv.pan = pan;
        gv.gain = gain;
        gv.reverse = v.s().reverse;
        if(gv.reverse)gv.pos=float((v.clipOut-1)/v.sample->rate-(pos-v.clipIn/v.sample->rate)-len);
        gv.stream = (uint8_t) stream;
        grainViews.push (std::move (gv));   // full (no one reading): dropped
    }

    /** grains.worklet.ts startSlot(): slot `si` of side `side` starts a grain, `lead` samples ago. */
    void startSlot (Voice& v, int si, int side, double at, double lead, double semis, double f) noexcept
    {
        const auto& s = v.s();
        const auto& smp = *v.sample;
        const double sr = smp.rate;
        const double clipIn = v.clipIn / sr, clipLen = std::max (1e-4, (v.clipOut - v.clipIn) / sr);
        auto& sl = v.slots[(size_t) si];
        const float u1 = random01(), u2 = random01();
        const double pos = clipIn + (s.grainPos + v.cGrainPos + s.grainPosKey * 0.01 * semis) * clipLen
            + dsp::sprayOffset (s.grainSpray, s.grainSpraySlope, s.grainSpraySign, u1, u2) / 1000.0
            + (s.grainScanOn ? dsp::scanOffset (v.t, s.grainScanTime, s.grainScanDist, s.grainScanCurve, clipLen) : 0.0);
        double rate = v.kRate * dsp::semisToRate (semis * s.grainTuneKey);
        if (s.grainTuneRnd > 0)
            rate *= dsp::tuneRndRatio (s.grainTuneRnd, random01());
        sl.step = rate * sr / engine.rate;
        sl.pos = pos * sr + sl.step * lead;
        sl.amp = s.grainAmp > 0 ? dsp::grainLevel (s.grainAmpMode, s.grainAmp, s.grainVoid, random01()) : 1.0f;
        sl.on = true;
        const double dur = 1.0 / f;
        report (v, at - lead / engine.rate, pos, dur * rate, dur, rate, side ? 0.5f : -0.5f, std::min (1.0f, sl.amp), side);
    }

    /** grains.worklet.ts renderCloud(), one sample: Granulator II's two phasors and their four grain slots. */
    void cloudSample (Voice& v, bool ending, double at, float bendSemis, float& xl, float& xr) noexcept
    {
        const auto& s = v.s();
        const auto& smp = *v.sample;
        if (s.grainWindow != v.windowShape || s.grainSymmetry != v.windowSym)
        {
            dsp::windowTable (s.grainWindow, s.grainSymmetry, v.window);
            v.windowShape = s.grainWindow;
            v.windowSym = s.grainSymmetry;
        }
        const double sr = engine.rate;
        const double semis = v.semisAt (v.t) + bendSemis;
        v.rndPhase += dsp::freqRndRate * s.grainFreq / sr;
        if (v.rndPhase >= 1)
        {
            v.rndPhase -= std::floor (v.rndPhase);
            v.rndFrom = v.rndTo;
            v.rndTo = random01() * 2 - 1;
        }
        const double wander = s.grainFreqRnd > 0
            ? (v.rndFrom + (v.rndTo - v.rndFrom) * v.rndPhase) * s.grainFreqRnd * dsp::freqRndSemis : 0.0;
        const double base = s.grainFreq * dsp::semisToRate (semis * s.grainFreqKey + wander + v.cGrainSize);
        double fmOffset = 0;
        if (s.grainFm && s.grainFmAmount > 0)
        {
            v.fmPhase += s.grainFmFreq * dsp::semisToRate (semis * s.grainFmKey) / sr;
            v.fmPhase -= std::floor (v.fmPhase);
            fmOffset = s.grainFmAmount * 0.02 / 1000.0 * smp.rate * std::sin (2 * dsp::pi * v.fmPhase);
        }
        const double spread = 1.0 + (double) s.grainStereo * s.grainStereo;
        const float* chans[2] = { smp.channels[0].data(), smp.channels.size() > 1 ? smp.channels[1].data() : smp.channels[0].data() };
        const auto lo = (long) std::floor (v.clipIn), hi = std::min ((long) smp.frames(), (long) std::ceil (v.clipOut));
        for (int side = 0; side < 2; ++side)
        {
            const double f = side ? base * spread : base / spread;
            const double step = std::min (dsp::maxPhaseStep, f / sr);
            const int a = side * 2;
            auto& ph = v.phase[(size_t) side];
            if (! v.cloudStarted)
            {
                // the note: A starts at once, and B with it at the top of its window
                ph = 0;
                if (! ending)
                {
                    startSlot (v, a, side, at, 0, semis, f);
                    startSlot (v, a + 1, side, at, 0, semis, f);
                }
            }
            else
            {
                const double prev = ph;
                double next = prev + step;
                if (next >= 1)
                {
                    next -= 1;
                    if (ending) v.slots[(size_t) a].on = false;
                    else startSlot (v, a, side, at, next / step, semis, f);
                }
                else if (prev < 0.5 && next >= 0.5)
                {
                    if (ending) v.slots[(size_t) a + 1].on = false;
                    else startSlot (v, a + 1, side, at, (next - 0.5) / step, semis, f);
                }
                ph = next;
            }
            float y = 0;
            for (int k = 0; k < 2; ++k)
            {
                auto& sl = v.slots[(size_t) (a + k)];
                if (! sl.on) continue;
                const double read = sl.pos + fmOffset;
                const double position = s.reverse ? v.clipOut - 1 - (read - v.clipIn) : read;
                y += dsp::windowAt (v.window, ph + 0.5 * k) * sl.amp * dsp::readAt (chans[side], position, lo, hi);
                sl.pos += sl.step;
            }
            (side ? xr : xl) = y;
        }
        v.cloudStarted = true;
    }

    /** grains.worklet.ts renderStretch(): start the grains due at this sample (`at` engine time). */
    void scheduleStretch (Voice& v, double at) noexcept
    {
        const auto& smp = *v.sample;
        const double sr = smp.rate;
        const double clipIn = v.clipIn / sr, clipOut = v.clipOut / sr, clipLen = std::max (1e-4, clipOut - clipIn);
        for (int guard = 0; guard < 4 && v.nextStretch <= v.t && v.t < v.endT; ++guard)
        {
            const double rate = v.kRate * dsp::semisToRate (v.semisAt (v.t));
            const double bufLen = std::min (dsp::stretchGrain * rate, clipLen);
            const double pos = std::min (clipIn + std::fmod (v.t * v.speed, clipLen), clipOut - bufLen);
            const double dur = bufLen / rate;
            if (v.grainCount < (int) v.grainPool.size())
            {
                auto& g = v.grainPool[(size_t) v.grainCount++];
                g.length = std::max (1, (int) std::lround (dur * engine.rate));
                g.index = 0;
                g.step = rate * sr / engine.rate;
                g.k = v.kRate;
                g.pos = pos * sr;
            }
            report (v, at, pos, bufLen, dur, rate, 0, 1, 0);
            v.nextStretch = v.t + std::max (dur, 1.0 / engine.rate) / dsp::stretchOverlap;
        }
    }

    bool grainsRinging (const Voice& v) const noexcept
    {
        if (v.kind == Voice::Kind::cloud)
            return std::any_of (v.slots.begin(), v.slots.end(), [] (const auto& sl) { return sl.on; });
        return v.grainCount > 0;
    }

    /** Render one voice for `n` samples, adding to the buses. Returns false once it has ended. */
    void renderVoice (Voice& v, int n, int bufferOffset) noexcept
    {
        const auto& s = v.s();
        const auto& smp = *v.sample;
        const float* chL = smp.channels[0].data();
        const float* chR = smp.channels.size() > 1 ? smp.channels[1].data() : chL;
        const auto frames = (double) smp.frames();
        const double dt = 1.0 / engine.rate;
        const double srRatio = smp.rate / engine.rate;
        float velGain = s.volume * (1.0f - s.velAmount + s.velAmount * v.velocity);

        const float* taps[3] = { tapOf (v, 0), tapOf (v, 1), tapOf (v, 2) };

        // this voice's sends: shared delay / reverb buses keyed by their settings
        DelayBus* dbus = nullptr;
        ReverbSlot* rslot = nullptr;
        if (!v.owner && (s.delayMix > 0.0001f || v.cDelay != 0))
            dbus = delayBusFor (s.delayTime, s.delayFeedback);
        if (!v.owner && (s.reverbMix > 0.0001f || v.cReverb != 0))
            rslot = reverbFor (dsp::impulseKey (s.reverbSize, s.reverbDecay));

        // Pool ownership cannot change while this voice's chunk is rendering.
        // Remember exhaustion as well as success, then retry next chunk. With
        // many sustained owners, retrying a full pool per sample dominated DSP.
        int contactBus=-2;
        for (int i = 0; i < n; ++i)
        {
            if (--v.countdown <= 0)
            {
                controlUpdate (v);
                velGain=s.volume*(1.0f-s.velAmount+s.velAmount*v.velocity);
                if (! v.vcoRoutes) updateFilter (v, 0, 0);
                v.padLfo[0].advance (dt * controlEvery);
                v.padLfo[1].advance (dt * controlEvery);
                v.countdown = controlEvery;
            }

            // audio-rate VCO modulation (FM / AM / filter)
            float vPitch = 0, vCut = 0, vRes = 0, vTrem = 0, vPan = 0;
            if (v.vcoRoutes)
            {
                for (int r = 0; r < v.routeCount; ++r)
                {
                    const auto& rt = v.routes[(size_t) r];
                    int slot = rt.source == ModSource::vco1 ? 0 : rt.source == ModSource::vco2 ? 1 : rt.source == ModSource::vco3 ? 2 : -1;
                    if (slot < 0 || ! taps[slot]) continue;
                    const float x = rt.gain * taps[slot][i];
                    switch (rt.dest)
                    {
                        case ModDest::pitch:     vPitch += x; break;
                        case ModDest::cutoff:    vCut += x; break;
                        case ModDest::resonance: vRes += x; break;
                        case ModDest::volume:    vTrem += x; break;
                        case ModDest::pan:       vPan += x; break;
                        default: break;
                    }
                }
                if ((i & (controlEvery - 1)) == 0) updateFilter (v, vCut, vRes);
            }

            // source
            const bool ended = v.t >= v.endT || (v.killT < inf && v.t >= v.killT + v.killFade);
            // A drum voice owns its exact ending sample. Continuing its filter
            // state until this render chunk finishes leaves a variable residual
            // tail when the host changes buffer partitions. Retire below at
            // the same sample, with contact effects retaining their own tails.
            if (v.owner && ended) break;
            float xl = 0, xr = 0;
            if (v.kind != Voice::Kind::sample)
            {
                if (--v.quantumLeft <= 0)
                {
                    v.quantumPitch = vPitch;
                    v.quantumLeft = 128;
                }
                const float bendSemis = (v.cPitch + v.quantumPitch) / 100.0f;
                if (v.kind == Voice::Kind::cloud)
                    cloudSample (v, ended, now + i * dt, bendSemis, xl, xr);
                else
                {
                    if (! ended)
                        scheduleStretch (v, now + i * dt);
                    const float bend = dsp::semisToRate (bendSemis);
                    const float gain = 2.0f / (float) dsp::stretchOverlap;
                    const auto last = (long) frames;
                    for (int gi = 0; gi < v.grainCount;)
                    {
                        auto& g = v.grainPool[(size_t) gi];
                        const float w = gain * dsp::hann (g.index, g.length);
                        const double position=s.reverse?v.clipOut-1-(g.pos-v.clipIn):g.pos;
                        xl += w * dsp::readAt (chL, position, 0, last);
                        xr += w * dsp::readAt (chR, position, 0, last);
                        g.pos += g.step * bend * (v.kRate / g.k);
                        if (++g.index >= g.length)
                            g = v.grainPool[(size_t) --v.grainCount];   // done: the last one takes its place
                        else
                            ++gi;
                    }
                }
            }
            else if (! ended)
            {
                const double p = s.reverse ? std::max(v.clipIn, v.clipOut - 1 - (v.pos - v.clipIn)) : v.pos;
                const auto i0 = (size_t) p;
                if ((double) i0 + 1 < frames)
                {
                    const auto f = (float) (p - (double) i0);
                    xl = chL[i0] + (chL[i0 + 1] - chL[i0]) * f;
                    xr = chR[i0] + (chR[i0 + 1] - chR[i0]) * f;
                }
                if (--v.quantumLeft <= 0)
                {
                    v.quantumPitch = vPitch;
                    v.quantumLeft = 128;
                }
                const float semis = v.semisAt (v.t) + (v.cPitch + v.quantumPitch) / 100.0f;
                const double step = v.kRate * dsp::semisToRate (semis) * v.speed * srRatio;
                v.pos += step;
                if(v.owner && v.zone<0)v.iterations+=step/std::max(1.,v.clipOut-v.clipIn);
                if (v.loops && v.pos >= v.loopEnd && v.loopEnd > v.loopStart)
                    v.pos = v.loopStart + std::fmod (v.pos - v.loopStart, v.loopEnd - v.loopStart);
                if (! v.loops && (v.pos >= v.clipOut || v.pos >= frames - 1))
                    v.endT = std::min (v.endT, v.t);   // the sample ran out
            }

            // amp envelope, filter, EQ
            const float amp = v.ampEnv.at (v.t, v.releaseT);
            xl *= amp; xr *= amp;
            xl = v.filt[0].process (v.fc, xl);
            xr = v.monoSource ? xl : v.filt[1].process (v.fc, xr);
            xl = v.hi[0].process (v.hiC, v.mid[0].process (v.midC, v.lo[0].process (v.loC, xl)));
            xr = v.monoSource ? xl : v.hi[1].process (v.hiC, v.mid[1].process (v.midC, v.lo[1].process (v.loC, xr)));

            // what a patch reads when this voice is one of its VCOs: the post-EQ signal, as mono
            if (! v.tap.empty())
                v.tap[(size_t) i] = v.monoSource ? xl : 0.5f * (xl + xr);

            if (! v.silent)
            {
                // out gain -> tremolo -> pan -> the hard-stop fade
                float g = velGain * v.zoneGain * v.ccGain * v.level * (v.cTrem + vTrem);
                if (v.killT < inf)
                    g *= (float) std::max (0.0, 1.0 - (v.t - v.killT) / v.killFade);
                // Always the stereo pan law: in the page every voice's output node also takes the
                // (stereo) reverb branch, so Web Audio up-mixes a mono voice to stereo before its
                // StereoPannerNode -- centred, it passes at full level rather than -3 dB.
                float ol, orr;
                v.outputPan.set(s.pan + v.zonePan + v.ccPan + v.cPan + vPan, false);
                v.outputPan.process(xl * g, xr * g, ol, orr);

                const auto o = (size_t) (bufferOffset + i);
                mainL[o] += ol;
                mainR[o] += orr;
                if(v.owner && (s.delayMix+v.cDelay>0 || s.reverbMix+v.cReverb>0)) {
                    // Replaced release tails stay replaced. Letting an old
                    // voice reacquire here makes tails steal slots back from
                    // each other according to the host's render partitions.
                    if(contactBus==-2)contactBus=v.contactEnded?contactEffects.find(v.owner):contactEffects.acquire(v.owner);
                    contactEffects.send(contactBus,v.owner,v.order,i,ol*(s.delayMix+v.cDelay),orr*(s.delayMix+v.cDelay),ol*(s.reverbMix+v.cReverb),orr*(s.reverbMix+v.cReverb),{s.delayTime,s.delayFeedback,s.reverbSize,s.reverbDecay},v.contactEnded);
                }
                if (dbus)
                {
                    const float m = s.delayMix + v.cDelay;
                    dbus->inL[o] += ol * m;
                    dbus->inR[o] += orr * m;
                    dbus->fed = true;
                }
                if (rslot)
                {
                    const float m = s.reverbMix + v.cReverb;
                    rslot->inL[o] += ol * m;
                    rslot->inR[o] += orr * m;
                    rslot->fed = true;
                }
            }
            if(v.owner && v.zone<0 && v.kind!=Voice::Kind::sample)v.iterations+=dt*v.speed/(std::max(1.,v.clipOut-v.clipIn)/smp.rate);
            v.t += dt;
        }

        const bool killed = v.killT < inf && v.t >= v.killT + v.killFade;
        // Drum envelopes own voice lifetime: an hour-long grain must not keep a
        // silent released contact alive until its phasor finally wraps.
        if (killed || (v.t >= v.endT && (v.owner || ! (v.kind != Voice::Kind::sample && grainsRinging (v)))))
            kill (v);
    }

    DelayBus* delayBusFor (float time, float feedback) noexcept
    {
        DelayBus* freeBus = nullptr;
        for (auto& d : delays)
        {
            if (d.used && d.time == time && d.feedback == feedback) return &d;
            if (! d.used && ! freeBus) freeBus = &d;
        }
        if (! freeBus) return nullptr;
        freeBus->used = true;
        freeBus->time = time;
        freeBus->feedback = feedback;
        freeBus->idle = 0;
        return freeBus;
    }

    ReverbSlot* reverbFor (uint32_t key) noexcept
    {
        for (auto& r : reverbs)
            if (r.key.load (std::memory_order_acquire) == key) return &r;
        return nullptr;   // not loaded (yet): no reverb rather than the wrong one
    }

    void renderChunk (int offset, int n) noexcept
    {
        contactEffects.begin();
        for (int i = 0; i < n; ++i)
            mainL[(size_t) (offset + i)] = mainR[(size_t) (offset + i)] = 0;
        for (auto& d : delays)
            for (int i = 0; i < n; ++i) d.inL[(size_t) (offset + i)] = d.inR[(size_t) (offset + i)] = 0;
        for (auto& r : reverbs)
            for (int i = 0; i < n; ++i) r.inL[(size_t) (offset + i)] = r.inR[(size_t) (offset + i)] = 0;

        // VCO voices first: their taps feed the voices that follow
        int count = 0;
        for (auto& v : voices)
            if (v.alive && v.group) { renderVoice (v, n, offset); }
        for (auto& v : voices)
            if (v.alive && ! v.group) { renderVoice (v, n, offset); }
        for (auto& v : voices)
            if (v.alive) ++count;
        engine.active.store (count, std::memory_order_relaxed);

        const double dt = 1.0 / engine.rate;
        globalLfo[0].advance (dt * n);
        globalLfo[1].advance (dt * n);

        contactEffects.process(mainL.data()+offset,mainR.data()+offset,n);

        // per-sound delays: hi -> delay (feedback) -> wet, shared by every voice with those settings
        for (auto& d : delays)
        {
            if (! d.used) continue;
            const double samples = std::max (0.01f, d.time) * engine.rate;
            const float fb = std::min (0.95f, d.feedback);
            float peakOut = 0;
            for (int i = 0; i < n; ++i)
            {
                const auto o = (size_t) (offset + i);
                const float yl = d.lineL.process (d.inL[o] + d.lastL * fb, samples);
                const float yr = d.lineR.process (d.inR[o] + d.lastR * fb, samples);
                d.lastL = yl; d.lastR = yr;
                mainL[o] += yl; mainR[o] += yr;
                peakOut = std::max (peakOut, std::max (std::abs (yl), std::abs (yr)));
            }
            d.idle = d.fed || peakOut > 1e-5f ? 0 : d.idle + n * dt;
            d.fed = false;
            if (d.idle > 1.0) { d.used = false; d.lineL.clear(); d.lineR.clear(); d.lastL = d.lastR = 0; }
        }

        // per-sound reverbs
        for (auto& r : reverbs)
        {
            if (! r.conv || r.key.load (std::memory_order_acquire) == 0) continue;
            if (! r.fed && r.idle > 12.0) continue;   // long silent: skip the work
            float* chans[] = { r.inL.data() + offset, r.inR.data() + offset };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
            r.conv->process (juce::dsp::ProcessContextReplacing<float> (block));
            for (int i = 0; i < n; ++i)
            {
                const auto o = (size_t) (offset + i);
                mainL[o] += r.inL[o];
                mainR[o] += r.inR[o];
            }
            r.idle = r.fed ? 0 : r.idle + n * dt;
            r.fed = false;
        }

        masterEffects (offset, n);
    }

    void masterEffects (int offset, int n) noexcept
    {
        if (! perf) return;
        const auto& fx = perf->fx;
        std::array<float,5> target {{fx.reverbMix, fx.delayMix, fx.chorusMix,
            float(std::log(std::clamp(fx.cutoff,20.f,20000.f)/20.f)/std::log(1000.f)), (fx.resonance-.1f)/19.9f}};
        for (size_t j=0;j<5;++j) {
            if (fx.sources[j]>=0 && fx.sources[j]<5) target[j]=globalExpression[(size_t)fx.sources[j]];
            target[j]=std::clamp(target[j],0.f,1.f);
        }
        const float smoothing = float(1.0-std::exp(-1.0/(engine.rate*.012)));
        for (auto& lfo : chorusLfo) lfo.set({LfoShape::sine,fx.chorusRate,false,1},bpm);
        const double dt = 1.0 / engine.rate;
        const float gain = perf->muted ? 0.0f : perf->volume;
        // synced: the division at the current tempo (the host's in a DAW), as the page computes it
        const double delaySeconds = fx.delaySync ? std::min (2.4, fx.delayBeats * 60.0 / std::max (1.0, bpm)) : (double) fx.delayTime;
        const double delaySamples = std::max (0.01, delaySeconds) * engine.rate;
        const float fb = std::min (0.95f, fx.delayFeedback);

        // reverb on the input, into its own buffer
        // Keep a bound reverb warm even at zero mix; skip its FFT work for dry,
        // unbound legacy patches. The smoothed fade finishes before bypass.
        const bool reverbOn = masterReverb && (fx.sources[0]>=0 || target[0]>.0001f || masterControls[0]>.0001f);
        if (reverbOn)
        {
            for (int i = 0; i < n; ++i)
            {
                wetL[(size_t) i] = mainL[(size_t) (offset + i)];
                wetR[(size_t) i] = mainR[(size_t) (offset + i)];
            }
            float* chans[] = { wetL.data(), wetR.data() };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
            masterReverb->process (juce::dsp::ProcessContextReplacing<float> (block));
        }

        for (int i = 0; i < n; ++i)
        {
            for (size_t j=0;j<5;++j) masterControls[j]+=(target[j]-masterControls[j])*smoothing;
            if (--masterFilterTick<=0) {
                const float hz=20.f*std::pow(1000.f,masterControls[3]);
                masterFilterC=hz>=19990.f ? dsp::BiquadCoefs{} : dsp::BiquadCoefs::make(FilterType::lowpass,hz,.1f+19.9f*masterControls[4],0,engine.rate);
                masterFilterTick=16;
            }
            const auto o = (size_t) (offset + i);
            const float inL = mainL[o], inR = mainR[o];
            float outL = inL, outR = inR;

            // chorus: two LFO-swept delays of the mono mix, one per side
            if (masterControls[2] > 0.0001f)
            {
                const float mixIn = 0.5f * (inL + inR);
                const float dl = chorusBase + fx.chorusDepth * chorusMaxDepth * chorusLfo[0].value();
                const float dr = chorusBase + fx.chorusDepth * chorusMaxDepth * chorusLfo[1].value();
                outL += masterControls[2] * chorusLine[0].process (mixIn, dl * engine.rate);
                outR += masterControls[2] * chorusLine[1].process (mixIn, dr * engine.rate);
            }
            else
            {
                chorusLine[0].process (0.5f * (inL + inR), chorusBase * engine.rate);
                chorusLine[1].process (0.5f * (inL + inR), chorusBase * engine.rate);
            }
            chorusLfo[0].advance (dt);
            chorusLfo[1].advance (dt);

            // delay with a damped feedback path
            const float yl = masterDelay[0].process (inL + masterFbL, delaySamples);
            const float yr = masterDelay[1].process (inR + masterFbR, delaySamples);
            masterFbL = damp[0].process (dampC, yl) * fb;
            masterFbR = damp[1].process (dampC, yr) * fb;
            outL += masterControls[1] * yl;
            outR += masterControls[1] * yr;

            if (reverbOn)
            {
                outL += masterControls[0] * wetL[(size_t) i];
                outR += masterControls[0] * wetR[(size_t) i];
            }

            outL = masterFilter[0].process(masterFilterC,outL)*gain;
            outR = masterFilter[1].process(masterFilterC,outR)*gain;
            mainL[o] = outL;
            mainR[o] = outR;
            peakL = std::max (peakL * 0.99995f, std::abs (outL));
            peakR = std::max (peakR * 0.99995f, std::abs (outR));
        }
    }
};

// =================================================================================== Engine

Engine::Engine() : impl (std::make_unique<Impl> (*this))
{
    auto& m = *impl;
    // MIDI's usual power-on values: volume 100, pan centre, expression full
    m.cc[7] = 100 / 127.0f;
    m.cc[10] = 64 / 127.0f;
    m.cc[11] = 1;
    m.perf = std::make_shared<Performance>();
}

Engine::~Engine()
{
    if (auto* p = impl->incoming.exchange (nullptr))
        delete p;
    collectGarbage();
}

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    // Build shared grain-window tables off the audio thread.
    (void) dsp::noizTable();
    (void) dsp::windowSineTable();
    auto& m = *impl;
    collectGarbage();
    rate = sampleRate;
    m.maxBlock = std::max (maxBlockSize, chunk);
    const auto size = (size_t) m.maxBlock;
    m.mainL.assign (size, 0); m.mainR.assign (size, 0);
    m.wetL.assign (size, 0); m.wetR.assign (size, 0);
    for (auto& v : m.voices)
    {
        v.tap.assign (size, 0);
        m.kill (v);
    }
    m.contactEffects.prepare(sampleRate);
    for (auto& d : m.delays)
    {
        d.inL.assign (size, 0); d.inR.assign (size, 0);
        d.lineL.prepare (sampleRate, 2.5); d.lineR.prepare (sampleRate, 2.5);
        d.used = false;
    }
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) m.maxBlock, 2 };
    for (auto& r : m.reverbs)
    {
        r.inL.assign (size, 0); r.inR.assign (size, 0);
        if (! r.conv) r.conv = std::make_unique<juce::dsp::Convolution> (juce::dsp::Convolution::NonUniform { 1024 });
    }
    for (auto& k : m.loadedKeys) k = 0;
    for (auto& r : m.reverbs) r.key.store (0);
    if (! m.masterReverb) m.masterReverb = std::make_unique<juce::dsp::Convolution> (juce::dsp::Convolution::NonUniform { 1024 });
    m.masterReverbKey = 0;
    for (auto& f : m.masterFilter) f.reset();
    m.masterFilterC={};m.masterFilterTick=0;m.masterControls={0,0,0,1,(.7f-.1f)/19.9f};
    for (auto& l : m.chorusLine) l.prepare (sampleRate, 0.1);
    for (auto& l : m.masterDelay) l.prepare (sampleRate, 2.5);
    m.dampC = dsp::BiquadCoefs::make (FilterType::lowpass, 5000, 1, 0, sampleRate);
    for (auto& d : m.damp) d.reset();

    // the page starts the second chorus LFO a quarter cycle later
    LfoDef chorus { LfoShape::sine, m.perf->fx.chorusRate, false, 1 };
    m.chorusLfo[0].start (chorus, 120, 1);
    m.chorusLfo[1].start (chorus, 120, 2);
    m.chorusLfo[1].advance (-0.25 / std::max (0.05f, m.perf->fx.chorusRate));
    m.globalLfo[0].start (m.perf->globalMod.lfo1, m.bpm, 11);
    m.globalLfo[1].start (m.perf->globalMod.lfo2, m.bpm, 12);

    // Re-set what is (or is about to be) playing, so its reverb impulses load at this sample rate.
    // A performance still waiting to be adopted wins: a host restores a session's state before it
    // prepares the plugin, and that state must not be replaced by the empty default.
    PerformancePtr current = m.perf;
    if (auto* pending = m.incoming.exchange (nullptr))
    {
        current = *pending;
        delete pending;
    }
    setPerformance (current);

    // JUCE completes queued IR loading in prepare(). Load the initial/restored
    // performance first so the first audio block uses its actual reverbs,
    // including when an offline host outruns the background convolution worker.
    // Later live performance changes retain their asynchronous, click-free load.
    for (auto& r : m.reverbs)
        r.conv->prepare (spec);
    m.masterReverb->prepare (spec);
}

void Engine::preparePerformance (const PerformancePtr& next)
{
    auto& m = *impl;
    if (! next) return;

    // Reverb impulses this performance needs, loaded here (JUCE loads them on its own thread and
    // swaps them in without a click). Slots already holding a wanted key keep it.
    std::vector<uint32_t> wanted;
    std::vector<std::pair<float, float>> params;
    auto want = [&] (float size, float decay)
    {
        const auto key = dsp::impulseKey (size, decay);
        if (std::find (wanted.begin(), wanted.end(), key) == wanted.end())
        {
            wanted.push_back (key);
            params.emplace_back (size, decay);
        }
    };
    if (! next->contactEffectsOnly) for (const auto& [id, s] : next->byId)
    {
        bool routed = false;
        for (const auto& r : s->s.mod.routes)
            routed = routed || r.dest == ModDest::reverbMix;
        if (s->s.reverbMix > 0.0001f || routed)
            want (s->s.reverbSize, s->s.reverbDecay);
    }
    auto load = [&] (juce::dsp::Convolution& conv, float size, float decay)
    {
        auto ir = dsp::makeImpulse (rate, size, decay);
        const float scale = convolverNormalisation (ir, rate);
        juce::AudioBuffer<float> buffer (2, (int) ir[0].size());
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, ir[(size_t) ch][(size_t) i] * scale);
        conv.loadImpulseResponse (std::move (buffer), rate, juce::dsp::Convolution::Stereo::yes,
                                  juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    };
    for (size_t w = 0; w < wanted.size() && w < (size_t) reverbSlots; ++w)
    {
        const auto key = wanted[w];
        if (std::find (std::begin (m.loadedKeys), std::end (m.loadedKeys), key) != std::end (m.loadedKeys))
            continue;
        // a slot whose key nobody wants any more
        for (int i = 0; i < reverbSlots; ++i)
        {
            if (m.loadedKeys[i] != 0 && std::find (wanted.begin(), wanted.end(), m.loadedKeys[i]) != wanted.end())
                continue;
            if (m.reverbs[(size_t) i].conv)
            {
                load (*m.reverbs[(size_t) i].conv, params[w].first, params[w].second);
                m.loadedKeys[i] = key;
                m.reverbs[(size_t) i].key.store (key, std::memory_order_release);
            }
            break;
        }
    }
    const auto masterKey = dsp::impulseKey (next->fx.reverbSize, next->fx.reverbDecay);
    if (m.masterReverb && masterKey != m.masterReverbKey)
    {
        load (*m.masterReverb, next->fx.reverbSize, next->fx.reverbDecay);
        m.masterReverbKey = masterKey;
    }

 }

bool Engine::canActivatePerformance() const noexcept { return impl->canRetireMore(); }
bool Engine::activatePerformance(const PerformancePtr& next) noexcept {
    if(!next || !canActivatePerformance())return false;
    impl->adopt(next);return true;
}
void Engine::setPerformance(PerformancePtr next) {
    preparePerformance(next);
    auto& m=*impl;
    auto* holder = new PerformancePtr (std::move (next));
    if (auto* stale = m.incoming.exchange (holder))
        delete stale;   // never reached the audio thread
}

void Engine::collectGarbage()
{
    auto& m = *impl;
    std::shared_ptr<const void> p;
    while (m.trash.pop (p)) p.reset();
    PerformancePtr* h = nullptr;
    while (m.trashHolders.pop (h)) delete h;
}

bool Engine::post (const Command& c) noexcept
{
    return impl->commands.push (Command (c));
}

void Engine::process (float* left, float* right, int n, const MidiEvent* midi, int midiCount, HostClock host) noexcept
{
    const double hostBpm = host.bpm;
    auto& m = *impl;
    if(m.canRetireMore() && m.trashHolders.available()>0)if(auto* holder=m.incoming.exchange(nullptr)) {
        m.adopt(std::move(*holder));
        const bool retired=m.trashHolders.push(std::move(holder));
        jassert(retired);juce::ignoreUnused(retired);
    }
    if (hostBpm > 0 && std::abs (hostBpm - m.bpm) > 1e-6)
    {
        m.bpm = hostBpm;
        for (int i = 0; i < 2; ++i)
            m.globalLfo[(size_t) i].set (i ? m.perf->globalMod.lfo2 : m.perf->globalMod.lfo1, m.bpm);
    }
    else if (hostBpm <= 0 && m.perf && std::abs (m.perf->bpm - m.bpm) > 1e-6)
    {
        m.bpm = m.perf->bpm;
        for (int i = 0; i < 2; ++i)
            m.globalLfo[(size_t) i].set (i ? m.perf->globalMod.lfo2 : m.perf->globalMod.lfo1, m.bpm);
    }

    // Synced global LFOs lock to the host's song position while it plays (MIDI Start's job
    // otherwise), so they land on the same beat every time the song is played.
    if (host.playing && host.ppq >= 0 && m.perf)
        for (int i = 0; i < 2; ++i)
        {
            const auto& def = i ? m.perf->globalMod.lfo2 : m.perf->globalMod.lfo1;
            if (def.sync && def.divisionBeats > 0)
                m.globalLfo[(size_t) i].syncTo (host.ppq / (double) def.divisionBeats);
        }

    Command c;
    while (m.commands.pop (c))
        m.command (c);

    int e = 0;
    for (int done = 0; done < n;)
    {
        // MIDI due at or before this point, then render up to the next event (or a chunk)
        while (e < midiCount && midi[e].offset <= done)
        {
            m.midi (midi[e].status, midi[e].data1, midi[e].data2);
            ++e;
        }
        int len = std::min ({ chunk, n - done, m.maxBlock });
        if (e < midiCount)
            len = std::min (len, std::max (1, midi[e].offset - done));
        // the buses are sized for maxBlock; render into them from 0 and copy out
        m.renderChunk (0, len);
        std::memcpy (left + done, m.mainL.data(), sizeof (float) * (size_t) len);
        std::memcpy (right + done, m.mainR.data(), sizeof (float) * (size_t) len);
        m.now += len / rate;
        done += len;
    }
    while (e < midiCount)
    {
        m.midi (midi[e].status, midi[e].data1, midi[e].data2);
        ++e;
    }
    peaks[0].store (m.peakL, std::memory_order_relaxed);
    peaks[1].store (m.peakR, std::memory_order_relaxed);
    m.publishViews();
}

void Engine::drumHit(const SoundPtr& sound,uint64_t owner,uint64_t token,float velocity,const std::array<float,5>& values) noexcept {
    if(!sound) return;
    auto& m=*impl;
    m.contactEffects.active(owner);
    Impl::Start start; start.hasMpe=true;start.owner=owner;start.token=token;start.expression=values;start.midiNote=60;
    int indices[32];int count=m.startNote(sound,60,velocity,start,0,false,false,indices,32);
    for(int i=0;i<count;++i) {auto& v=m.voices[(size_t)indices[i]];
        const int choke=v.s().choke;
        if(choke>0)for(auto& old:m.voices)if(old.alive && (old.owner!=owner || old.eventToken!=token) && old.s().choke==choke)m.stopVoice(old,.005);
        v.countdown=0;
    }
}
void Engine::drumExpression(uint64_t owner,const std::array<float,5>& values) noexcept {
    for(auto& v:impl->voices) if(v.alive && v.owner==owner && v.expression!=values) {v.expression=values;v.countdown=0;}
}
void Engine::drumRelease(uint64_t owner,const std::array<float,5>& values,bool hard) noexcept {
    impl->contactEffects.release(owner);
    for(auto& v:impl->voices) if(v.alive && v.owner==owner) {
        v.expression=values;v.contactEnded=true;impl->applyExpression(v,true);v.countdown=0;
        if(hard)impl->stopVoice(v,.003);else if(v.s().mode==TriggerMode::hold || v.s().repeat==0)impl->releaseVoice(v);
    }
}
void Engine::drumEnd(uint64_t token) noexcept {
    for(auto& v:impl->voices) if(v.alive && v.eventToken==token && v.s().mode==TriggerMode::hold)impl->releaseVoice(v);
}
void Engine::drumPanic() noexcept {impl->panic();}
void Engine::drumGlobal(int index,float value) noexcept {if(index>=0 && index<5)impl->globalExpression[(size_t)index]=std::clamp(value,0.f,1.f);}

bool Engine::readVoices (std::vector<VoiceView>& out, double& time) const
{
    const auto& m = *impl;
    const auto before = m.viewSeq.load (std::memory_order_acquire);
    if (before & 1u) return false;
    const auto n = m.viewCount.load (std::memory_order_relaxed);
    out.assign (m.views.begin(), m.views.begin() + std::clamp (n, 0, maxVoices));
    time = m.viewTime;
    std::atomic_thread_fence (std::memory_order_acquire);
    return m.viewSeq.load (std::memory_order_acquire) == before;
}

void Engine::readGrains (std::vector<GrainView>& out, size_t max)
{
    out.clear();
    GrainView g;
    while (impl->grainViews.pop (g))
        out.push_back (g);
    if (out.size() > max)   // keep the latest (after the editor was closed the ring holds old ones)
        out.erase (out.begin(), out.end() - (std::ptrdiff_t) max);
}

} // namespace ssb
