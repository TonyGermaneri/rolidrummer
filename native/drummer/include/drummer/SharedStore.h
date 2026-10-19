#pragma once
#include <juce_core/juce_core.h>
#include <mutex>
struct sqlite3;
namespace drummer {
/** Transactional field updates. One connection per process; file locking also handles
    plugins in other host processes. Never called by the audio thread. */
class SharedStore {
public:
 explicit SharedStore(const juce::File&);
 ~SharedStore();
 juce::var snapshot(int64_t after=0);
 juce::var statistics();
 bool lease(const juce::String& resource,const juce::String& owner,bool release=false);
 juce::var commit(const juce::var& operations);
private:
 sqlite3* db=nullptr;
 std::mutex mutex;
 void exec(const char*);
};
}
