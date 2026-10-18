#include "check.h"

#include <ssb/Engine.h>
#include <ssb/ModelJson.h>

#include <cmath>
#include <map>
#include <thread>

// The engine without a host: generated samples in, rendered audio out, measured.

namespace
{
constexpr double rate = 48000;
constexpr int block = 256;

ssb::SamplePtr sine (double hz, double seconds, int channels = 1, double sampleRate = rate)
{
    auto s = std::make_shared<ssb::Sample>();
    s->rate = sampleRate;
    const auto n = (size_t) (seconds * sampleRate);
    s->channels.assign ((size_t) channels, std::vector<float> (n));
    for (auto& ch : s->channels)
        for (size_t i = 0; i < n; ++i)
            ch[i] = (float) std::sin (2 * ssb::dsp::pi * hz * (double) i / sampleRate);
    return s;
}

struct Rig
{
    ssb::Engine engine;
    std::map<std::string, std::shared_ptr<ssb::Sound>> sounds;
    std::map<std::string, ssb::SamplePtr> samples;
    juce::var meta { new juce::DynamicObject() };

    Rig() { engine.prepare (rate, block); }

    ssb::Sound& add (const std::string& id, ssb::SamplePtr sample, std::function<void (ssb::Settings&)> tweak = {})
    {
        auto s = std::make_shared<ssb::Sound>();
        s->id = id;
        s->audioId = id + ".wav";
        s->sample = sample;
        s->s.repeat = 0;   // loop, unless a test says otherwise
        if (tweak) tweak (s->s);
        sounds[id] = s;
        return *s;
    }

    void set (const char* key, const juce::var& v) { meta.getDynamicObject()->setProperty (key, v); }

    void commit()
    {
        engine.setPerformance (ssb::buildPerformance (meta, sounds));
        std::vector<float> l (block), r (block);
        engine.process (l.data(), r.data(), 1, nullptr, 0);   // adopt it
    }

    std::vector<float> left, right;

    /** Render `seconds`, with MIDI `events` ({sample offset, status, d1, d2}) at their times. */
    ssb::HostClock host;   // what the "DAW" reports

    void render (double seconds, std::vector<ssb::MidiEvent> events = {})
    {
        const auto total = (int) (seconds * rate);
        left.assign ((size_t) total, 0);
        right.assign ((size_t) total, 0);
        for (int done = 0; done < total; done += block)
        {
            const int n = std::min (block, total - done);
            std::vector<ssb::MidiEvent> here;
            for (const auto& e : events)
                if (e.offset >= done && e.offset < done + n)
                    here.push_back ({ e.offset - done, e.status, e.data1, e.data2 });
            engine.process (left.data() + done, right.data() + done, n, here.data(), (int) here.size(), host);
            if (host.ppq >= 0 && host.playing) host.ppq += n / rate * host.bpm / 60.0;
        }
        engine.collectGarbage();
    }

    double rms (double from, double to) const
    {
        double sum = 0;
        size_t n = 0;
        for (auto i = (size_t) (from * rate); i < (size_t) (to * rate) && i < left.size(); ++i, ++n)
            sum += (double) left[i] * left[i];
        return n ? std::sqrt (sum / (double) n) : 0.0;
    }

    /** Frequency from rising zero crossings of the left channel. */
    double hz (double from, double to) const
    {
        int crossings = 0;
        double first = -1, last = -1;
        for (auto i = (size_t) (from * rate) + 1; i < (size_t) (to * rate) && i < left.size(); ++i)
            if (left[i - 1] <= 0 && left[i] > 0)
            {
                const double t = (double) i / rate;
                if (first < 0) first = t;
                last = t;
                ++crossings;
            }
        return crossings > 1 ? (crossings - 1) / (last - first) : 0.0;
    }

    void press (const std::string& id, float velocity = 1)
    {
        engine.post (ssb::Command::make (ssb::Command::Type::press, id, velocity));
    }
};

/** A constant signal: every grain of it is a copy of its window. */
ssb::SamplePtr dc (float value, double seconds)
{
    auto s = std::make_shared<ssb::Sample>();
    s->rate = rate;
    s->channels.assign (1, std::vector<float> ((size_t) (seconds * rate), value));
    return s;
}

/** Granulator II's cloud as it starts (nothing random), GRAIN at `hz`: each grain lasts 1 / hz, two overlap. */
void cloud (ssb::Settings& s, float hz)
{
    s.grain = true;
    s.grainFreq = hz;
}

/** How exactly `x` repeats every `period` seconds between `from` and `to`: 1 = perfectly, 0 = not at all. */
double periodicity (const std::vector<float>& x, double from, double to, double period)
{
    const auto a = (size_t) (from * rate), b = std::min (x.size(), (size_t) (to * rate));
    const double lag = period * rate;
    double num = 0, den = 0;
    for (size_t i = a; i < b; ++i)
    {
        const double p = (double) i - lag;
        const auto j = (size_t) p;
        if (p < 0 || j + 1 >= x.size()) continue;
        const double y = x[j] + (x[j + 1] - x[j]) * (p - (double) j);   // the signal one period ago
        num += x[i] * y;
        den += x[i] * x[i];
    }
    return den > 0 ? num / den : 0.0;
}

ssb::MidiEvent on (double t, int note, int vel = 100) { return { (int) (t * rate), 0x90, (uint8_t) note, (uint8_t) vel }; }
ssb::MidiEvent off (double t, int note) { return { (int) (t * rate), 0x80, (uint8_t) note, 0 }; }
} // namespace

