#pragma once
#include <array>
#include <vector>

namespace drummer {
constexpr int rhythmDimensions = 8;
using RhythmVector = std::array<double, rhythmDimensions>;

// A fitted family transform is immutable during performance and saved alongside
// resolved patterns. Coordinates are standardized; eigenvalues express variance.
struct RhythmSpace {
    static constexpr int version = 2;
    static constexpr double eigenPrecision = 1e-10;
    RhythmSpace() noexcept;
    RhythmVector mean {}, scale {}, eigenvalues {};
    std::array<RhythmVector, rhythmDimensions> components {};
    static RhythmSpace fit(const std::vector<RhythmVector>&);
    RhythmVector project(const RhythmVector&) const noexcept;
    RhythmVector reconstruct(const RhythmVector&) const noexcept;
    double distance(const RhythmVector&, const RhythmVector&, int dimensions = rhythmDimensions) const noexcept;
};
}
