#include <drummer/VectorSearch.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#if JUCE_MAC
 #import <Metal/Metal.h>
#endif

namespace drummer {
namespace {
void validate(const TextVector& vector) {
    double norm = 0;
    for (const auto value : vector) { if (!std::isfinite(value)) throw std::runtime_error("Similarity vectors must be finite"); norm += double(value) * value; }
    if (std::abs(norm - 1) > .001) throw std::runtime_error("Similarity vectors must be L2 normalized");
}
int64_t canonicalScore(double value) { return (int64_t) std::llround(value * 10000000); }
bool better(const VectorMatch& a, const VectorMatch& b) {
    const auto left = canonicalScore(a.score), right = canonicalScore(b.score);
    return left != right ? left > right : a.row < b.row;
}
}
struct VectorSearch::Impl {
    std::vector<TextVector> matrix;
    mutable std::mutex mutex;
    juce::String provider = "CPU";
   #if JUCE_MAC
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLComputePipelineState> pipeline = nil;
    id<MTLBuffer> rows = nil;
    ~Impl() { [rows release]; [pipeline release]; [queue release]; [device release]; }
   #endif
};
VectorSearch::VectorSearch(std::vector<TextVector> matrix, bool gpu) : impl(std::make_unique<Impl>()) {
    for (const auto& row : matrix) validate(row);
    impl->matrix = std::move(matrix);
   #if JUCE_MAC
    if (gpu && !impl->matrix.empty()) @autoreleasepool {
        impl->device = MTLCreateSystemDefaultDevice();
        if (!impl->device || impl->matrix.size() * sizeof(TextVector) > [impl->device maxBufferLength]) return;
        NSError* error = nil;
        NSString* source = @"#include <metal_stdlib>\nusing namespace metal;\n"
            "kernel void similarity(device const float4* rows [[buffer(0)]], device const float4* query [[buffer(1)]], "
            "device float* scores [[buffer(2)]], constant uint& count [[buffer(3)]], uint i [[thread_position_in_grid]]) { "
            "if(i>=count)return; float sum=0; for(uint d=0;d<96;++d)sum+=dot(rows[i*96+d],query[d]); scores[i]=sum; }";
        id<MTLLibrary> library = [impl->device newLibraryWithSource:source options:nil error:&error];
        if (!library) return;
        id<MTLFunction> function = [library newFunctionWithName:@"similarity"];
        impl->pipeline = [impl->device newComputePipelineStateWithFunction:function error:&error];
        [function release]; [library release];
        if (!impl->pipeline) return;
        impl->queue = [impl->device newCommandQueue];
        impl->rows = [impl->device newBufferWithBytes:impl->matrix.data() length:impl->matrix.size() * sizeof(TextVector) options:MTLResourceStorageModeShared];
        if (impl->queue && impl->rows) impl->provider = "Metal";
    }
   #else
    juce::ignoreUnused(gpu);
   #endif
}
VectorSearch::~VectorSearch() = default;
const std::vector<TextVector>& VectorSearch::vectors() const { return impl->matrix; }
juce::String VectorSearch::provider() const { return impl->provider; }
double VectorSearch::similarity(const TextVector& a, const TextVector& b) noexcept {
    double sum = 0;
    for (size_t i = 0; i < a.size(); ++i) sum += double(a[i]) * double(b[i]);
    return sum;
}
std::vector<float> VectorSearch::scores(const TextVector& query) const {
    validate(query);
    std::vector<float> result(impl->matrix.size());
   #if JUCE_MAC
    if (impl->provider == "Metal" && !result.empty()) @autoreleasepool {
        std::lock_guard lock(impl->mutex);
        id<MTLBuffer> input = [impl->device newBufferWithBytes:query.data() length:sizeof(TextVector) options:MTLResourceStorageModeShared];
        id<MTLBuffer> output = [impl->device newBufferWithLength:result.size() * sizeof(float) options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> command = [impl->queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        bool valid = input && output && command && encoder;
        if (valid) {
            const uint32_t count = (uint32_t) result.size();
            [encoder setComputePipelineState:impl->pipeline];
            [encoder setBuffer:impl->rows offset:0 atIndex:0]; [encoder setBuffer:input offset:0 atIndex:1]; [encoder setBuffer:output offset:0 atIndex:2];
            [encoder setBytes:&count length:sizeof(count) atIndex:3];
            const auto width = std::min(NSUInteger(128), [impl->pipeline maxTotalThreadsPerThreadgroup]);
            [encoder dispatchThreads:MTLSizeMake(count, 1, 1) threadsPerThreadgroup:MTLSizeMake(width, 1, 1)];
            [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
            valid = command.status == MTLCommandBufferStatusCompleted;
            if (valid) std::memcpy(result.data(), [output contents], result.size() * sizeof(float));
        } else if (encoder) [encoder endEncoding];
        [input release]; [output release];
        if (valid) return result;
    }
   #endif
    for (size_t i = 0; i < result.size(); ++i) result[i] = (float) similarity(impl->matrix[i], query);
    return result;
}
std::vector<VectorMatch> VectorSearch::search(const TextVector& query, size_t limit) const {
    if (!limit || impl->matrix.empty()) return {};
    limit = std::min(limit, impl->matrix.size());
    auto approximate = scores(query);
    auto ordered = approximate;
    for (auto& value : ordered) if (!std::isfinite(value)) value = -2;
    std::nth_element(ordered.begin(), ordered.begin() + (ptrdiff_t) (limit - 1), ordered.end(), std::greater<float>());
    // A float32 dot over 384 normalized terms has a forward-error bound well
    // below 1e-4. Include both sides of that bound, then score with the oracle.
    const double threshold = ordered[limit - 1] - .0002;
    std::vector<VectorMatch> result;
    for (size_t i = 0; i < approximate.size(); ++i)
        if (!std::isfinite(approximate[i]) || approximate[i] >= threshold) result.push_back({i, similarity(impl->matrix[i], query)});
    std::sort(result.begin(), result.end(), better);
    if (result.size() > limit) result.resize(limit);
    return result;
}
}
