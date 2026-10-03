#include "check.h"

#include <ssb/MidiQueue.h>

#include <memory>
#include <thread>

TEST ("MidiQueue: FIFO order, empty pop")
{
    auto q = std::make_unique<ssb::MidiQueue>();
    ssb::MidiMessage m;
    CHECK (! q->pop (m));
    for (uint8_t i = 0; i < 10; ++i)
        CHECK (q->push ({ 0x90, i, 100, 3 }));
    for (uint8_t i = 0; i < 10; ++i)
    {
        CHECK (q->pop (m));
        CHECK (m.data1 == i);
    }
    CHECK (! q->pop (m));
}

TEST ("MidiQueue: full ring drops the newest and counts it")
{
    auto q = std::make_unique<ssb::MidiQueue>();
    for (size_t i = 0; i < ssb::MidiQueue::capacity; ++i)
        CHECK (q->push ({ 0xb0, 1, 2, 3 }));
    CHECK (! q->push ({ 0x90, 60, 1, 3 }));
    CHECK (q->dropped() == 1);
    q->clear();
    ssb::MidiMessage m;
    CHECK (! q->pop (m));
    CHECK (q->push ({ 0x90, 60, 1, 3 }));
}

TEST ("MidiQueue: one producer, one consumer, nothing lost or reordered")
{
    auto q = std::make_unique<ssb::MidiQueue>();
    constexpr int total = 200000;
    std::thread producer ([&] {
        for (int i = 0; i < total;)
            if (q->push ({ 0x90, (uint8_t) (i & 0x7f), (uint8_t) ((i >> 7) & 0x7f), 3 }))
                ++i;
    });
    int expected = 0;
    bool inOrder = true;
    while (expected < total)
    {
        ssb::MidiMessage m;
        if (q->pop (m))
        {
            inOrder = inOrder && m.data1 == (expected & 0x7f) && m.data2 == ((expected >> 7) & 0x7f);
            ++expected;
        }
    }
    producer.join();
    // (the producer retries when the ring is full; each refused attempt is counted in dropped())
    CHECK (inOrder);
}

TEST ("midiMessageSize")
{
    CHECK (ssb::midiMessageSize (0x90) == 3);
    CHECK (ssb::midiMessageSize (0xc3) == 2);
    CHECK (ssb::midiMessageSize (0xd0) == 2);
    CHECK (ssb::midiMessageSize (0xf8) == 1);
    CHECK (ssb::midiMessageSize (0xf0) == 0);   // sysex is not forwarded
}
