#include <drummer/PatternAnalysis.h>
#include <drummer/Json.h>
#include <DrumMapsData.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <vector>

namespace drummer {
using namespace json;
namespace {
juce::String clean(const juce::String& input) {
    static const std::regex expression0(R"((^|/)[0-9]+@)");
    static const std::regex expression1(R"(([0-9]+)#([0-9]+))");
    static const std::regex expression2(R"((theme|variation|beat)[ _-]*[0-9]+(?:-[0-9]+)*)");
    static const std::regex expression3(R"(\[[0-9, ]+files\])");
    static const std::regex expression4(R"((^|/)800000[^/]*/|(^|/)superior drummer 2 drum midi/|(^|/)gm midi pack/)");
    static const std::regex expression5(R"((^|/)[0-9]{2}[ ._]+(?![0-9]))");
    static const std::regex expression6(R"(\bqtr\b)");
    static const std::regex expression7(R"(\beighthnote\b)");
    static const std::regex expression8(R"(\bsixteenthnote\b)");
    static const std::regex expression9(R"(\bquarternote\b)");
    static const std::regex expression10(R"([_#/@]+)");
    static const std::regex expression11(R"(\s+)");
    auto text = input.toLowerCase().replace(".midi", "").replace(".mid", "").replace("\\", "/").toStdString();
    text = std::regex_replace(text, expression0, "$1");
    text = std::regex_replace(text, expression1, "$1/$2");
    text = std::regex_replace(text, expression2, " ");
    text = std::regex_replace(text, expression3, " ");
    text = std::regex_replace(text, expression4, "$1");
    text = std::regex_replace(text, expression5, "$1");
    text = std::regex_replace(text, expression6, "quarter note");
    text = std::regex_replace(text, expression7, "eighth note");
    text = std::regex_replace(text, expression8, "sixteenth note");
    text = std::regex_replace(text, expression9, "quarter note");
    text = std::regex_replace(text, expression10, " ");
    text = std::regex_replace(text, expression11, " ");
    return juce::String::fromUTF8(text.c_str()).trim();
}

juce::var features(const juce::Array<juce::var>& notes, double length) {
    const double span = std::max(.001, length);
    double velocity = 0, velocity2 = 0, microtiming = 0, syncopation = 0;
    int endings = 0, accents = 0, ghosts = 0;
    std::set<int64_t> onsets;
    std::array<int, 3> subdivision {};
    for (const auto& note : notes) {
        const double beat = number(note, "beat", 0), v = number(note, "velocity", 0);
        velocity += v; velocity2 += v * v;
        accents += v >= .78; ghosts += v < .4;
        endings += beat >= std::max(0.0, length - std::min(1.0, length * .25));
        microtiming += std::abs(beat * 4 - std::round(beat * 4)) / 4;
        const double offbeat = std::abs(beat - std::round(beat));
        if (offbeat > .1) syncopation += v;
        for (int i = 0; i < 3; ++i) {
            const double grid = i == 0 ? 2 : i == 1 ? 3 : 4;
            subdivision[(size_t) i] += std::abs(beat * grid - std::round(beat * grid)) < .08;
        }
        onsets.insert((int64_t) std::llround(beat * 1000000));
    }
    const double count = std::max(1, notes.size()), mean = velocity / count;
    // Keep raw, interpretable measurements. Family fitting scales them later.
    return object({{"notes", notes.size()}, {"density", notes.size() / span}, {"onsetDensity", onsets.size() / span},
                   {"velocity", mean}, {"velocityDeviation", std::sqrt(std::max(0.0, velocity2 / count - mean * mean))},
                   {"accentShare", accents / count}, {"ghostShare", ghosts / count}, {"endingShare", endings / count},
                   {"syncopation", syncopation / std::max(.00001, velocity)}, {"microtiming", microtiming / count},
                   {"eighthFit", subdivision[0] / count}, {"tripletFit", subdivision[1] / count}, {"sixteenthFit", subdivision[2] / count}});
}
}

juce::var drumMapDefinitions() {
    static const auto data = juce::JSON::parse(juce::String::fromUTF8(drummermaps::drumMaps_json, drummermaps::drumMaps_jsonSize));
    return data.clone();
}

juce::String describePattern(const juce::String& path, const juce::var& metadata) {
    auto result = clean(path);
    auto text = get(metadata, "text");
    if (auto* entries = text.getArray()) for (const auto& item : *entries) {
        const auto kind = get(item, "kind").toString();
        if (kind != "track" && kind != "marker" && kind != "instrument") continue;
        const auto value = clean(get(item, "text").toString());
        if (value.isEmpty() || value == "untitled" || value == "track 1" || value == "groovemonkee" || value.startsWith("http") || value.startsWith("www.")) continue;
        if (!result.contains(value)) result += " " + value;
    }
    return result.substring(0, 2048);
}

juce::String patternFamily(const juce::String& path) {
    // Export format and patch IDs are aliases, not independent musical votes.
    auto value = clean(path.upToLastOccurrenceOf("/", false, false)).toStdString();
    static const std::regex formats(R"(\b(gm|dfh|ssd[0-9.]*|fpc|superior|session drummer|live sd|ls ext|reason|preview files|single track|type 0|groove clips)\b)");
    value = std::regex_replace(value, formats, " ");
    static const std::regex spaces(R"(\s+)");
    value = std::regex_replace(value, spaces, " ");
    return juce::String(value).trim();
}

bool patternUsesVendorArticulations(const juce::String& description) {
    const auto words=" "+description.toLowerCase()+" ";
    return words.contains(" dfh ")||words.contains(" imap ")||words.contains(" fpc ")||words.contains(" ssd")||words.contains(" superior ")||words.contains(" ezx")||words.contains("ezdrummer")||words.contains("studio drummer")||words.contains("drumkit from hell")||words.contains("fullkit");
}

void validatePatternMapping(const juce::var& policy) {
    static const auto canonical = get(drumMapDefinitions(), "canonical");
    auto validate = [&](const juce::var& value) {
        const auto id = get(value, "id", "auto").toString();
        if (id != "auto" && id != "gm" && id != "vdrums" && id != "custom") throw std::runtime_error("Unknown source drum map");
        const auto notes = get(value, "notes");
        if (!notes.isVoid() && !notes.isObject()) throw std::runtime_error("Source note overrides must be a note-to-role object");
        if (auto* object = notes.getDynamicObject()) for (const auto& item : object->getProperties()) {
            const auto key = item.name.toString();
            if (key.isEmpty() || key.containsOnly("0123456789") == false || key.getIntValue() > 127) throw std::runtime_error("Source notes must be whole numbers from 0 to 127");
            const auto role = item.value.toString();
            if (role.isNotEmpty() && canonical.getProperty(role, {}).isVoid()) throw std::runtime_error("Unknown canonical drum role: " + role.toStdString());
        }
    };
    if (!policy.isVoid() && !policy.isObject()) throw std::runtime_error("Source mapping must be an object");
    validate(policy);
    const auto rules = get(policy, "rules");
    if (!rules.isVoid() && !rules.isArray()) throw std::runtime_error("Source map rules must be an array");
    if (auto* list = rules.getArray()) {
        if (list->size() > 256) throw std::runtime_error("At most 256 source mapping rules are supported");
        for (const auto& rule : *list) {
            if (get(rule, "prefix").toString().trim().isEmpty()) throw std::runtime_error("A source mapping rule needs a folder prefix");
            validate(rule);
        }
    }
}

juce::var selectPatternMapping(const juce::String& path, const juce::var& policy) {
    auto selected = object({{"id", get(policy, "id", "auto")}, {"notes", get(policy, "notes").clone()}});
    const auto location = "/" + path.replace("\\", "/").toLowerCase().trimCharactersAtStart("/");
    int longest = -1;
    auto rules = get(policy, "rules");
    if (auto* list = rules.getArray()) for (const auto& rule : *list) {
        const auto prefix = get(rule, "prefix").toString().replace("\\", "/").toLowerCase().trim().trimCharactersAtStart("/").trimCharactersAtEnd("/");
        if (prefix.isEmpty() || prefix.length() <= longest || !location.contains("/" + prefix + "/")) continue;
        longest = prefix.length();
        auto overrides = get(policy, "notes").clone();
        if (!overrides.isObject()) overrides = object({});
        auto extra = get(rule, "notes");
        if (auto* values = extra.getDynamicObject()) for (const auto& value : values->getProperties()) overrides.getDynamicObject()->setProperty(value.name, value.value);
        selected = object({{"id", get(rule, "id", get(policy, "id", "auto"))}, {"notes", overrides}});
    }
    return selected;
}

juce::var analyzePattern(const juce::var& clip, const juce::String& path, const juce::var& policy, int mappingVersion,bool measure) {
    if(mappingVersion<1 || mappingVersion>drumMappingVersion)throw std::runtime_error("Unsupported source mapping version");
    static const auto definitions = drumMapDefinitions();
    const auto maps = get(definitions, "maps"), canonical = get(definitions, "canonical");
    const auto selectedPolicy = selectPatternMapping(path, policy);
    const auto requested = get(selectedPolicy, "id", "auto").toString();
    if (requested != "auto" && requested != "gm" && requested != "vdrums" && requested != "custom") throw std::runtime_error("Unknown source drum map");
    const auto description = describePattern(path, get(clip, "metadata"));
    const auto words = " " + description + " ";
    // These packs use named sampled instruments at nonstandard numbers. Keep
    // them unresolved until an instrument-specific map has been supplied.
    const bool instrumentPack = words.contains("africa ") || words.contains("asia ") || words.contains("middle east ") || words.contains("south america ");
    auto source=get(clip,"notes");
    std::array<int,128> histogram{};int total=0;
    if(auto* notes=source.getArray())for(const auto& note:*notes){const int value=(int)number(note,"note",-1);if(value>=0&&value<128){++histogram[(size_t)value];++total;}}
    const bool roland=mappingVersion>=2&&(histogram[22]+histogram[26])>=total*.02&&(histogram[48]+histogram[45])>(histogram[50]+histogram[47]);
    const juce::String mapId = requested == "auto" ? (instrumentPack ? "custom" : roland?"vdrums":"gm") : requested;
    // Export names identify where GM-numbered percussion can instead mean a
    // kit articulation. Leave those notes unresolved until a map/override is
    // confirmed, rather than claiming that a recognized number proves identity.
    const bool vendor=mappingVersion>=2&&patternUsesVendorArticulations(description);
    const auto inbound = get(maps, mapId.toRawUTF8());
    auto overrides = get(selectedPolicy, "notes");
    auto result = clip.clone();
    juce::Array<juce::var> mapped, unknown;
    std::map<juce::String, juce::Array<juce::var>> parts;
    if (auto* notes = source.getArray()) for (const auto& original : *notes) {
        const int sourceNote = (int) number(original, "note", -1);
        if (sourceNote < 0 || sourceNote > 127) throw std::runtime_error("Invalid pattern drum note");
        const auto key = juce::String(sourceNote);
        auto role = inbound.getProperty(key, {}).toString();
        if(requested=="auto"&&(vendor||roland)&&(sourceNote>=60||sourceNote==56||sourceNote==58))role.clear();
        if (auto* object = overrides.getDynamicObject(); object && object->hasProperty(key)) role = overrides.getProperty(key, {}).toString();
        auto destination = role.isEmpty() ? juce::var() : canonical.getProperty(role, {});
        if (role.isEmpty() || destination.isVoid()) { unknown.add(original.clone()); continue; }
        auto note = original.clone();
        set(note, "sourceNote", sourceNote); set(note, "role", role); set(note, "note", destination);
        mapped.add(note); if(measure)parts[role].add(note);
    }
    const double length = number(clip, "length", 4);
    if (length <= 0) throw std::runtime_error("Invalid pattern length");
    juce::var measured;
    if(measure){
        juce::var perPart = object({});
        for (const auto& [role, notes] : parts) perPart.getDynamicObject()->setProperty(role, features(notes, length));
        measured = features(mapped, length);
        juce::String role = "groove";
        if (words.contains(" fills ") || words.contains(" fill ")) role = "fill";
        else if (words.contains(" intro ") || words.contains(" intros ")) role = "intro";
        else if (words.contains(" ending ") || words.contains(" outro ")) role = "ending";
        else if (words.contains(" sparse ") || words.contains(" relaxed ")) role = "sparse";
        set(measured, "role", role); set(measured, "parts", perPart); set(measured, "version", patternFeatureVersion);
    }
    // Musical identity excludes paths, track names, PPQ and output kit exports.
    // Decimal beat values retain microtiming; role keeps distinct articulations.
    juce::Array<juce::var> fingerprint;
    std::vector<juce::var> ordered(mapped.begin(), mapped.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        const double left = number(a, "beat", 0), right = number(b, "beat", 0);
        if (left != right) return left < right;
        const auto roleA = get(a, "role").toString(), roleB = get(b, "role").toString();
        if (roleA != roleB) return roleA < roleB;
        const double durationA = number(a, "duration", 0), durationB = number(b, "duration", 0);
        if (durationA != durationB) return durationA < durationB;
        return number(a, "velocity", 0) < number(b, "velocity", 0);
    });
    for (const auto& note : ordered) fingerprint.add(object({{"beat", get(note, "beat")}, {"duration", get(note, "duration")}, {"role", get(note, "role")}, {"velocity", get(note, "velocity")}}));
    const auto meter = get(get(clip, "metadata"), "meter");
    const auto identity = juce::JSON::toString(object({{"version", patternFeatureVersion}, {"length", length}, {"numerator", get(meter, "numerator", 4)}, {"denominator", get(meter, "denominator", 4)}, {"notes", fingerprint}, {"unknown", unknown}}), true);
    set(result, "id", "pattern-" + juce::SHA256(identity.toUTF8()).toHexString());
    set(result, "notes", mapped); set(result, "unmappedNotes", unknown);
    // Reserve the same property position when the reader will restore pinned
    // measurements, keeping canonical serialized patch bytes unchanged.
    set(result,"features",measured);
    set(result, "mapping", object({{"id", mapId}, {"requested", requested}, {"version", mappingVersion}, {"confirmed", requested != "auto"},
                                   {"reason", requested!="auto"?"Explicit source map":instrumentPack?"Named instrument map required":vendor?"GM-compatible core; vendor articulations remain unresolved":roland?"Roland hi-hat edges and tom positions":"General MIDI inference"},
                                   {"coverage", mapped.size() / double(std::max(1, mapped.size() + unknown.size()))}, {"overrides", overrides}}));
    set(result, "description", description); set(result, "family", patternFamily(path));
    return result;
}
}
