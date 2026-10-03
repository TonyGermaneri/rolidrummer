#include <ssb/MidiQueue.h>

namespace ssb
{

static_assert ((MidiQueue::capacity & (MidiQueue::capacity - 1)) == 0, "capacity must be a power of two");

bool MidiQueue::push (MidiMessage message) noexcept
{
    const auto h = head.load (std::memory_order_relaxed);
    const auto t = tail.load (std::memory_order_acquire);
    if (h - t >= capacity)
    {
        droppedCount.fetch_add (1, std::memory_order_relaxed);
        return false;
    }
    ring[h & (capacity - 1)] = message;
    head.store (h + 1, std::memory_order_release);
    return true;
}

bool MidiQueue::pop (MidiMessage& out) noexcept
{
    const auto t = tail.load (std::memory_order_relaxed);
    const auto h = head.load (std::memory_order_acquire);
    if (t == h)
        return false;
    out = ring[t & (capacity - 1)];
    tail.store (t + 1, std::memory_order_release);
    return true;
}

void MidiQueue::clear() noexcept
{
    tail.store (head.load (std::memory_order_acquire), std::memory_order_release);
}

uint8_t midiMessageSize (uint8_t status) noexcept
{
    if (status >= 0xf8)
        return 1;   // realtime: clock, start, continue, stop, active sensing, reset
    switch (status & 0xf0)
    {
        case 0x80: case 0x90: case 0xa0: case 0xb0: case 0xe0: return 3;
        case 0xc0: case 0xd0:                                   return 2;
        default:                                                return 0;
    }
}

} // namespace ssb
