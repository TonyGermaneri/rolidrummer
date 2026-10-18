#include <drummer/RhythmSpace.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace { void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); } }
int main() { try {
    using namespace drummer;
    std::vector<RhythmVector> samples;
    for (int i = 0; i < 14; ++i) samples.push_back({double(i), double(i * 2 + 10), 7, 0, 0, 0, 0, 0});
    const auto space = RhythmSpace::fit(samples);
    check(std::abs(space.eigenvalues[0] - 2) < 1e-10 && space.eigenvalues[1] < 1e-10, "PCA did not recover the correlated rank-one family");
    for (int i = 0; i < rhythmDimensions; ++i) for (int j = 0; j < rhythmDimensions; ++j) {
        double dot = 0;
        for (int k = 0; k < rhythmDimensions; ++k) dot += space.components[(size_t) i][(size_t) k] * space.components[(size_t) j][(size_t) k];
        check(std::abs(dot - (i == j ? 1 : 0)) < 1e-10, "PCA basis is not orthonormal");
    }
    for (const auto& sample : samples) {
        const auto reconstructed = space.reconstruct(space.project(sample));
        for (int d = 0; d < rhythmDimensions; ++d) check(std::abs(sample[(size_t) d] - reconstructed[(size_t) d]) < 1e-9, "PCA failed to preserve feature coordinates");
    }
    auto scaled = samples;
    for (auto& sample : scaled) { sample[0] = sample[0] * 100 + 20; sample[1] = sample[1] * .001 - 4; }
    const auto other = RhythmSpace::fit(scaled);
    for (int d = 0; d < rhythmDimensions; ++d) check(std::abs(space.project(samples.back())[(size_t) d] - other.project(scaled.back())[(size_t) d]) < 1e-9, "Feature units changed PCA geometry");
    const auto repeated = RhythmSpace::fit(samples);
    check(space.components == repeated.components, "Repeated fitting flipped an axis");
    // Tiny perturbations must not select arbitrary directions in a null space.
    auto perturbed=samples;for(size_t i=0;i<perturbed.size();++i)perturbed[i][1]+=(i%2?1:-1)*1e-14;
    const auto nearby=RhythmSpace::fit(perturbed);
    for(int i=0;i<rhythmDimensions;++i)for(int j=0;j<rhythmDimensions;++j)check(std::abs(space.components[(size_t)i][(size_t)j]-nearby.components[(size_t)i][(size_t)j])<1e-10,"Roundoff rotated a degenerate rhythm subspace");
    // Equal positive eigenvalues have no preferred eigenvector either. A
    // symmetric feature family should produce canonical coordinate axes.
    std::vector<RhythmVector> symmetric;
    for(int dimension=0;dimension<3;++dimension)for(int sign:{-1,1}){RhythmVector value{};value[(size_t)dimension]=sign;symmetric.push_back(value);}
    const auto tied=RhythmSpace::fit(symmetric);
    for(int i=0;i<rhythmDimensions;++i)for(int j=0;j<rhythmDimensions;++j)check(std::abs(tied.components[(size_t)i][(size_t)j]-(i==j?1.0:0.0))<1e-10,"Repeated positive eigenvalues did not use canonical feature directions");
    const auto empty = RhythmSpace::fit({});
    check(std::isfinite(empty.distance(samples.front(), samples.back())), "Empty family produced invalid distances");
    const auto singleton = RhythmSpace::fit({samples.front()});
    for (double x : singleton.project(samples.front())) check(x == 0, "Singleton family has a spurious deviation");
    bool rejected = false;
    try { auto bad = samples; bad[0][0] = std::numeric_limits<double>::quiet_NaN(); RhythmSpace::fit(bad); } catch (...) { rejected = true; }
    check(rejected, "Invalid rhythm features reached the fitted transform");
    rejected=false;try{RhythmVector positive{},negative{};positive[0]=1e308;negative[0]=-1e308;RhythmSpace::fit({positive,negative});}catch(...){rejected=true;}
    check(rejected,"Finite features overflowed the fitted covariance silently");
    std::cout << "rhythm: covariance eigenvalues, orthonormality, reconstruction, unit invariance, stable axes and degenerate families passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