TEST ("engine: a pad plays its sample at pitch, and PITCH shifts it")
{
    Rig rig;
    rig.add ("a", sine (440, 2));
    rig.add ("b", sine (440, 2), [] (auto& s) { s.pitch = 12; });
    rig.commit();
    rig.press ("a");
    rig.render (0.5);
    CHECK (rig.rms (0.1, 0.4) > 0.1);
    CHECK_NEAR (rig.hz (0.1, 0.4), 440.0, 2.0);

    Rig rig2;
    rig2.add ("b", sine (440, 2), [] (auto& s) { s.pitch = 12; });
    rig2.commit();
    rig2.press ("b");
    rig2.render (0.5);
    CHECK_NEAR (rig2.hz (0.1, 0.4), 880.0, 4.0);
}

TEST ("engine: a sample at another rate plays at its own pitch")
{
    Rig rig;
    rig.add ("a", sine (440, 2, 1, 44100));
    rig.commit();
    rig.press ("a");
    rig.render (0.5);
    CHECK_NEAR (rig.hz (0.1, 0.4), 440.0, 2.0);
}

TEST ("engine: attack ramps linearly, release fades to silence")
{
    Rig rig;
    rig.add ("a", sine (100, 3), [] (auto& s) { s.attack = 0.2f; s.release = 0.1f; s.mode = ssb::TriggerMode::hold; });
    rig.commit();
    rig.press ("a");
    rig.render (0.1);
    const double halfway = rig.rms (0.08, 0.1);
    rig.render (0.3);
    const double full = rig.rms (0.1, 0.3);
    CHECK (halfway > 0.25 * full && halfway < 0.65 * full);
    rig.engine.post (ssb::Command::make (ssb::Command::Type::release, "a"));
    rig.render (0.3);
    CHECK (rig.rms (0.15, 0.3) < 1e-4);
    CHECK (rig.engine.activeVoices() == 0);
}

TEST ("engine: REPEAT 2 plays the clip twice and stops")
{
    Rig rig;
    rig.add ("a", sine (200, 0.25), [] (auto& s) { s.repeat = 2; s.release = 0.01f; });
    rig.commit();
    rig.press ("a");
    rig.render (0.7);
    CHECK (rig.rms (0.05, 0.45) > 0.1);
    CHECK (rig.rms (0.52, 0.7) < 1e-4);
}

TEST ("engine: drum release endings stop filter residuals at a fixed sample")
{
    auto render = [] (int partition, bool hard)
    {
        Rig rig;
        rig.add ("ending", sine (123, 1), [] (auto& s)
        {
            s.mode = ssb::TriggerMode::hold;
            s.sustain = 1;
            s.release = .06f;
            s.cutoff = 1100;
            s.resonance = 8;
        });
        rig.commit();
        std::vector<float> left (6000), right (6000);
        rig.engine.drumHit (rig.sounds["ending"], 1, 1, 1, {1, .5f, 0, 0, 0});
        auto segment = [&] (int begin, int end)
        {
            for (int frame = begin; frame < end; frame += partition)
                rig.engine.process (left.data() + frame, right.data() + frame,
                                    std::min (partition, end - frame), nullptr, 0);
        };
        segment (0, 531);
        rig.engine.drumRelease (1, {1, .5f, 0, 0, 0}, hard);
        segment (531, 6000);
        CHECK (rig.engine.activeVoices() == 0);
        if (!hard)
            for (size_t i = 531 + 2880; i < left.size(); ++i)
                CHECK (left[i] == 0 && right[i] == 0);
        return left;
    };
    for (bool hard : {false, true})
    {
        const auto reference = render (1, hard);
        CHECK (std::any_of (reference.begin(), reference.end(), [] (float value) { return std::abs (value) > .01f; }));
        for (int partition : {64, 257, 2048})
            CHECK (render (partition, hard) == reference);
    }
}

TEST ("engine: trigger modes -- STOP releases on a second press, choke groups cut each other")
{
    Rig rig;
    rig.add ("a", sine (200, 2), [] (auto& s) { s.mode = ssb::TriggerMode::stop; s.release = 0.01f; s.choke = 1; });
    rig.add ("b", sine (300, 2), [] (auto& s) { s.choke = 1; });
    juce::Array<juce::var> pads { "a", "b" };
    rig.set ("pads", pads);
    rig.commit();
    rig.press ("a");
    rig.render (0.2);
    CHECK (rig.engine.activeVoices() == 1);
    rig.press ("a");   // STOP: second press releases
    rig.render (0.2);
    CHECK (rig.engine.activeVoices() == 0);
    rig.press ("a");
    rig.render (0.1);
    rig.press ("b");   // same choke group: a stops
    rig.render (0.2);
    CHECK (rig.engine.activeVoices() == 1);
    CHECK_NEAR (rig.hz (0.1, 0.2), 300.0, 3.0);
}

