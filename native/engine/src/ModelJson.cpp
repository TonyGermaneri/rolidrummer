#include <ssb/ModelJson.h>

namespace ssb
{

namespace
{

float num (const juce::var& o, const char* key, float fallback)
{
    const auto v = o.getProperty (key, {});
    return v.isVoid() || v.isUndefined() ? fallback : (float) (double) v;
}

bool flag (const juce::var& o, const char* key, bool fallback)
{
    const auto v = o.getProperty (key, {});
    return v.isVoid() || v.isUndefined() ? fallback : (bool) v;
}

juce::String str (const juce::var& o, const char* key)
{
    return o.getProperty (key, {}).toString();
}

FilterType filterType (const juce::String& s)
{
    if (s == "highpass")  return FilterType::highpass;
    if (s == "bandpass")  return FilterType::bandpass;
    if (s == "lowshelf")  return FilterType::lowshelf;
    if (s == "highshelf") return FilterType::highshelf;
    if (s == "peaking")   return FilterType::peaking;
    if (s == "notch")     return FilterType::notch;
    if (s == "allpass")   return FilterType::allpass;
    return FilterType::lowpass;
}

LfoShape lfoShape (const juce::String& s)
{
    if (s == "triangle") return LfoShape::triangle;
    if (s == "square")   return LfoShape::square;
    if (s == "sawtooth") return LfoShape::sawtooth;
    if (s == "rampDown") return LfoShape::rampDown;
    if (s == "random")   return LfoShape::random;
    if (s == "smooth")   return LfoShape::smooth;
    return LfoShape::sine;
}

std::optional<ModSource> modSource (const juce::String& s)
{
    static const std::pair<const char*, ModSource> table[] {
        { "lfo1", ModSource::lfo1 }, { "lfo2", ModSource::lfo2 }, { "mod", ModSource::mod },
        { "aftertouch", ModSource::aftertouch }, { "velocity", ModSource::velocity }, { "bend", ModSource::bend },
        { "timbre", ModSource::timbre }, { "vco1", ModSource::vco1 }, { "vco2", ModSource::vco2 }, { "vco3", ModSource::vco3 },
    };
    for (const auto& [name, v] : table)
        if (s == name) return v;
    return std::nullopt;
}

std::optional<ModDest> modDest (const juce::String& s)
{
    static const std::pair<const char*, ModDest> table[] {
        { "pitch", ModDest::pitch }, { "cutoff", ModDest::cutoff }, { "resonance", ModDest::resonance },
        { "volume", ModDest::volume }, { "pan", ModDest::pan }, { "grainPos", ModDest::grainPos },
        { "grainSize", ModDest::grainSize }, { "delayMix", ModDest::delayMix }, { "reverbMix", ModDest::reverbMix },
    };
    for (const auto& [name, v] : table)
        if (s == name) return v;
    return std::nullopt;
}

LfoDef lfoFromJson (const juce::var& o, const LfoDef& fallback)
{
    if (! o.isObject()) return fallback;
    LfoDef l;
    l.shape = lfoShape (str (o, "shape"));
    l.rate = num (o, "rate", fallback.rate);
    l.sync = flag (o, "sync", false);
    l.divisionBeats = num (o, "divisionBeats", fallback.divisionBeats);
    return l;
}

Zone zoneFromJson (const juce::var& o, const SampleLookup& samples)
{
    Zone z;
    z.audioId = str (o, "audioId").toStdString();
    z.sample = samples ? samples (z.audioId) : nullptr;
    z.lokey = (int) num (o, "lokey", 0);
    z.hikey = (int) num (o, "hikey", 127);
    z.lovel = (int) num (o, "lovel", 1);
    z.hivel = (int) num (o, "hivel", 127);
    z.keycenter = num (o, "keycenter", 60);
    z.keytrack = num (o, "keytrack", 100);
    z.transpose = num (o, "transpose", 0);
    z.tune = num (o, "tune", 0);
    z.volume = num (o, "volume", 0);
    z.pan = num (o, "pan", 0);
    z.offset = num (o, "offset", 0);
    z.end = num (o, "end", 0);
    const auto mode = str (o, "loopMode");
    z.loopMode = mode == "one_shot" ? LoopMode::oneShot
               : mode == "loop_continuous" ? LoopMode::loopContinuous
               : mode == "loop_sustain" ? LoopMode::loopSustain : LoopMode::noLoop;
    z.loopStart = num (o, "loopStart", 0);
    z.loopEnd = num (o, "loopEnd", 0);
    z.seqLength = (int) num (o, "seqLength", 1);
    z.seqPosition = (int) num (o, "seqPosition", 1);
    z.lorand = num (o, "lorand", 0);
    z.hirand = num (o, "hirand", 1);
    if (const auto env = o.getProperty ("env", {}); env.isObject())
    {
        auto opt = [&] (const char* k) -> std::optional<float>
        {
            const auto v = env.getProperty (k, {});
            if (v.isVoid() || v.isUndefined()) return std::nullopt;
            return (float) (double) v;
        };
        z.env = { opt ("a"), opt ("d"), opt ("s"), opt ("r") };
    }
    if (const auto amp = o.getProperty ("amplitude", {}); ! amp.isVoid() && ! amp.isUndefined())
        z.amplitude = (float) (double) amp;
    z.releaseTrigger = str (o, "trigger") == "release";
    z.rtDecay = num (o, "rtDecay", 0);
    if (auto* ranges = o.getProperty ("ccRange", {}).getDynamicObject())
        for (const auto& p : ranges->getProperties())
            if (auto* pair = p.value.getArray(); pair && pair->size() >= 2)
                z.ccRange.push_back ({ p.name.toString().getIntValue(), (int) (*pair)[0], (int) (*pair)[1] });
    if (auto* mods = o.getProperty ("ccMods", {}).getArray())
        for (const auto& m : *mods)
        {
            ZoneCcMod c;
            c.cc = (int) num (m, "cc", 0);
            const auto t = str (m, "target");
            c.target = t == "amplitude" ? ZoneCcMod::Target::amplitude : t == "pan" ? ZoneCcMod::Target::pan
                     : t == "pitch" ? ZoneCcMod::Target::pitch : t == "cutoff" ? ZoneCcMod::Target::cutoff
                     : t == "resonance" ? ZoneCcMod::Target::resonance : ZoneCcMod::Target::volume;
            c.amount = num (m, "amount", 0);
            z.ccMods.push_back (c);
        }
    if (auto* lfos = o.getProperty ("lfos", {}).getArray())
        for (const auto& l : *lfos)
        {
            ZoneLfo z2;
            const auto t = str (l, "target");
            z2.target = t == "cutoff" ? ZoneLfo::Target::cutoff : t == "volume" ? ZoneLfo::Target::volume
                      : t == "pan" ? ZoneLfo::Target::pan : ZoneLfo::Target::pitch;
            const auto w = str (l, "wave");   // an OscillatorType
            z2.wave = w == "square" ? LfoShape::square : w == "sawtooth" ? LfoShape::sawtooth
                    : w == "triangle" ? LfoShape::triangle : LfoShape::sine;
            z2.freq = num (l, "freq", 1);
            z2.depth = num (l, "depth", 0);
            z2.delay = num (l, "delay", 0);
            z2.invert = flag (l, "invert", false);
            z.lfos.push_back (z2);
        }
    if (const auto f = o.getProperty ("filter", {}); f.isObject())
    {
        ZoneFilter zf;
        zf.type = filterType (str (f, "type"));
        zf.cutoff = num (f, "cutoff", 20000);
        zf.resonance = num (f, "resonance", 0);
        zf.keytrack = num (f, "keytrack", 0);
        zf.keycenter = num (f, "keycenter", 60);
        zf.veltrack = num (f, "veltrack", 0);
        if (const auto e = f.getProperty ("env", {}); e.isObject())
            zf.env = ZoneFilter::Env { num (e, "a", 0), num (e, "d", 0), num (e, "s", 1), num (e, "r", 0), num (e, "depth", 0) };
        z.filter = zf;
    }
    return z;
}

} // namespace

ModMatrix matrixFromJson (const juce::var& o)
{
    ModMatrix m;
    if (! o.isObject()) return m;
    m.lfo1 = lfoFromJson (o.getProperty ("lfo1", {}), m.lfo1);
    m.lfo2 = lfoFromJson (o.getProperty ("lfo2", {}), m.lfo2);
    if (auto* routes = o.getProperty ("routes", {}).getArray())
        for (const auto& r : *routes)
        {
            const auto src = modSource (str (r, "source"));
            const auto dst = modDest (str (r, "dest"));
            if (src && dst)
                m.routes.push_back ({ *src, *dst, num (r, "amount", 0) });
        }
    return m;
}

Settings settingsFromJson (const juce::var& o)
{
    Settings s;
    s.volume = num (o, "volume", s.volume);
    s.pan = num (o, "pan", s.pan);
    s.repeat = (int) std::lround (num (o, "repeat", (float) s.repeat));
    s.choke = (int) std::lround (num (o, "choke", 0));
    const auto mode = str (o, "mode");
    s.mode = mode == "restart" ? TriggerMode::restart : mode == "stack" ? TriggerMode::stack
           : mode == "hold" ? TriggerMode::hold : TriggerMode::stop;
    s.pitch = num (o, "pitch", 0);
    s.fine = num (o, "fine", 0);
    s.speed = std::max (0.01f, num (o, "speed", 1));
    s.stretch = str (o, "timeMode") == "stretch";
    s.clipIn = num (o, "clipIn", 0);
    s.clipOut = num (o, "clipOut", 1);
    s.attack = num (o, "attack", s.attack);
    s.decay = num (o, "decay", s.decay);
    s.sustain = num (o, "sustain", s.sustain);
    s.release = num (o, "release", s.release);
    s.filterType = filterType (str (o, "filterType"));
    s.cutoff = num (o, "cutoff", s.cutoff);
    s.resonance = num (o, "resonance", s.resonance);
    s.fEnvAmount = num (o, "fEnvAmount", 0);
    s.fAttack = num (o, "fAttack", s.fAttack);
    s.fDecay = num (o, "fDecay", s.fDecay);
    s.fSustain = num (o, "fSustain", s.fSustain);
    s.fRelease = num (o, "fRelease", s.fRelease);
    if (o.hasProperty ("grain"))
    {
        s.grain = flag (o, "grain", false);
        s.grainSize = num (o, "grainSize", s.grainSize);
        s.grainPos = num (o, "grainPos", s.grainPos);
        s.grainWidth = num (o, "grainWidth", s.grainWidth);
        s.grainRate = num (o, "grainRate", s.grainRate);
        s.grainShape = num (o, "grainShape", s.grainShape);
        s.grainJitter = num (o, "grainJitter", s.grainJitter);
        s.grainReverse = num (o, "grainReverse", s.grainReverse);
        s.grainSpread = num (o, "grainSpread", s.grainSpread);
        s.grainStreams = num (o, "grainStreams", s.grainStreams);
        s.grainScatter = num (o, "grainScatter", s.grainScatter);
        s.grainDrift = num (o, "grainDrift", s.grainDrift);
    }
    else if (const float size = num (o, "grainSize", 0); size > 0)
    {
        // saved before 0.2 (types.ts migrateGrains): size > 0 was on, grainDensity counted overlapping grains
        s.grain = true;
        s.grainSize = size;
        s.grainRate = std::clamp (num (o, "grainDensity", 2) / (size / 1000.0f), 0.5f, 1000.0f);
        s.grainShape = 1;
        s.grainPos = num (o, "grainPos", 0.5f);
        s.grainWidth = num (o, "grainWidth", 0);
        s.grainJitter = num (o, "grainJitter", 0);
        s.grainReverse = num (o, "grainReverse", 0);
        s.grainSpread = num (o, "grainSpread", 0);
        s.grainStreams = num (o, "grainStreams", 1);
        s.grainScatter = num (o, "grainScatter", 0);
        s.grainDrift = num (o, "grainDrift", 0);
    }
    s.delayTime = num (o, "delayTime", s.delayTime);
    s.delayFeedback = num (o, "delayFeedback", s.delayFeedback);
    s.delayMix = num (o, "delayMix", 0);
    s.reverbSize = num (o, "reverbSize", s.reverbSize);
    s.reverbDecay = num (o, "reverbDecay", s.reverbDecay);
    s.reverbMix = num (o, "reverbMix", 0);
    s.eqLow = num (o, "eqLow", 0);
    s.eqMid = num (o, "eqMid", 0);
    s.eqHigh = num (o, "eqHigh", 0);
    s.rootNote = num (o, "rootNote", 60);
    s.velAmount = num (o, "velAmount", 1);
    s.bendRange = num (o, "bendRange", 2);
    s.mod = matrixFromJson (o.getProperty ("mod", {}));
    return s;
}

std::shared_ptr<Sound> soundFromJson (const juce::var& o, const SampleLookup& samples)
{
    auto sound = std::make_shared<Sound>();
    sound->id = str (o, "id").toStdString();
    sound->audioId = str (o, "audioId").toStdString();
    sound->sample = samples ? samples (sound->audioId) : nullptr;
    sound->s = settingsFromJson (o.getProperty ("s", {}));
    if (auto* zones = o.getProperty ("zones", {}).getArray())
    {
        sound->zones.reserve ((size_t) zones->size());
        for (const auto& z : *zones)
            sound->zones.push_back (zoneFromJson (z, samples));
    }
    if (auto* links = o.getProperty ("links", {}).getArray())
        for (const auto& l : *links)
        {
            Link link;
            link.index = (int) num (l, "index", 0);
            link.soundId = str (l, "soundId").toStdString();
            link.level = num (l, "level", 1);
            link.audible = flag (l, "audible", true);
            link.track = flag (l, "track", true);
            link.transpose = num (l, "transpose", 0);
            link.fine = num (l, "fine", 0);
            link.fixedNote = (int) num (l, "fixedNote", 60);
            sound->links.push_back (std::move (link));
        }
    if (auto* cc = o.getProperty ("ccDefaults", {}).getDynamicObject())
        for (const auto& p : cc->getProperties())
            sound->ccDefaults.emplace_back (p.name.toString().getIntValue(), (float) (double) p.value);
    return sound;
}

PerformancePtr buildPerformance (const juce::var& meta, const std::map<std::string, std::shared_ptr<Sound>>& sounds)
{
    auto p = std::make_shared<Performance>();
    p->volume = num (meta, "volume", 0.8f);
    p->muted = flag (meta, "muted", false);
    p->mono = flag (meta, "mono", false);
    p->play = flag (meta, "play", false);
    p->mpe = flag (meta, "mpe", false);
    p->glide = num (meta, "glide", 0);
    p->mpeBendRange = num (meta, "mpeBendRange", 48);
    p->bpm = num (meta, "bpm", 120);
    if (const auto fx = meta.getProperty ("fx", {}); fx.isObject())
    {
        p->fx.chorusRate = num (fx, "chorusRate", p->fx.chorusRate);
        p->fx.chorusDepth = num (fx, "chorusDepth", p->fx.chorusDepth);
        p->fx.chorusMix = num (fx, "chorusMix", 0);
        p->fx.delayTime = num (fx, "delayTime", p->fx.delayTime);
        p->fx.delayFeedback = num (fx, "delayFeedback", p->fx.delayFeedback);
        p->fx.delayMix = num (fx, "delayMix", 0);
        p->fx.delaySync = flag (fx, "delaySync", false);
        p->fx.delayBeats = num (fx, "delayBeats", 0.75f);
        p->fx.reverbSize = num (fx, "reverbSize", p->fx.reverbSize);
        p->fx.reverbDecay = num (fx, "reverbDecay", p->fx.reverbDecay);
        p->fx.reverbMix = num (fx, "reverbMix", 0);
    }
    p->globalMod = matrixFromJson (meta.getProperty ("mod", {}));

    // Links are resolved on a copy, so the library's own Sound objects stay link-free and a sound
    // can be linked from several patches.
    std::map<std::string, SoundPtr> resolved;
    for (const auto& [id, s] : sounds)
    {
        if (s->links.empty())
        {
            resolved[id] = s;
            continue;
        }
        auto copy = std::make_shared<Sound> (*s);
        for (auto& l : copy->links)
            if (auto it = sounds.find (l.soundId); it != sounds.end())
                l.osc = it->second;
        resolved[id] = copy;
    }
    for (const auto& [id, s] : resolved)
        p->byId.emplace (id, s);

    const auto playable = str (meta, "playable").toStdString();
    p->playable = playable.empty() ? nullptr : p->find (playable);
    if (auto* notes = meta.getProperty ("padNotes", {}).getArray())
        for (const auto& pair : *notes)
            if (auto* a = pair.getArray(); a && a->size() >= 2)
                if (auto s = p->find ((*a)[1].toString().toStdString()))
                    p->padNotes.emplace_back ((int) (*a)[0], s);
    if (auto* pads = meta.getProperty ("pads", {}).getArray())
        for (const auto& id : *pads)
            if (auto s = p->find (id.toString().toStdString()))
                p->pads.push_back (s);
    return p;
}

} // namespace ssb
