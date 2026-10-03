#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ssb
{

/** One short MIDI message: status and two data bytes (a missing byte is 0), and its length. */
struct MidiMessage
{
    uint8_t status { 0 };
    uint8_t data1 { 0 };
    uint8_t data2 { 0 };
    uint8_t size { 0 };
};

/**
    Single-producer / single-consumer ring of MIDI messages: the audio thread pushes what the host
    (or the standalone's MIDI inputs) delivered, the message thread pops and hands it to the page.

    Wait-free on both sides and allocation-free, so push() is safe on the audio thread. When the
    ring is full the newest message is dropped and counted -- never blocking the audio thread is
    the point; `dropped()` says it happened.
*/
class MidiQueue
{
public:
    static constexpr size_t capacity = 4096;   // power of two

    /** Audio thread. False (and counted) when full. */
    bool push (MidiMessage message) noexcept;

    /** Message thread. False when empty. */
    bool pop (MidiMessage& out) noexcept;

    /** Messages pushed while the ring was full, since construction. */
    uint64_t dropped() const noexcept { return droppedCount.load (std::memory_order_relaxed); }

    /** Discard everything queued (consumer side). */
    void clear() noexcept;

private:
    std::array<MidiMessage, capacity> ring {};
    std::atomic<size_t> head { 0 };   // next write, producer-owned
    std::atomic<size_t> tail { 0 };   // next read, consumer-owned
    std::atomic<uint64_t> droppedCount { 0 };
};

/** Length of a channel-voice or system-realtime message from its status byte; 0 for anything
    else (sysex, system common), which SSB does not forward. */
uint8_t midiMessageSize (uint8_t status) noexcept;

} // namespace ssb