TEST ("engine: keyboard play follows the root note; MIDI pads answer their notes")
{
    Rig rig;
    rig.add ("a", sine (440, 3), [] (auto& s) { s.rootNote = 69; s.release = 0.01f; });
    rig.set ("play", true);
    rig.set ("playable", "a");
    rig.commit();
    rig.render (0.5, { on (0.0, 81) });   // an octave above the root
    CHECK_NEAR (rig.hz (0.1, 0.4), 880.0, 4.0);
    rig.render (0.3, { off (0.0, 81) });
    CHECK (rig.engine.activeVoices() == 0);

    Rig pads;
    pads.add ("kick", sine (60, 1));
    juce::Array<juce::var> pair { 36, "kick" };
    juce::Array<juce::var> notes { juce::var (pair) };
    pads.set ("padNotes", notes);
    pads.commit();
    pads.render (0.4, { on (0.0, 36) });
    CHECK_NEAR (pads.hz (0.05, 0.35), 60.0, 1.5);
}

TEST ("engine: SFZ zones are chosen by key and velocity")
{
    Rig rig;
    auto& inst = rig.add ("inst", sine (440, 2));
    ssb::Zone low, high;
    low.sample = sine (220, 2);  low.lokey = 0;  low.hikey = 59; low.keycenter = 57;
    high.sample = sine (880, 2); high.lokey = 60; high.hikey = 127; high.keycenter = 81;
    low.hivel = 127; high.hivel = 127;
    inst.zones = { low, high };
    rig.set ("play", true);
    rig.set ("playable", "inst");
    rig.commit();
    rig.render (0.4, { on (0.0, 57) });
    CHECK_NEAR (rig.hz (0.1, 0.35), 220.0, 2.0);
    rig.render (0.4, { off (0.0, 57), on (0.05, 81) });
    CHECK_NEAR (rig.hz (0.15, 0.38), 880.0, 5.0);
}

TEST ("engine: mono glide slides between held notes")
{
    Rig rig;
    rig.add ("a", sine (220, 4), [] (auto& s) { s.rootNote = 57; });
    rig.set ("play", true);
    rig.set ("mono", true);
    rig.set ("glide", 0.2);
    rig.set ("playable", "a");
    rig.commit();
    rig.render (1.0, { on (0.0, 57), on (0.3, 69) });
    CHECK_NEAR (rig.hz (0.1, 0.28), 220.0, 2.0);
    const double mid = rig.hz (0.36, 0.44);
    CHECK (mid > 240 && mid < 420);          // on its way
    CHECK_NEAR (rig.hz (0.6, 0.95), 440.0, 4.0);
    CHECK (rig.engine.activeVoices() == 1);   // one voice, slid rather than retriggered
}

TEST ("engine: the low-pass filter removes what is above its cutoff")
{
    Rig open, closed;
    open.add ("a", sine (5000, 1));
    closed.add ("a", sine (5000, 1), [] (auto& s) { s.cutoff = 300; });
    open.commit();
    closed.commit();
    open.press ("a");
    closed.press ("a");
    open.render (0.3);
    closed.render (0.3);
    CHECK (closed.rms (0.1, 0.3) < 0.02 * open.rms (0.1, 0.3));
}

TEST ("engine: a patch's VCO modulates its main voice (FM), and a MOD-only VCO is not heard")
{
    auto patch = [] (bool fm)
    {
        auto rig = std::make_unique<Rig>();
        auto& carrier = rig->add ("carrier", sine (440, 3), [&] (auto& s)
        {
            s.rootNote = 69;
            if (fm) s.mod.routes.push_back ({ ssb::ModSource::vco2, ssb::ModDest::pitch, 0.35f });
        });
        rig->add ("mod", sine (440, 3), [] (auto& s) { s.rootNote = 69; });
        ssb::Link link;
        link.index = 1;
        link.soundId = "mod";
        link.audible = false;
        carrier.links.push_back (link);
        rig->set ("play", true);
        rig->set ("playable", "carrier");
        rig->commit();
        rig->render (0.5, { on (0.0, 69) });
        return rig;
    };
    auto plain = patch (false);
    auto fm = patch (true);
    CHECK_NEAR (plain->hz (0.1, 0.45), 440.0, 2.0);     // the silent VCO adds nothing
    CHECK (plain->engine.activeVoices() == 2);          // but it runs
    // FM smears the zero crossings: the waveforms differ substantially
    double diff = 0;
    for (size_t i = (size_t) (0.1 * rate); i < (size_t) (0.45 * rate); ++i)
        diff += std::abs (plain->left[i] - fm->left[i]);
    CHECK (diff / (0.35 * rate) > 0.1);
}

TEST ("engine: sustain pedal holds released notes until it lifts")
{
    Rig rig;
    rig.add ("a", sine (220, 4), [] (auto& s) { s.rootNote = 57; s.release = 0.01f; });
    rig.set ("play", true);
    rig.set ("playable", "a");
    rig.commit();
    rig.render (0.6, { { 0, 0xb0, 64, 127 }, on (0.05, 57), off (0.2, 57) });
    CHECK (rig.engine.activeVoices() == 1);
    rig.render (0.2, { { 0, 0xb0, 64, 0 } });
    CHECK (rig.engine.activeVoices() == 0);
}

