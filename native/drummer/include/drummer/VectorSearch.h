#pragma once
#include "TextEmbedding.h"
#include <memory>

namespace drummer {
struct VectorMatch { size_t row = 0; double score = 0; };
// Worker-only immutable matrix. Metal retrieves candidates; canonical double
// rescoring and stable row order establish the same result as the CPU oracle.
class VectorSearch {
public:
    explicit VectorSearch(std::vector<TextVector>, bool useGPU = true);
    ~VectorSearch();
    std::vector<VectorMatch> search(const TextVector&, size_t limit) const;
    std::vector<float> scores(const TextVector&) const;
    const std::vector<TextVector>& vectors() const;
    juce::String provider() const;
    static double similarity(const TextVector&, const TextVector&) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
