#include <drummer/MidiImport.h>
#include <drummer/Json.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <array>
#include <limits>

namespace drummer {
using namespace json;
juce::MidiFile readMidi(juce::InputStream& stream) {
    if (stream.getTotalLength() > 32 * 1024 * 1024) throw std::runtime_error("MIDI file exceeds 32 MB");
    juce::MemoryBlock bytes;
    stream.readIntoMemoryBlock(bytes, 32 * 1024 * 1024 + 1);
    if (bytes.getSize() > 32 * 1024 * 1024) throw std::runtime_error("MIDI file exceeds 32 MB");
    juce::MemoryOutputStream standard;
    if (bytes.getSize() >= 14 && std::memcmp(bytes.getData(), "MThd", 4) == 0) {
        const auto* data = static_cast<const unsigned char*>(bytes.getData());
        const auto headerSize = juce::ByteOrder::bigEndianInt(data + 4);
        const auto tracks = juce::ByteOrder::bigEndianShort(data + 10);
        if (headerSize < 6 || headerSize > bytes.getSize() - 8 || tracks < 1 || tracks > 2048) throw std::runtime_error("Invalid MIDI header");
        standard.write("MThd", 4); standard.writeIntBigEndian(6); standard.write(data + 8, 6);
        size_t position = 8 + headerSize;
        int found = 0, chunks = 0;
        while (found < tracks) {
            if (position > bytes.getSize() || bytes.getSize() - position < 8 || ++chunks > 4096) throw std::runtime_error("Truncated MIDI track directory");
            const size_t length = juce::ByteOrder::bigEndianInt(data + position + 4);
            if (length > bytes.getSize() - position - 8) throw std::runtime_error("Truncated MIDI track");
            if (std::memcmp(data + position, "MTrk", 4) == 0) { standard.write(data + position, length + 8); ++found; }
            position += 8 + length;
        }
        // Many commercial files append vendor data after the declared SMF
        // tracks. It is not MIDI; retain it in the source payload, while passing
        // only complete declared tracks to JUCE's otherwise strict reader.
    } else standard.write(bytes.getData(), bytes.getSize());
    juce::MemoryInputStream input(standard.getData(), standard.getDataSize(), false);
    juce::MidiFile midi;
    if (!midi.readFrom(input)) throw std::runtime_error("Invalid MIDI file");
    if (midi.getTimeFormat() <= 0) throw std::runtime_error("SMPTE MIDI timing is not supported");
    if (midi.getNumTracks() > 2048) throw std::runtime_error("MIDI file exceeds 2048 tracks");
    return midi;
}

juce::var inspectMidi(const juce::MidiFile& midi) {
    std::array<int, 17> channels {};
    juce::Array<juce::var> tracks, signatures, tempos, text;
    const double ppq = midi.getTimeFormat();
    double firstSignature = std::numeric_limits<double>::infinity();
    int numerator = 4, denominator = 4;
    for (int t = 0; t < midi.getNumTracks(); ++t) {
        std::array<int, 17> counts {};
        int total = 0;
        juce::String name;
        const auto& track = *midi.getTrack(t);
        for (int i = 0; i < track.getNumEvents(); ++i) {
            const auto& message = track.getEventPointer(i)->message;
            const double beat = message.getTimeStamp() / ppq;
            if (!std::isfinite(beat) || beat < 0) throw std::runtime_error("Invalid MIDI event time");
            if (message.isTrackNameEvent()) name = message.getTextFromTextMetaEvent().trimCharactersAtEnd(juce::String::charToString(0));
            if (message.isNoteOn()) { ++counts[(size_t) message.getChannel()]; ++channels[(size_t) message.getChannel()]; ++total; }
            if (message.isTimeSignatureMetaEvent()) {
                int n = 4, d = 4;
                message.getTimeSignatureInfo(n, d);
                if (n < 1 || d < 1) throw std::runtime_error("Invalid MIDI time signature");
                signatures.add(object({{"beat", beat}, {"numerator", n}, {"denominator", d}, {"track", t}}));
                if (beat < firstSignature) { firstSignature = beat; numerator = n; denominator = d; }
            }
            if (message.isTempoMetaEvent()) {
                const double seconds = message.getTempoSecondsPerQuarterNote();
                if (seconds > 0 && std::isfinite(seconds)) tempos.add(object({{"beat", beat}, {"bpm", 60.0 / seconds}, {"track", t}}));
            }
            if (message.isMetaEvent()) {
                const int type = message.getMetaEventType();
                const char* kind = type == 1 ? "text" : type == 2 ? "copyright" : type == 3 ? "track" : type == 4 ? "instrument" : type == 6 ? "marker" : nullptr;
                if (kind && text.size() < 4096) {
                    auto value = message.getTextFromTextMetaEvent().substring(0, 2048).trim();
                    if (value.isNotEmpty()) text.add(object({{"beat", beat}, {"kind", kind}, {"text", value}, {"track", t}}));
                }
            }
        }
        juce::Array<juce::var> active;
        for (int ch = 1; ch <= 16; ++ch) if (counts[(size_t) ch]) active.add(ch);
        tracks.add(object({{"index", t}, {"name", name.isEmpty() ? "Track " + juce::String(t + 1) : name}, {"notes", total}, {"channels", active}}));
    }
    auto sort = [](auto& values) { std::stable_sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return number(a, "beat", 0) < number(b, "beat", 0); }); };
    sort(tempos); sort(signatures); sort(text);
    juce::Array<juce::var> active;
    for (int ch = 1; ch <= 16; ++ch) if (channels[(size_t) ch]) active.add(ch);
    const int automatic = channels[10] ? 10 : active.size() == 1 ? (int) active[0] : -1;
    return object({{"version", 1}, {"tracks", tracks}, {"channels", active}, {"autoChannel", automatic}, {"ppq", (int) ppq},
                   {"meter", object({{"numerator", numerator}, {"denominator", denominator}, {"explicit", !signatures.isEmpty()}})},
                   {"signatures", signatures}, {"tempos", tempos}, {"text", text}});
}