TEST ("engine: live settings changes reach a sounding voice")
{
    Rig rig;
    rig.add ("a", sine (440, 3));
    rig.commit();
    rig.press ("a");
    rig.render (0.3);
    CHECK_NEAR (rig.hz (0.1, 0.28), 440.0, 2.0);
    rig.add ("a", sine (440, 3), [] (auto& s) { s.pitch = 7; });
    rig.commit();
    rig.render (0.3);
    CHECK_NEAR (rig.hz (0.05, 0.28), 440.0 * std::pow (2.0, 7 / 12.0), 4.0);
}

TEST ("engine: master volume and mute")
{
    Rig rig;
    rig.add ("a", sine (440, 3));
    rig.set ("volume", 0.5);
    rig.commit();
    rig.press ("a");
    rig.render (0.3);
    const double half = rig.rms (0.1, 0.3);
    CHECK_NEAR (half, 0.5 * std::sqrt (0.5), 0.03);   // sine RMS; a centred voice passes at full level (as in the page)
    rig.set ("muted", true);
    rig.commit();
    rig.render (0.2);
    CHECK (rig.rms (0.05, 0.2) < 1e-6);
}

TEST ("engine: a sound's delay echoes after its delay time; its reverb rings after it stops")
{
    Rig dly;
    dly.add ("a", sine (440, 0.1), [] (auto& s) { s.repeat = 1; s.release = 0.005f; s.delayMix = 1; s.delayTime = 0.3f; s.delayFeedback = 0; });
    dly.commit();
    dly.press ("a");
    dly.render (0.6);
    CHECK (dly.rms (0.12, 0.28) < 1e-4);   // gap
    CHECK (dly.rms (0.31, 0.39) > 0.05);   // echo
    CHECK (dly.rms (0.45, 0.6) < 1e-4);    // no feedback: one echo

    Rig rev;
    rev.add ("a", sine (440, 0.1), [] (auto& s) { s.repeat = 1; s.release = 0.005f; s.reverbMix = 1; s.reverbSize = 1; s.reverbDecay = 2; });
    rev.commit();
    // the impulse loads on JUCE's background thread, in real time; offline rendering outruns it
    std::this_thread::sleep_for (std::chrono::milliseconds (300));
    rev.render (0.1);
    rev.press ("a");
    rev.render (1.0);
    CHECK (rev.rms (0.2, 0.5) > 1e-3);     // tail after the 0.1 s note
    CHECK (rev.rms (0.95, 1.0) < rev.rms (0.2, 0.3));
}

TEST ("engine: master delay repeats the whole mix")
{
    Rig rig;
    rig.add ("a", sine (440, 0.1), [] (auto& s) { s.repeat = 1; s.release = 0.005f; });
    juce::var fx (new juce::DynamicObject());
    fx.getDynamicObject()->setProperty ("delayMix", 1.0);
    fx.getDynamicObject()->setProperty ("delayTime", 0.25);
    fx.getDynamicObject()->setProperty ("delayFeedback", 0.0);
    rig.set ("fx", fx);
    rig.commit();
    rig.press ("a");
    rig.render (0.5);
    CHECK (rig.rms (0.26, 0.34) > 0.05);
}

TEST ("engine: a grain cloud plays grains around GRAIN POS at the note's pitch, for as long as REPEAT says")
{
    Rig rig;
    // 1 s of 440 Hz, grains of 80 ms (GRAIN 12.5 Hz), two overlapping on each side
    rig.add ("a", sine (440, 1.0), [] (auto& s) { cloud (s, 12.5f); s.repeat = 1; s.release = 0.01f; });
    rig.commit();
    rig.press ("a");
    rig.render (1.4);
    CHECK (rig.rms (0.1, 0.9) > 0.1);                      // overlapping grains keep it sounding
    // grains keep the sample's pitch; overlapping grains at different phases smear the zero
    // crossings a little (as they do in the page), hence the wider tolerance
    CHECK_NEAR (rig.hz (0.2, 0.8), 440.0, 15.0);
    CHECK (rig.rms (1.15, 1.4) < 1e-4);                     // one pass of the 1 s clip, then done
    CHECK (rig.engine.activeVoices() == 0);
}

TEST ("engine: STRETCH plays at half speed without changing pitch")
{
    Rig rig;
    rig.add ("a", sine (440, 0.5), [] (auto& s) { s.stretch = true; s.speed = 0.5f; s.repeat = 1; s.release = 0.01f; });
    rig.commit();
    rig.press ("a");
    rig.render (1.3);
    CHECK (rig.rms (0.1, 0.9) > 0.1);                       // a 0.5 s clip lasts 1 s
    CHECK_NEAR (rig.hz (0.2, 0.8), 440.0, 8.0);
    CHECK (rig.rms (1.1, 1.3) < 1e-4);
}

