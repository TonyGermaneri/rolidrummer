#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace drummer {
constexpr int textEmbeddingDimensions = 384;
using TextVector = std::array<float, textEmbeddingDimensions>;
juce::File neuralAssetsDirectory();

// Local pretrained inference, exclusively for workers. Sessions are shared by
// instances in a host process; model/tokenizer/runtime versions name the cache.
class TextEmbedding {
public:
    static std::shared_ptr<TextEmbedding> acquire(const juce::File& directory, bool useCoreML = false);
    ~TextEmbedding();
    std::vector<int64_t> tokenize(const juce::String&) const;
    std::vector<TextVector> encode(const std::vector<juce::String>&);
    juce::String version() const;
    juce::var info() const;
private:
    TextEmbedding(const juce::File&, bool);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