juce::var inspectMidi(const juce::File& file) {
    juce::FileInputStream stream(file);
    if (!stream.openedOk()) throw std::runtime_error("Cannot open MIDI file");
    return inspectMidi(readMidi(stream));
}

juce::var importMidi(const juce::MidiFile& midi, const juce::String& source, int channel, int track) {
    const int ppq = midi.getTimeFormat();
    if (channel < -1 || channel > 16 || track < -1 || track >= midi.getNumTracks()) throw std::runtime_error("Invalid MIDI track or channel selection");
    auto info = inspectMidi(midi);
    if (channel == -1) {
        std::array<bool, 17> active {};
        for (int t = 0; t < midi.getNumTracks(); ++t) if (track == -1 || track == t)
            for (int i = 0; i < midi.getTrack(t)->getNumEvents(); ++i) {
                const auto& m = midi.getTrack(t)->getEventPointer(i)->message;
                if (m.isNoteOn()) active[(size_t) m.getChannel()] = true;
            }
        if (active[10]) channel = 10;
        else if (std::count(active.begin(), active.end(), true) == 1) channel = (int) std::distance(active.begin(), std::find(active.begin(), active.end(), true));
        else throw std::runtime_error("Choose a MIDI channel to import");
    }
    juce::MidiMessageSequence selected;
    double end = 0;
    for (int t = 0; t < midi.getNumTracks(); ++t) for (int i = 0; i < midi.getTrack(t)->getNumEvents(); ++i) {
        const auto& m = midi.getTrack(t)->getEventPointer(i)->message;
        end = std::max(end, m.getTimeStamp() / ppq);
        if ((track == -1 || track == t) && (channel == 0 || m.getChannel() == channel) && (m.isNoteOn() || m.isNoteOff())) selected.addEvent(m);
    }
    selected.updateMatchedPairs();
    juce::Array<juce::var> notes;
    for (int i = 0; i < selected.getNumEvents(); ++i) {
        const auto* event = selected.getEventPointer(i);
        const auto& m = event->message;
        if (!m.isNoteOn()) continue;
        if (notes.size() >= 100000) throw std::runtime_error("Pattern exceeds 100000 notes");
        const double durationTicks = event->noteOffObject ? std::max(0.00001 * ppq, event->noteOffObject->message.getTimeStamp() - m.getTimeStamp()) : ppq * .1;
        notes.add(object({{"beat", m.getTimeStamp() / ppq}, {"duration", durationTicks / ppq}, {"note", m.getNoteNumber()}, {"velocity", m.getFloatVelocity()},
                          {"tick", m.getTimeStamp()}, {"durationTicks", durationTicks}, {"channel", m.getChannel()}}));
    }
    if (notes.isEmpty()) throw std::runtime_error("The selected track and channel have no notes");
    const auto meter = get(info, "meter");
    const double bar = number(meter, "numerator", 4) * 4.0 / number(meter, "denominator", 4);
    double length = std::ceil((end - 1e-8) / bar) * bar;
    if (length <= 0) length = bar;
    auto name = source.fromLastOccurrenceOf("/", false, false);
    if (name.isEmpty()) name = source;
    if (track >= 0) name += " / track " + juce::String(track + 1);
    if (channel > 0) name += " / ch " + juce::String(channel);
    auto identity = juce::JSON::toString(object({{"source", source}, {"channel", channel}, {"track", track}, {"length", length}, {"notes", notes}}), true);
    return object({{"id", "midi-" + juce::SHA256(identity.toUTF8()).toHexString()}, {"name", name}, {"path", source}, {"channel", channel}, {"track", track},
                   {"length", length}, {"ppq", ppq}, {"notes", notes}, {"metadata", info}});
}

juce::var importMidi(const juce::File& file, int channel, int track) {
    juce::FileInputStream stream(file);
    if (!stream.openedOk()) throw std::runtime_error("Cannot open MIDI file");
    return importMidi(readMidi(stream), file.getFullPathName(), channel, track);
}
}