TEST ("engine: an SFZ region LFO on pitch makes vibrato")
{
    auto render = [] (float depth)
    {
        auto rig = std::make_unique<Rig>();
        auto& inst = rig->add ("inst", sine (440, 3));
        ssb::Zone z;
        z.sample = sine (440, 3);
        z.keycenter = 69;
        ssb::ZoneLfo l;
        l.target = ssb::ZoneLfo::Target::pitch;
        l.freq = 5;
        l.depth = depth;   // cents
        z.lfos.push_back (l);
        inst.zones = { z };
        rig->set ("play", true);
        rig->set ("playable", "inst");
        rig->commit();
        rig->render (1.0, { on (0.0, 69) });
        return rig;
    };
    auto flat = render (0), vib = render (100);
    // at the LFO's peak (t = 50 ms into a 5 Hz cycle) the pitch is a semitone up
    CHECK_NEAR (flat->hz (0.03, 0.07), 440.0, 15.0);
    CHECK (vib->hz (0.03, 0.07) > 455.0);
    CHECK (vib->hz (0.13, 0.17) < 425.0);
}

TEST ("engine: a synced delay follows the host's tempo, not the page's")
{
    Rig rig;
    rig.add ("a", sine (440, 0.05), [] (auto& s) { s.repeat = 1; s.release = 0.005f; });
    juce::var fx (new juce::DynamicObject());
    fx.getDynamicObject()->setProperty ("delayMix", 1.0);
    fx.getDynamicObject()->setProperty ("delayFeedback", 0.0);
    fx.getDynamicObject()->setProperty ("delaySync", true);
    fx.getDynamicObject()->setProperty ("delayBeats", 1.0);   // a quarter note
    rig.set ("fx", fx);
    rig.set ("bpm", 90);                                     // the page's own tempo: 0.667 s
    rig.commit();
    rig.host = { 120, 0, true };                              // the host's: 0.5 s
    rig.press ("a");
    rig.render (0.8);
    CHECK (rig.rms (0.49, 0.56) > 0.05);
    CHECK (rig.rms (0.64, 0.72) < 1e-3);
}

namespace
{
/** A 2 s log sweep, 200 Hz -> 2 kHz: where a grain reads from, you can hear (and measure). */
ssb::SamplePtr sweep()
{
    auto s = std::make_shared<ssb::Sample>();
    s->rate = rate;
    const auto n = (size_t) (2 * rate);
    s->channels.assign (1, std::vector<float> (n));
    double ph = 0;
    for (size_t i = 0; i < n; ++i)
    {
        ph += 2 * ssb::dsp::pi * 200 * std::pow (10.0, (double) i / rate / 2) / rate;
        s->channels[0][i] = (float) (0.8 * std::sin (ph));
    }
    return s;
}

/** The period (s) of the output's amplitude envelope: how often grains start. */
double envelopePeriod (const std::vector<float>& x, double from, double to)
{
    const auto a = (size_t) (from * rate), b = (size_t) (to * rate);
    std::vector<double> env;
    for (size_t i = a; i + 96 < b; i += 96)   // 2 ms windows
    {
        double s = 0;
        for (size_t j = 0; j < 96; ++j) s += std::abs (x[i + j]);
        env.push_back (s / 96);
    }
    double mean = 0;
    for (auto e : env) mean += e;
    mean /= (double) env.size();
    size_t best = 0;
    double bestScore = -1;
    for (size_t lag = 8; lag < env.size() / 2; ++lag)   // 16 ms and up
    {
        double s = 0;
        for (size_t i = 0; i + lag < env.size(); ++i) s += (env[i] - mean) * (env[i + lag] - mean);
        if (s > bestScore) { bestScore = s; best = lag; }
    }
    return (double) best * 96 / rate;
}
} // namespace

// ------------------------------------------------------------------ the grain cloud: Granulator II's voice

namespace
{
/** A cloud played from the keyboard at MIDI `note` (the pad's ROOT is C4). */
std::unique_ptr<Rig> keyed (ssb::SamplePtr sample, std::function<void (ssb::Settings&)> tweak, int note, double seconds)
{
    auto rig = std::make_unique<Rig>();
    rig->add ("a", sample, std::move (tweak));
    rig->set ("play", true);
    rig->set ("playable", "a");
    rig->commit();
    rig->render (seconds, { on (0.0, note) });
    return rig;
}
} // namespace

TEST ("engine: GRAIN POS picks where in the sample grains read from")
{
    auto play = [] (float pos)
    {
        auto rig = std::make_unique<Rig>();
        rig->add ("a", sweep(), [pos] (auto& s) { cloud (s, 15); s.grainPos = pos; });
        rig->commit();
        rig->press ("a");
        rig->render (0.6);
        return rig->hz (0.15, 0.55);
    };
    // 200 * 10^pos Hz; a 66 ms grain hears a little of the sweep, hence the tolerance
    CHECK_NEAR (play (0.1f), 252.0, 30.0);
    CHECK_NEAR (play (0.5f), 632.0, 60.0);
    CHECK_NEAR (play (0.9f), 1589.0, 120.0);
}

