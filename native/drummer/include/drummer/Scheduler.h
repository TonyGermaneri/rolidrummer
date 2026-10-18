#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace drummer {
constexpr int maxZones = 8;
constexpr int maxContacts = 16;
constexpr int maxRuntimeClips = 2048;
constexpr int maxCandidates = 16;
constexpr int intentDimensions = 4;
using PatternIntent = std::array<float,intentDimensions>;
struct Note { double beat = 0, duration = .1; int note = 36; float velocity = 1; };
struct ClipEvent { double beat=0; int note=0; int64_t cycleShift=0; bool off=false; };
struct Clip { std::vector<ClipEvent> timeline; uint64_t id = 0; double length = 4, origin = 0; std::vector<Note> notes; };
void prepareClip(Clip&);
struct PatternCandidate { int clip = -1; PatternIntent position {{.5f,.5f,.5f,.5f}}; };
struct PatternGroup { std::array<PatternCandidate,maxCandidates> candidates{}; int count=0; };
struct Zone { float upper = 1; int clip = -1,group=-1; };
struct Key {
    bool enabled = false;
    std::array<bool,128> parts {};
    std::array<Zone,maxZones> zones {};
    int zoneCount = 3;
    int zoneSource = 2; // strike, glide, slide, press, lift
    bool intentEnabled = false;
    PatternIntent intentWeights {{.4f,.2f,.3f,.1f}};
    float intentSlewMs = 35, intentPenalty = .025f;
    double intentDwell = .25;
};
enum class KeyboardMode { normal, hold, toggle };
struct Configuration {
    std::array<Key,128> keys;
    std::vector<Clip> clips;
    std::vector<PatternGroup> groups;
    double launchGrid = .25, switchGrid = .25;
    float hysteresis = .015f;
    bool hardRelease = false;
    KeyboardMode keyboardMode = KeyboardMode::normal;
    int masterChannel = 0;
    int sustainCC = 64; // 0–119, except per-note Slide (74).
};
struct Clock { double ppq = 0, bpm = 120, sampleRate = 48000; bool playing = false, discontinuity = false; };
struct Midi { int offset = 0; uint8_t status = 0, a = 0, b = 0; };
struct Expression { float strike = 1, glide = .5f, slide = 0, press = 0, lift = 0; };
struct Event {
    enum Type { hit, endHit, expression, releaseContact, killContact, switchZone, panic, globalCC } type = hit;
    int offset = 0, key = -1, channel = 0, drumNote = -1;
    uint64_t owner = 0, token = 0;
    float velocity = 1;
    Expression values;
    int cc = 0;
    float value = 0;
};
struct Contact {
    bool down = false; // Physical contact, including keys without an assigned sound.
    bool held = false;
    bool toggled = false, suppressed = false; // Explicitly stopped physical touches cannot restart on an edit.
    int key = -1, zone = 0, wanted = 0, channel = 0;
    uint64_t owner = 0;
    double opens = 0, switches = 0;
    float releaseVelocity = 0;
    Expression values;
    int clip=-1,wantedClip=-1;
    PatternIntent intent {{.5f,.5f,.5f,.5f}};
    double intentBeat=0,nextIntent=0,changedAt=0;
};
/** Native clip bus. No allocation/locks/I/O in process; editor configuration is immutable. */
class Scheduler {
public:
    static constexpr int capacity = 16384;
    static constexpr int maxSustained = 128;
    using Sink = void (*)(void*, const Event&);
    void process(const Configuration&, Clock, int frames, const Midi*, int count, Sink, void*) noexcept;
    void reset() noexcept;
    void reconfigure(const Configuration& previous,const Configuration& next,double beat,Sink,void*) noexcept;
    const std::array<Contact,maxContacts>& contacts() const { return fingers; }
    const std::array<Contact,maxSustained>& sustainedContacts() const { return sustained; }
    bool hasHeldNotes() const noexcept;
    uint32_t evaluations = 0;
    bool overflow = false;
private:
    struct Due { int offset; int clip, note; int64_t cycle; bool off; };
    struct PendingOff { double beat; uint64_t token; };
    std::array<Due,capacity> due;
    int dueCount = 0;
    std::array<PendingOff,capacity> pendingOffs;
    int pendingOffCount = 0;
    std::array<Contact,maxContacts> fingers {};
    std::array<Contact,maxSustained> sustained {};
    std::array<bool,maxContacts> pedals {};
    int pedalCC = 64, pedalMaster = 0;
    KeyboardMode keyboardMode = KeyboardMode::normal;
    Contact& contact(int i) noexcept { return i < maxContacts ? fingers[(size_t)i] : sustained[(size_t)(i-maxContacts)]; }
    bool pedalDown(int channel) const noexcept { return pedals[(size_t)pedalMaster] || pedals[(size_t)channel]; }
    void enforceToggle(int offset, Sink, void*) noexcept;
    void release(Contact&, int offset, Event::Type, Sink, void*) noexcept;
    void lift(Contact&, int offset, Sink, void*) noexcept;
    uint64_t generation = 0;
    uint64_t hitGeneration = 0;
    double expected = 0;
    bool wasPlaying = false;
    void input(const Configuration&, const Midi&, double beat, double bpm, Sink, void*) noexcept;
};
}
