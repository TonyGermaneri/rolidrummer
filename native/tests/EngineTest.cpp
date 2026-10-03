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

/** A grain cloud with nothing random about it: no spray, a steady clock, centred. */
void steadyCloud (ssb::Settings& s, float ms, float perSecond)
{
    s.grain = true;
    s.grainSize = ms;
    s.grainRate = perSecond;
    s.grainWidth = 0;
    s.grainScatter = 0;
    s.grainSpread = 0;
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
    // 1 s of 440 Hz, grains of 80 ms, 50 a second (four overlapping)
    rig.add ("a", sine (440, 1.0), [] (auto& s) { steadyCloud (s, 80, 50); s.repeat = 1; s.release = 0.01f; });
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

TEST ("engine: GRAIN POS picks where in the sample grains read from")
{
    auto play = [] (float pos)
    {
        auto rig = std::make_unique<Rig>();
        rig->add ("a", sweep(), [pos] (auto& s) { steadyCloud (s, 60, 66.7f); s.grainPos = pos; });
        rig->commit();
        rig->press ("a");
        rig->render (0.6);
        return rig->hz (0.15, 0.55);
    };
    // 200 * 10^pos Hz; overlapping grains blur it a little
    CHECK_NEAR (play (0.1f), 252.0, 30.0);
    CHECK_NEAR (play (0.5f), 632.0, 60.0);
    CHECK_NEAR (play (0.9f), 1589.0, 120.0);
}

TEST ("engine: GRAIN SIZE sets how long each grain is (and so the grain rhythm)")
{
    auto period = [] (float ms)
    {
        auto rig = std::make_unique<Rig>();
        rig->add ("a", sine (440, 3), [ms] (auto& s) { steadyCloud (s, ms, 1000.0f / ms); });
        rig->commit();
        rig->press ("a");
        rig->render (1.2);
        return envelopePeriod (rig->left, 0.1, 1.1);
    };
    // one grain every SIZE: grains end to end, one Hann window each
    CHECK_NEAR (period (40), 0.040, 0.006);
    CHECK_NEAR (period (150), 0.150, 0.012);
}

namespace
{
/** Start times (seconds) of the grains in `x`: where it rises from silence, at least `gap` apart. */
std::vector<double> onsets (const std::vector<float>& x, double from, double to, double gap = 0.0005)
{
    std::vector<double> out;
    double last = -1;
    for (auto i = (size_t) (from * rate) + 1; i < (size_t) (to * rate) && i < x.size(); ++i)
        if (std::abs (x[i]) > 1e-3f && std::abs (x[i - 1]) <= 1e-3f)
        {
            const double t = (double) i / rate;
            if (last < 0 || t - last >= gap) out.push_back (t);
            last = t;
        }
    return out;
}
} // namespace

TEST ("engine: RATE is grains per second whatever their SIZE: a sparse cloud has gaps")
{
    Rig rig;
    rig.add ("a", dc (0.5f, 2), [] (auto& s) { steadyCloud (s, 10, 10); });   // 10 ms grains, 10 a second
    rig.commit();
    rig.press ("a");
    rig.render (1.2);
    const auto starts = onsets (rig.left, 0.05, 1.15);
    CHECK_NEAR ((double) starts.size(), 11.0, 1.0);
    size_t silent = 0, n = 0;
    for (auto i = (size_t) (0.1 * rate); i < (size_t) (1.1 * rate); ++i, ++n)
        silent += std::abs (rig.left[i]) < 1e-4f;
    CHECK_NEAR ((double) silent / (double) n, 0.9, 0.03);   // sounding 10 % of the time
}

TEST ("engine: a grain can be one sample long, and hundreds start a second")
{
    Rig rig;
    rig.add ("a", dc (0.5f, 2), [] (auto& s) { steadyCloud (s, 1000.0f / 48000.0f, 200); s.cutoff = 20000; });
    rig.commit();
    rig.press ("a");
    rig.render (1.1);
    const auto starts = onsets (rig.left, 0.05, 1.05, 0.002);
    CHECK_NEAR ((double) starts.size(), 200.0, 3.0);
    // each is a click: one sample at the grain's level, then (after the 20 kHz filter rings) nothing
    size_t loud = 0;
    for (auto i = (size_t) (0.05 * rate); i < (size_t) (1.05 * rate); ++i)
        loud += std::abs (rig.left[i]) > 0.1f;
    CHECK (loud >= 200 && loud < 200 * 4);
}

TEST ("engine: SHAPE runs the grain window from square to Hann")
{
    auto firstSample = [] (float shape)
    {
        Rig rig;
        rig.add ("a", dc (0.5f, 2), [shape] (auto& s) { steadyCloud (s, 20, 10); s.grainShape = shape; });
        rig.commit();
        rig.press ("a");
        rig.render (0.5);
        const auto starts = onsets (rig.left, 0.05, 0.45);
        const auto i = (size_t) (starts.at (1) * rate);
        float peak = 0;   // the grain's own top, whatever the voice's volume and pan law make of it
        for (auto j = i; j < i + (size_t) (0.02 * rate); ++j) peak = std::max (peak, std::abs (rig.left[j]));
        return std::abs (rig.left[i + 2]) / peak;
    };
    CHECK (firstSample (0) > 0.9f);    // square: full level from the start
    CHECK (firstSample (1) < 0.05f);   // Hann: fades in
}

TEST ("engine: SCATTER moves grains from a steady clock to random (Poisson) times, at the same rate")
{
    auto intervals = [] (float scatter)
    {
        Rig rig;
        rig.add ("a", dc (0.5f, 30), [scatter] (auto& s) { steadyCloud (s, 1, 40); s.grainScatter = scatter; });
        rig.commit();
        rig.press ("a");
        rig.render (20);
        const auto t = onsets (rig.left, 0.1, 19.9, 0.0015);
        double mean = 0, var = 0;
        for (size_t i = 1; i < t.size(); ++i) mean += t[i] - t[i - 1];
        mean /= (double) (t.size() - 1);
        for (size_t i = 1; i < t.size(); ++i) var += std::pow (t[i] - t[i - 1] - mean, 2);
        return std::pair { mean, std::sqrt (var / (double) (t.size() - 1)) / mean };   // mean, spread (CV)
    };
    const auto [steadyMean, steadyCv] = intervals (0);
    const auto [randomMean, randomCv] = intervals (1);
    CHECK_NEAR (steadyMean, 0.025, 0.0005);
    CHECK (steadyCv < 0.01);
    CHECK_NEAR (randomMean, 0.025, 0.003);   // same rate on average (a few near-simultaneous grains merge)
    CHECK (randomCv > 0.7);                  // exponential: CV 1 (merged neighbours trim it a little)
}

TEST ("engine: the grains it starts are reported for the editor's waveform")
{
    Rig rig;
    rig.add ("a", sine (440, 1.0), [] (auto& s) { steadyCloud (s, 50, 20); s.grainPos = 0.25f; s.grainReverse = 1; });
    rig.commit();
    rig.press ("a", 0.5f);
    rig.render (0.5);
    std::vector<ssb::GrainView> grains;
    rig.engine.readGrains (grains, 1000);
    CHECK_NEAR ((double) grains.size(), 10.0, 1.0);
    std::vector<ssb::VoiceView> voices;
    double time = 0;
    CHECK (rig.engine.readVoices (voices, time));
    CHECK (voices.size() == 1 && voices[0].grains && std::abs (voices[0].velocity - 0.5f) < 1e-6f);
    for (const auto& g : grains)
    {
        CHECK (g.voice == voices[0].id);
        CHECK (g.reverse);
        CHECK_NEAR (g.pos + g.len / 2, 0.25, 0.001);   // centred on GRAIN POS
        CHECK_NEAR (g.dur, 0.05, 1e-4);
        CHECK (g.when >= 0 && g.when <= time);
    }
    CHECK_NEAR (grains[1].when - grains[0].when, 0.05, 1e-3);
    rig.engine.readGrains (grains, 1000);
    CHECK (grains.empty());   // each one once
}

TEST ("engine: a session saved before 0.2 keeps its grain cloud (density was an overlap count)")
{
    auto o = new juce::DynamicObject();
    o->setProperty ("grainSize", 100);
    o->setProperty ("grainDensity", 4);
    const auto old = ssb::settingsFromJson (juce::var (o));
    CHECK (old.grain);
    CHECK_NEAR (old.grainRate, 40.0, 1e-3);
    CHECK_NEAR (old.grainWidth, 0.0, 1e-9);   // what it had, not today's defaults
    auto off = new juce::DynamicObject();
    off->setProperty ("grainSize", 0);
    const auto plain = ssb::settingsFromJson (juce::var (off));
    CHECK (! plain.grain);
    CHECK_NEAR (plain.grainRate, ssb::Settings {}.grainRate, 1e-6);
}

TEST ("engine: turning RATE up takes effect at once, not after the grain the old rate had drawn")
{
    Rig rig;
    rig.add ("a", dc (0.5f, 5), [] (auto& s) { steadyCloud (s, 2, 1); });   // one grain a second
    rig.commit();
    rig.press ("a");
    rig.render (0.3);
    CHECK (onsets (rig.left, 0, 0.3).size() == 1);   // the note's first grain; the next is due at 1 s
    rig.add ("a", dc (0.5f, 5), [] (auto& s) { steadyCloud (s, 2, 50); });
    rig.commit();                                     // the knob turned to 50 a second
    rig.render (0.3);
    const auto starts = onsets (rig.left, 0, 0.3);
    CHECK_NEAR ((double) starts.size(), 15.0, 1.0);
    CHECK (starts.at (0) < 0.005);                    // the first right away
}

TEST ("engine: turning PITCH retunes grains that are already sounding")
{
    Rig rig;
    rig.add ("a", sine (440, 4), [] (auto& s) { steadyCloud (s, 1500, 0.5f); s.grainShape = 0; });   // one long grain
    rig.commit();
    rig.press ("a");
    rig.render (0.4);
    CHECK_NEAR (rig.hz (0.1, 0.4), 440.0, 3.0);
    rig.add ("a", sine (440, 4), [] (auto& s) { steadyCloud (s, 1500, 0.5f); s.grainShape = 0; s.pitch = 12; });
    rig.commit();
    rig.render (0.4);
    CHECK_NEAR (rig.hz (0.02, 0.4), 880.0, 6.0);     // the same grain, an octave up, straight away
}

TEST ("engine: turning GRAIN POS while a cloud sounds moves it")
{
    Rig rig;
    rig.add ("a", sweep(), [] (auto& s) { steadyCloud (s, 60, 66.7f); s.grainPos = 0.1f; });
    rig.commit();
    rig.press ("a");
    rig.render (0.5);
    CHECK_NEAR (rig.hz (0.15, 0.45), 252.0, 30.0);
    rig.add ("a", sweep(), [] (auto& s) { steadyCloud (s, 60, 66.7f); s.grainPos = 0.9f; });
    rig.commit();   // the page's knob, as the next performance
    rig.render (0.5);
    CHECK_NEAR (rig.hz (0.15, 0.45), 1589.0, 120.0);
}

TEST ("engine: fast grain windows match the original cosine envelope")
{
    ssb::dsp::GrainWindowTable table;
    for(int length:{1,2,3,7,31,64,127,1000,4320,9601})
        for(float taper:{0.f,.0001f,.01f,.2f,.5f,.99f,1.f})
            for(int i=0;i<length;++i){
                const float reference=ssb::dsp::grainWindow(i,length,taper);
                CHECK_NEAR(table.at(i,length,taper),reference,.0000002);
                CHECK_NEAR(table.atPhase((i+.5)*(1.0/length),taper>0?2.0/taper:0),reference,.0000002);
            }
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
