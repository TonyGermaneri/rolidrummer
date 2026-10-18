#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace drummer {
class NeuralIndex;
class PatternFamilies;
struct LibraryImportOptions {
    juce::var mapping;
    int batchSize = 256;
    std::function<bool()> cancelled;
    std::function<void(const juce::var&)> progress;
};

// One worker's prepared pattern query and read transaction. Its first lookup
// pins the catalog view; completed historical bindings remain valid. Do not
// share this session between threads or retain it in an audio runtime.
class PatternReader {
public:
    explicit PatternReader(const juce::File& database);
    ~PatternReader();
    juce::var pattern(const juce::String& bindingId);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Opens its own SQLite connection for each operation. No method is audio-safe.
// Readers only see completed source revisions. Interrupted runs resume in place.
class PatternLibrary {
public:
    explicit PatternLibrary(juce::File database);
    juce::var importSource(const juce::File&, const LibraryImportOptions& = {}) const;
    juce::var status() const;
    juce::var browse(const juce::var& query = {}) const;
    juce::var pattern(const juce::String& bindingId) const;
    juce::var errors(const juce::String& runId, int offset = 0, int limit = 100) const;
    const juce::File& file() const { return database; }
private:
    juce::File database;
};

// One service per database per host process; its import worker outlives editors.
class LibraryService {
public:
    static std::shared_ptr<LibraryService> acquire(const juce::File& database);
    ~LibraryService();
    void start(const juce::File&, juce::var mapping = {});
    void buildNeuralIndex();
    void refreshFamilies();
    void cancel() noexcept { cancelRequested = true; }
    juce::var progress() const;
    PatternLibrary catalog;
    std::unique_ptr<NeuralIndex> neural;
    std::unique_ptr<PatternFamilies> families;
private:
    explicit LibraryService(const juce::File&);
    void publish(const juce::var&);
    void launch(std::function<void()>,bool familyRefresh=false);
    void prepareSearch();
    std::mutex workerMutex;
    bool familiesRequested=false;
    mutable std::mutex mutex;
    juce::var snapshot;
    std::thread worker;
    std::atomic<bool> cancelRequested {false}, running {false};
};
}