TEST ("engine: GRAIN is how often grains start; each lasts one period, and two overlap")
{
    Rig rig;
    rig.add ("a", dc (0.5f, 3), [] (auto& s) { cloud (s, 10); });
    rig.commit();
    rig.press ("a");
    rig.render (1.2);
    // two sine windows half a period apart: a ripple at twice GRAIN (sin + cos)
    CHECK_NEAR (envelopePeriod (rig.left, 0.1, 1.1), 0.05, 0.006);
    std::vector<ssb::GrainView> grains;
    rig.engine.readGrains (grains, 1000);
    // two at the note on each side, then two a period on each side
    CHECK_NEAR ((double) grains.size(), 4 + 1.2 * 10 * 2 * 2, 3.0);
    for (const auto& g : grains)
        CHECK_NEAR (g.dur, 0.1, 1e-4);
}

TEST ("engine: short grains are a pitched tone that follows the keyboard (G<KEY)")
{
    auto at = [] (int note, float key)
    {
        return keyed (sine (500, 2), [key] (auto& s) { cloud (s, 100); s.grainFreqKey = key; }, note, 0.6);
    };
    const auto root = at (60, 1);
    CHECK (root->rms (0.1, 0.5) > 0.05);
    CHECK (periodicity (root->left, 0.1, 0.5, 0.01) > 0.98);       // 100 Hz: GRAIN
    const auto octave = at (72, 1);
    CHECK (periodicity (octave->left, 0.1, 0.5, 0.005) > 0.98);    // an octave up: 200 Hz
    const auto fixed = at (72, 0);
    CHECK (periodicity (fixed->left, 0.1, 0.5, 0.01) > 0.98);      // G<KEY 0: GRAIN stays put
    CHECK (fixed->rms (0.1, 0.5) > 0.05);
}

TEST ("engine: T<KEY decides whether the note transposes the sample inside the grains")
{
    auto at = [] (float tuneKey)
    {
        return keyed (sine (440, 2), [tuneKey] (auto& s) { cloud (s, 20); s.grainFreqKey = 0; s.grainTuneKey = tuneKey; }, 72, 0.6);
    };
    CHECK_NEAR (at (1)->hz (0.1, 0.5), 880.0, 20.0);
    CHECK_NEAR (at (0)->hz (0.1, 0.5), 440.0, 15.0);
}

TEST ("engine: SPREAD pulls the left and right grain rates apart, up to two octaves")
{
    Rig rig;
    rig.add ("a", sine (500, 2), [] (auto& s) { cloud (s, 100); s.grainStereo = 1; });
    rig.commit();
    rig.press ("a");
    rig.render (0.6);
    CHECK (periodicity (rig.left, 0.1, 0.5, 0.02) > 0.98);    // GRAIN / 2
    CHECK (periodicity (rig.right, 0.1, 0.5, 0.005) > 0.98);  // GRAIN × 2
}

TEST ("engine: SHAPE squares the window: the overlapping pair sits flat between short bumps")
{
    auto flat = [] (float shape)
    {
        Rig rig;
        rig.add ("a", dc (0.5f, 2), [shape] (auto& s) { cloud (s, 20); s.grainWindow = shape; });
        rig.commit();
        rig.press ("a");
        rig.render (0.6);
        float top = 0;
        for (auto i = (size_t) (0.1 * rate); i < (size_t) (0.5 * rate); ++i) top = std::max (top, std::abs (rig.left[i]));
        // the share of the time the pair is at one window's full level (√2 below the sine pair's top)
        size_t at = 0, n = 0;
        const float one = shape > 0 ? top / 1.63f : top / std::sqrt (2.0f);
        for (auto i = (size_t) (0.1 * rate); i < (size_t) (0.5 * rate); ++i, ++n)
            at += std::abs (std::abs (rig.left[i]) - one) < 0.02f * one;
        return (double) at / (double) n;
    };
    CHECK (flat (0) < 0.3);   // sine windows: always between 1 and √2 of one
    CHECK (flat (1) > 0.9);   // squares half a period wide end to end, a bump where they meet (as in Granulator II)
}

TEST ("engine: SPRAY offsets each grain by up to ±SPRAY ms, or to one side")
{
    auto spread = [] (ssb::SpraySign sign)
    {
        Rig rig;
        rig.add ("a", sine (440, 2), [sign] (auto& s) { cloud (s, 20); s.grainSpray = 200; s.grainSpraySign = sign; });
        rig.commit();
        rig.press ("a");
        rig.render (1.0);
        std::vector<ssb::GrainView> grains;
        rig.engine.readGrains (grains, 1000);
        double lo = 9, hi = -9;
        for (const auto& g : grains)
        {
            lo = std::min (lo, (double) g.pos);
            hi = std::max (hi, (double) g.pos);
        }
        return std::pair { lo, hi };
    };
    const auto [lo, hi] = spread (ssb::SpraySign::sym);   // around POS: 1 s
    CHECK (lo >= 0.8 - 1e-4 && hi <= 1.2 + 1e-4);
    CHECK (hi - lo > 0.3);
    const auto [rlo, rhi] = spread (ssb::SpraySign::right);
    CHECK (rlo >= 1.0 - 1e-4 && rhi > 1.15);
}

