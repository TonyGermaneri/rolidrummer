#pragma once
#include "PatternLibrary.h"
#include "TextEmbedding.h"

namespace drummer {
constexpr int semanticDescriptionVersion = 3;
constexpr int musicalQueryVersion = 1;
juce::String semanticDescription(const juce::String&);
juce::String musicalQueryDescription(const juce::String&);
struct NeuralBuildOptions {
    std::function<bool()> cancelled;
    std::function<void(const juce::var&)> progress;
};
struct SemanticAffinity { juce::String id,description;double score=0; };
struct SemanticAffinities {
    juce::String indexId,modelVersion,canonicalQuery,provider;
    int64_t revision=0;
    std::vector<SemanticAffinity> groups;
};
// Committed source snapshots and cached pretrained vectors. All operations are
// worker-only. Index identity includes source revisions, model and preprocessing.
class NeuralIndex {
public:
    explicit NeuralIndex(juce::File database, juce::File assets = neuralAssetsDirectory());
    ~NeuralIndex();
    juce::var build(const NeuralBuildOptions& = {});
    juce::var status() const;
    juce::var search(const juce::var& options) const;
    // Worker diagnostics for repeatable process/instance resource measurements.
    // Reference counts are observations, not ownership or synchronization APIs.
    juce::var resources() const;
    SemanticAffinities affinities(const juce::String& query,const juce::String& indexId={},bool cpu=false) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
