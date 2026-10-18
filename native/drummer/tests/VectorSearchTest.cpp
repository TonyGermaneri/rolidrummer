#include <drummer/VectorSearch.h>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
drummer::TextVector normalized(drummer::TextVector vector) {
    double norm = 0; for (auto value : vector) norm += double(value) * value;
    for (auto& value : vector) value = (float) (value / std::sqrt(norm)); return vector;
}
}
int main() { try {
    using namespace drummer;
    std::mt19937 random(441);
    std::uniform_real_distribution<float> uniform(-1, 1);
    std::vector<TextVector> matrix;
    for (int row = 0; row < 12000; ++row) { TextVector vector; for (auto& value : vector) value = uniform(random); matrix.push_back(normalized(vector)); }
    for (int row = 0; row < 300; ++row) matrix[(size_t) row] = matrix[400];
    TextVector query = matrix[400];
    for (int row = 300; row < 400; ++row) { matrix[(size_t) row] = query; matrix[(size_t) row][0] += (row - 300) * .00001f; matrix[(size_t) row] = normalized(matrix[(size_t) row]); }
    VectorSearch cpu(matrix, false), gpu(matrix);
   #if JUCE_MAC
    check(gpu.provider() == "Metal", "Metal similarity pipeline did not initialize on the test Mac");
   #endif
    const auto expectedScores = cpu.scores(query), actualScores = gpu.scores(query);
    double error = 0;
    for (size_t i = 0; i < actualScores.size(); ++i) error = std::max(error, std::abs(double(expectedScores[i]) - actualScores[i]));
    check(error < .00001, "Metal dot products exceed the canonical rescoring error allowance");
    for (const size_t count : {size_t(1), size_t(17), size_t(200), size_t(12000), size_t(14000)}) {
        const auto expected = cpu.search(query, count), actual = gpu.search(query, count);
        check(expected.size() == actual.size(), "CPU/GPU result count differs");
        for (size_t i = 0; i < expected.size(); ++i) check(expected[i].row == actual[i].row && expected[i].score == actual[i].score, "Close scores or ties differ between CPU and GPU");
    }
    auto first = std::async(std::launch::async, [&] { return gpu.search(query, 50); });
    const auto simultaneous = gpu.search(matrix[800], 50), oracle = cpu.search(matrix[800], 50);
    for (size_t i = 0; i < oracle.size(); ++i) check(simultaneous[i].row == oracle[i].row, "Shared GPU query results interfered");
    check(first.get().size() == 50, "Concurrent GPU query failed");
    check(gpu.search(query, 0).empty(), "Zero search limit is not empty");
    auto invalid = query; invalid[0] = std::numeric_limits<float>::quiet_NaN();
    bool rejected = false; try { gpu.scores(invalid); } catch (...) { rejected = true; }
    check(rejected, "Nonfinite search vector was accepted");
    std::cout << "vector search: " << gpu.provider() << ", 12000 vectors, max score error " << error << "; CPU oracle, near ties, stable ordering and concurrent queries passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
