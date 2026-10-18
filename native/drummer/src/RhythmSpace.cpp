#include <drummer/RhythmSpace.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <stdexcept>

namespace drummer {
RhythmSpace::RhythmSpace() noexcept {
    scale.fill(1);
    for (int i = 0; i < rhythmDimensions; ++i) components[(size_t) i][(size_t) i] = 1;
}
RhythmSpace RhythmSpace::fit(const std::vector<RhythmVector>& samples) {
    RhythmSpace result;
    if (samples.empty()) return result;
    for (const auto& sample : samples) for (int d = 0; d < rhythmDimensions; ++d) {
        if (!std::isfinite(sample[(size_t) d])) throw std::runtime_error("Rhythm features must be finite");
        result.mean[(size_t) d] += sample[(size_t) d] / samples.size();
    }
    if (samples.size() < 2) return result;
    const double divisor = double(samples.size() - 1);
    RhythmVector variance {};
    for (const auto& sample : samples) for (int d = 0; d < rhythmDimensions; ++d) {
        const auto delta = sample[(size_t) d] - result.mean[(size_t) d];
        variance[(size_t) d] += delta * delta / divisor;
    }
    for (int d = 0; d < rhythmDimensions; ++d) {
        if(!std::isfinite(variance[(size_t)d]))throw std::runtime_error("Rhythm feature variance exceeds numerical range");
        if (variance[(size_t) d] > 1e-20) result.scale[(size_t) d] = std::sqrt(variance[(size_t) d]);
    }
    std::array<RhythmVector, rhythmDimensions> covariance {}, vectors = result.components;
    for (const auto& sample : samples) {
        RhythmVector centered {};
        for (int d = 0; d < rhythmDimensions; ++d) centered[(size_t) d] = (sample[(size_t) d] - result.mean[(size_t) d]) / result.scale[(size_t) d];
        for (int i = 0; i < rhythmDimensions; ++i) for (int j = 0; j < rhythmDimensions; ++j) covariance[(size_t) i][(size_t) j] += centered[(size_t) i] * centered[(size_t) j] / divisor;
    }
    // Standardized covariance is bounded. Remove sub-picounit accumulation
    // differences before fitting, without quantizing source note timing.
    for(auto& row:covariance)for(auto& value:row)value=std::round(value*1e12)/1e12;
    // Symmetric Jacobi eigendecomposition. Eight dimensions keep fitting small
    // and portable; fixed pivot/tie order makes degenerate families repeatable.
    for (int iteration = 0; iteration < rhythmDimensions * rhythmDimensions * 100; ++iteration) {
        int p = 0, q = 1;
        double largest = 0;
        for (int i = 0; i < rhythmDimensions; ++i) for (int j = i + 1; j < rhythmDimensions; ++j)
            if (const auto value = std::abs(covariance[(size_t) i][(size_t) j]); value > largest) { largest = value; p = i; q = j; }
        if (largest < 1e-12) break;
        const double off = covariance[(size_t) p][(size_t) q];
        const double tau = (covariance[(size_t) q][(size_t) q] - covariance[(size_t) p][(size_t) p]) / (2 * off);
        const double t = std::copysign(1.0, tau) / (std::abs(tau) + std::sqrt(1 + tau * tau));
        const double c = 1 / std::sqrt(1 + t * t), s = c * t;
        covariance[(size_t) p][(size_t) p] -= t * off;
        covariance[(size_t) q][(size_t) q] += t * off;
        covariance[(size_t) p][(size_t) q] = covariance[(size_t) q][(size_t) p] = 0;
        for (int j = 0; j < rhythmDimensions; ++j) {
            if (j != p && j != q) {
                const double a = covariance[(size_t) j][(size_t) p], b = covariance[(size_t) j][(size_t) q];
                covariance[(size_t) j][(size_t) p] = covariance[(size_t) p][(size_t) j] = c * a - s * b;
                covariance[(size_t) j][(size_t) q] = covariance[(size_t) q][(size_t) j] = s * a + c * b;
            }
            const double a = vectors[(size_t) j][(size_t) p], b = vectors[(size_t) j][(size_t) q];
            vectors[(size_t) j][(size_t) p] = c * a - s * b;
            vectors[(size_t) j][(size_t) q] = s * a + c * b;
        }
    }
    std::array<int, rhythmDimensions> order {};
    std::iota(order.begin(), order.end(), 0);
    auto eigenKey=[&](int axis){return (int64_t)std::llround(std::max(0.0,covariance[(size_t)axis][(size_t)axis])/eigenPrecision);};
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return eigenKey(a)>eigenKey(b); });
    for (int d = 0; d < rhythmDimensions; ++d) {
        const auto axis = order[(size_t) d];
        result.eigenvalues[(size_t) d] = eigenKey(axis)*eigenPrecision;
        int anchor = 0;
        for (int j = 0; j < rhythmDimensions; ++j) {
            result.components[(size_t) d][(size_t) j] = vectors[(size_t) j][(size_t) axis];
            if (std::llround(std::abs(vectors[(size_t) j][(size_t) axis])/eigenPrecision) > std::llround(std::abs(vectors[(size_t) anchor][(size_t) axis])/eigenPrecision)) anchor = j;
        }
        if (result.components[(size_t) d][(size_t) anchor] < 0) for (auto& value : result.components[(size_t) d]) value = -value;
    }
    // Individual eigenvectors are undefined within a repeated-eigenvalue
    // subspace. Its projector is unique: project canonical feature axes into
    // that space, choosing the largest residual with stable feature-order ties.
    // This covers null spaces as well as repeated positive eigenvalues.
    for(int start=0;start<rhythmDimensions;){
        int end=start+1;while(end<rhythmDimensions&&eigenKey(order[(size_t)end])==eigenKey(order[(size_t)start]))++end;
        if(end-start>1){
            const auto original=result.components;
            for(int axis=start;axis<end;++axis){
                RhythmVector best{};int64_t bestKey=-1;double bestNorm=0;
                for(int feature=0;feature<rhythmDimensions;++feature){
                    RhythmVector projected{};
                    for(int i=start;i<end;++i)for(int j=0;j<rhythmDimensions;++j)projected[(size_t)j]+=original[(size_t)i][(size_t)feature]*original[(size_t)i][(size_t)j];
                    for(int pass=0;pass<2;++pass)for(int previous=start;previous<axis;++previous){
                        double dot=0;for(int j=0;j<rhythmDimensions;++j)dot+=projected[(size_t)j]*result.components[(size_t)previous][(size_t)j];
                        for(int j=0;j<rhythmDimensions;++j)projected[(size_t)j]-=dot*result.components[(size_t)previous][(size_t)j];
                    }
                    double norm=0;for(double value:projected)norm+=value*value;
                    const auto key=(int64_t)std::llround(norm/eigenPrecision);
                    if(key>bestKey){best=projected;bestNorm=norm;bestKey=key;}
                }
                if(bestNorm<1e-16)throw std::runtime_error("Cannot canonicalize a degenerate rhythm subspace");
                for(int j=0;j<rhythmDimensions;++j)result.components[(size_t)axis][(size_t)j]=best[(size_t)j]/std::sqrt(bestNorm);
            }
        }
        start=end;
    }
    return result;
}
RhythmVector RhythmSpace::project(const RhythmVector& input) const noexcept {
    RhythmVector result {};
    for (int i = 0; i < rhythmDimensions; ++i) for (int j = 0; j < rhythmDimensions; ++j) result[(size_t) i] += components[(size_t) i][(size_t) j] * (input[(size_t) j] - mean[(size_t) j]) / scale[(size_t) j];
    return result;
}
RhythmVector RhythmSpace::reconstruct(const RhythmVector& input) const noexcept {
    RhythmVector result = mean;
    for (int i = 0; i < rhythmDimensions; ++i) for (int j = 0; j < rhythmDimensions; ++j) result[(size_t) j] += input[(size_t) i] * components[(size_t) i][(size_t) j] * scale[(size_t) j];
    return result;
}
double RhythmSpace::distance(const RhythmVector& a, const RhythmVector& b, int dimensions) const noexcept {
    const auto left = project(a), right = project(b);
    double result = 0;
    for (int i = 0; i < std::clamp(dimensions, 0, rhythmDimensions); ++i) result += (left[(size_t) i] - right[(size_t) i]) * (left[(size_t) i] - right[(size_t) i]);
    return std::sqrt(result);
}
}