TEST ("engine: VOID drops √amount of the grains; T<RND gives each its own pitch")
{
    Rig rig;
    rig.add ("a", sine (440, 3), [] (auto& s)
    {
        cloud (s, 20);
        s.grainAmpMode = ssb::GrainAmpMode::drop;
        s.grainAmp = 0.49f;
        s.grainTuneRnd = 1;
    });
    rig.commit();
    rig.press ("a");
    rig.render (2.0);
    std::vector<ssb::GrainView> grains;
    rig.engine.readGrains (grains, 1000);
    int dropped = 0;
    double lo = 9, hi = 0;
    for (const auto& g : grains)
    {
        dropped += g.gain < 0.5f;
        lo = std::min (lo, (double) g.rate);
        hi = std::max (hi, (double) g.rate);
    }
    CHECK_NEAR ((double) dropped / (double) grains.size(), 0.7, 0.1);
    CHECK (lo >= 0.5 - 1e-6 && hi <= 1.5 + 1e-6 && hi - lo > 0.7);
}

TEST ("engine: FM swings where the grains read, so their pitch wobbles")
{
    auto wobble = [] (bool fm)
    {
        Rig rig;
        rig.add ("a", sine (440, 3), [fm] (auto& s) { cloud (s, 20); s.grainFm = fm; s.grainFmFreq = 3; s.grainFmAmount = 250; });
        rig.commit();
        rig.press ("a");
        rig.render (1.0);
        double lo = 1e9, hi = 0;
        for (double t = 0.1; t < 0.9; t += 0.05)
        {
            const double f = rig.hz (t, t + 0.05);
            lo = std::min (lo, f);
            hi = std::max (hi, f);
        }
        return hi - lo;
    };
    CHECK (wobble (false) < 15);
    CHECK (wobble (true) > 40);   // ± 5 ms at 3 Hz: ± 9 %
}

TEST ("engine: SCAN moves POS from each note at 100 / TIME times real time, for its distance")
{
    auto play = [] (float time, float dist)
    {
        auto rig = std::make_unique<Rig>();
        rig->add ("a", sweep(), [time, dist] (auto& s)
        {
            cloud (s, 15);
            s.grainPos = 0;
            s.grainScanOn = true;
            s.grainScanTime = time;
            s.grainScanDist = dist;
        });
        rig->commit();
        rig->press ("a");
        rig->render (1.0);
        return std::pair { rig->hz (0.15, 0.25), rig->hz (0.75, 0.85) };
    };
    // the 2 s sweep is at 200 × 10^(t / 2) Hz t seconds in
    const auto [early, late] = play (100, 1);
    CHECK_NEAR (early, 252.0, 35.0);
    CHECK_NEAR (late, 502.0, 60.0);
    CHECK_NEAR (play (200, 1).second, 317.0, 40.0);    // half speed
    CHECK_NEAR (play (100, 0.25f).second, 355.0, 45.0); // stopped half a second in
}

TEST ("engine: the grains it starts are reported for the editor's waveform")
{
    Rig rig;
    rig.add ("a", sine (440, 1.0), [] (auto& s) { cloud (s, 20); s.grainPos = 0.25f; });
    rig.commit();
    rig.press ("a", 0.5f);
    rig.render (0.5);
    std::vector<ssb::GrainView> grains;
    rig.engine.readGrains (grains, 1000);
    CHECK_NEAR ((double) grains.size(), 4 + 0.5 * 20 * 2 * 2, 3.0);
    std::vector<ssb::VoiceView> voices;
    double time = 0;
    CHECK (rig.engine.readVoices (voices, time));
    CHECK (voices.size() == 1 && voices[0].grains && std::abs (voices[0].velocity - 0.5f) < 1e-6f);
    int left = 0;
    for (const auto& g : grains)
    {
        CHECK (g.voice == voices[0].id);
        CHECK_NEAR (g.pos, 0.25, 0.001);   // at GRAIN POS
        CHECK_NEAR (g.dur, 0.05, 1e-4);
        CHECK (g.when >= 0 && g.when <= time);
        left += g.stream == 0;
    }
    CHECK_NEAR ((double) left, grains.size() / 2.0, 1.0);   // left and right
    rig.engine.readGrains (grains, 1000);
    CHECK (grains.empty());   // each one once
}

