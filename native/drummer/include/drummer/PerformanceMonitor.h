#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace drummer {
// Audio produces, the editor consumes. A full monitor never stalls the instrument.
struct PlayedEvent {
 enum Type { on, off, ownerOff, allOff } type=on;
 uint64_t epoch=0,owner=0,token=0;
 double beat=0,duration=0;
 int key=-1,note=-1;
 float velocity=0;
};
class PerformanceMonitor {
public:
 static constexpr unsigned capacity=8192;
 bool push(const PlayedEvent& event) noexcept {
  const auto w=write.load(std::memory_order_relaxed);
  if(w-read.load(std::memory_order_acquire)>=capacity){dropped.fetch_add(1,std::memory_order_relaxed);return false;}
  events[w%capacity]=event;write.store(w+1,std::memory_order_release);return true;
 }
 bool pop(PlayedEvent& event) noexcept {
  const auto r=read.load(std::memory_order_relaxed);
  if(r==write.load(std::memory_order_acquire))return false;
  event=events[r%capacity];read.store(r+1,std::memory_order_release);return true;
 }
 std::atomic<unsigned> dropped{0};
private:
 std::array<PlayedEvent,capacity> events{};
 std::atomic<unsigned> read{0},write{0};
};
}