TEST ("engine: clouds saved before the Granulator II model keep their grain length and POS")
{
    auto o = new juce::DynamicObject();   // before 0.2: a SIZE > 0 was on
    o->setProperty ("grainSize", 100);
    o->setProperty ("grainDensity", 4);
    const auto v01 = ssb::settingsFromJson (juce::var (o));
    CHECK (v01.grain);
    CHECK_NEAR (v01.grainFreq, 10.0, 1e-4);
    auto last = new juce::DynamicObject();   // the version before: sprayed, scattered, 80 ms
    last->setProperty ("grain", true);
    last->setProperty ("grainSize", 80);
    last->setProperty ("grainPos", 0.3);
    last->setProperty ("grainWidth", 0.1);
    last->setProperty ("grainScatter", 0.5);
    const auto v03 = ssb::settingsFromJson (juce::var (last));
    CHECK (v03.grain);
    CHECK_NEAR (v03.grainFreq, 12.5, 1e-4);
    CHECK_NEAR (v03.grainPos, 0.3, 1e-6);
    CHECK_NEAR (v03.grainSpray, 0.0, 1e-9);
    auto off = new juce::DynamicObject();
    off->setProperty ("grainSize", 0);
    CHECK (! ssb::settingsFromJson (juce::var (off)).grain);
    auto now = new juce::DynamicObject();
    now->setProperty ("grain", true);
    now->setProperty ("grainFreq", 77);
    auto long_ = new juce::DynamicObject();   // a grain the length of a 20 s sample: below Granulator II's floor
    long_->setProperty ("grain", true);
    long_->setProperty ("grainFreq", 0.05);
    CHECK_NEAR (ssb::settingsFromJson (juce::var (long_)).grainFreq, 0.05, 1e-6);
    now->setProperty ("grainSymmetry", "fall");
    now->setProperty ("grainAmpMode", "void");
    now->setProperty ("grainSpraySign", "left");
    now->setProperty ("grainScanOn", true);
    const auto today = ssb::settingsFromJson (juce::var (now));
    CHECK_NEAR (today.grainFreq, 77.0, 1e-6);
    CHECK (today.grainSymmetry == ssb::GrainSymmetry::fall);
    CHECK (today.grainAmpMode == ssb::GrainAmpMode::drop);
    CHECK (today.grainSpraySign == ssb::SpraySign::left);
    CHECK (today.grainScanOn);
}

TEST ("engine: turning GRAIN up takes effect at once")
{
    Rig rig;
    rig.add ("a", dc (0.5f, 5), [] (auto& s) { cloud (s, 1); });
    rig.commit();
    rig.press ("a");
    rig.render (0.3);
    std::vector<ssb::GrainView> grains;
    rig.engine.readGrains (grains, 1000);
    CHECK (grains.size() == 4);   // the note's; the next are half a second off
    rig.add ("a", dc (0.5f, 5), [] (auto& s) { cloud (s, 40); });
    rig.commit();
    rig.render (0.3);
    rig.engine.readGrains (grains, 1000);
    CHECK_NEAR ((double) grains.size(), 0.3 * 40 * 2 * 2, 4.0);
}

TEST ("engine: PITCH reaches the grains that start after it turns")
{
    Rig rig;
    rig.add ("a", sine (440, 4), [] (auto& s) { cloud (s, 20); });
    rig.commit();
    rig.press ("a");
    rig.render (0.4);
    CHECK_NEAR (rig.hz (0.1, 0.4), 440.0, 6.0);
    rig.add ("a", sine (440, 4), [] (auto& s) { cloud (s, 20); s.pitch = 12; });
    rig.commit();
    rig.render (0.4);
    CHECK_NEAR (rig.hz (0.1, 0.4), 880.0, 12.0);
}

TEST ("engine: turning GRAIN POS while a cloud sounds moves it")
{
    Rig rig;
    rig.add ("a", sweep(), [] (auto& s) { cloud (s, 15); s.grainPos = 0.1f; });
    rig.commit();
    rig.press ("a");
    rig.render (0.5);
    CHECK_NEAR (rig.hz (0.15, 0.45), 252.0, 30.0);
    rig.add ("a", sweep(), [] (auto& s) { cloud (s, 15); s.grainPos = 0.9f; });
    rig.commit();   // the page's knob, as the next performance
    rig.render (0.5);
    CHECK_NEAR (rig.hz (0.15, 0.45), 1589.0, 120.0);
}


TEST ("engine: cached panners preserve mono and stereo pan laws")
{
    ssb::dsp::PreparedPan cached;
    for(bool mono:{false,true})for(int position=-20;position<=20;++position)
        for(float left:{-.7f,0.f,.9f})for(float right:{-.4f,0.f,.3f}){
            const float pan=position*.07f;float expectedL=0,expectedR=0,actualL=0,actualR=0;
            ssb::dsp::pan(pan,mono,left,right,expectedL,expectedR);
            cached.set(pan,mono);cached.process(left,right,actualL,actualR);
            CHECK_NEAR(actualL,expectedL,.0000001);CHECK_NEAR(actualR,expectedR,.0000001);
        }
}

TEST ("engine: cached grain window matches SSB formula across shapes and symmetries")
{
    for (auto sym : {ssb::GrainSymmetry::std, ssb::GrainSymmetry::fall, ssb::GrainSymmetry::rise, ssb::GrainSymmetry::noiz})
        for (int shape = 0; shape <= 1000; ++shape)
        {
            const float amount = shape / 1000.f;
            ssb::dsp::WindowTable table;
            ssb::dsp::windowTable (amount, sym, table);
            const double f = 1 + std::pow (0.04 * std::clamp (amount * 100.0, 0.0, 100.0), 3.0);
            for (int i = 0; i <= ssb::dsp::windowPoints; ++i)
            {
                const double x = sym == ssb::GrainSymmetry::fall ? .5*i+256 : sym == ssb::GrainSymmetry::rise ? 512-.5*i
                    : sym == ssb::GrainSymmetry::noiz ? ssb::dsp::noizTable()[size_t(i)]+256 : i;
                const double u = x <= 256 ? x-128 : 385-x;
                const double expected = std::sin (ssb::dsp::pi * std::clamp (u*f+128, 0.0, 256.0) / 512);
                CHECK (std::abs (table[size_t(i)] - expected) < 1e-7);
            }
        }
}
