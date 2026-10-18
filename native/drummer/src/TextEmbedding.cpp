#include <drummer/TextEmbedding.h>
#include <drummer/Json.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include "UnicodeMarks.h"
#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#endif
#if RD_HAVE_ONNX
 #include <onnxruntime_c_api.h>
#endif

namespace drummer {
using namespace json;
juce::File neuralAssetsDirectory() {
    const auto override = juce::SystemStats::getEnvironmentVariable("RD_NEURAL_DIR", {});
    if (override.isNotEmpty()) return juce::File(override);
    const auto binary = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto bundled = binary.getParentDirectory().getSiblingFile("Resources").getChildFile("neural");
    if (bundled.getChildFile("manifest.json").existsAsFile()) return bundled;
   #ifdef RD_NEURAL_SOURCE_DIR
    return juce::File(RD_NEURAL_SOURCE_DIR);
   #else
    return bundled;
   #endif
}
namespace {
juce::String fileHash(const juce::File& file) {
    juce::FileInputStream source(file);
    if (!source.openedOk()) throw std::runtime_error("Missing neural asset: " + file.getFileName().toStdString());
    juce::BufferedInputStream stream(source, 262144);
    return juce::SHA256(stream).toHexString();
}
juce::String normalizeText(const juce::String& input) {
    auto lowered = input.toLowerCase();
   #if JUCE_MAC
    const auto utf8 = lowered.toUTF8();
    auto original = CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(utf8.getAddress()), (CFIndex) utf8.sizeInBytes() - 1, kCFStringEncodingUTF8, false);
    if (!original) throw std::runtime_error("Cannot normalize embedding text");
    auto decomposed = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, original);
    CFRelease(original);
    CFStringNormalize(decomposed, kCFStringNormalizationFormD);
    const auto length = CFStringGetMaximumSizeForEncoding(CFStringGetLength(decomposed), kCFStringEncodingUTF8) + 1;
    std::vector<char> bytes((size_t) length);
    if (!CFStringGetCString(decomposed, bytes.data(), length, kCFStringEncodingUTF8)) { CFRelease(decomposed); throw std::runtime_error("Cannot encode normalized text"); }
    CFRelease(decomposed);
    lowered = juce::String::fromUTF8(bytes.data());
   #endif
    return lowered;
}
bool mark(juce::juce_wchar c) {
    if (c < 0x300) return false;
    const auto it = std::lower_bound(nonspacingMarks.begin(), nonspacingMarks.end(), c, [](const auto& range, auto value) { return range[1] < value; });
    return it != nonspacingMarks.end() && c >= (*it)[0];
}
bool control(juce::juce_wchar c) {
    if (c == '\t' || c == '\n' || c == '\r') return false;
   #if JUCE_MAC
    return CFCharacterSetIsLongCharacterMember(CFCharacterSetGetPredefined(kCFCharacterSetControl), c);
   #else
    return c < 0x20 || (c >= 0x7f && c <= 0x9f);
   #endif
}
bool punctuation(juce::juce_wchar c) {
    if ((c >= 33 && c <= 47) || (c >= 58 && c <= 64) || (c >= 91 && c <= 96) || (c >= 123 && c <= 126)) return true;
   #if JUCE_MAC
    return CFCharacterSetIsLongCharacterMember(CFCharacterSetGetPredefined(kCFCharacterSetPunctuation), c);
   #else
    return (c >= 0x2000 && c <= 0x206f) || (c >= 0x3000 && c <= 0x303f);
   #endif
}
bool chinese(juce::juce_wchar c) {
    return (c >= 0x4e00 && c <= 0x9fff) || (c >= 0x3400 && c <= 0x4dbf) || (c >= 0x20000 && c <= 0x2a6df) || (c >= 0x2a700 && c <= 0x2b73f) ||
           (c >= 0x2b740 && c <= 0x2b81f) || (c >= 0x2b820 && c <= 0x2ceaf) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0x2f800 && c <= 0x2fa1f);
}
}

struct TextEmbedding::Impl {
    juce::var manifest;
    juce::String identity, provider = "CPU";
    std::unordered_map<std::string, int64_t> vocabulary;
    std::mutex mutex;
   #if RD_HAVE_ONNX
    juce::DynamicLibrary module;
    const OrtApi* api = nullptr;
    OrtEnv* environment = nullptr;
    OrtSessionOptions* options = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memory = nullptr;
    OrtAllocator* allocator = nullptr;
    juce::String outputName;
    void check(OrtStatus* status) const {
        if (status) { const std::string error = api->GetErrorMessage(status); api->ReleaseStatus(status); throw std::runtime_error(error); }
    }
    ~Impl() {
        if (!api) return;
        if (session) api->ReleaseSession(session);
        if (memory) api->ReleaseMemoryInfo(memory);
        if (options) api->ReleaseSessionOptions(options);
        if (environment) api->ReleaseEnv(environment);
    }
   #endif
    std::vector<int64_t> tokenize(const juce::String& original) const {
        std::vector<int64_t> result {101};
        auto wordPieces = [&](const juce::String& word) {
            if (word.isEmpty() || result.size() >= 255) return;
            if (word.length() > 100) { result.push_back(100); return; }
            std::vector<int64_t> pieces;
            for (int start = 0; start < word.length();) {
                bool found = false;
                for (int end = word.length(); end > start; --end) {
                    auto piece = word.substring(start, end);
                    if (start) piece = "##" + piece;
                    if (auto it = vocabulary.find(piece.toStdString()); it != vocabulary.end()) { pieces.push_back(it->second); start = end; found = true; break; }
                }
                if (!found) { result.push_back(100); return; }
            }
            for (auto piece : pieces) if (result.size() < 255) result.push_back(piece);
        };
        auto basic = [&](const juce::String& text) {
            juce::String word;
            for (auto c : normalizeText(text)) {
                if (c == 0 || c == 0xfffd || control(c) || mark(c)) continue;
                if (juce::CharacterFunctions::isWhitespace(c)) { wordPieces(word); word.clear(); }
                else if (punctuation(c) || chinese(c)) { wordPieces(word); word.clear(); wordPieces(juce::String::charToString(c)); }
                else word += juce::String::charToString(c);
                if (result.size() >= 255) break;
            }
            wordPieces(word);
        };
        // Preserve BERT's literal added special tokens before lowercasing.
        static const std::array<const char*, 5> specials {"[UNK]", "[SEP]", "[PAD]", "[CLS]", "[MASK]"};
        static const std::array<int64_t, 5> specialIds {100, 102, 0, 101, 103};
        const auto text = original.substring(0, 8192);
        int at = 0;
        while (at < text.length() && result.size() < 255) {
            int next = text.length(), which = -1;
            for (int i = 0; i < (int) specials.size(); ++i) if (const int found = text.indexOf(at, specials[(size_t) i]); found >= 0 && found < next) { next = found; which = i; }
            basic(text.substring(at, next));
            if (which < 0) break;
            if (result.size() < 255) result.push_back(specialIds[(size_t) which]);
            at = next + (int) std::strlen(specials[(size_t) which]);
        }
        result.push_back(102);
        return result;
    }
};

TextEmbedding::TextEmbedding(const juce::File& directory, bool coreML) : impl(std::make_unique<Impl>()) {
   #if RD_HAVE_ONNX
    impl->manifest = juce::JSON::parse(directory.getChildFile("manifest.json").loadFileAsString());
    if ((int) get(impl->manifest, "dimensions", 0) != textEmbeddingDimensions || get(impl->manifest, "revision").toString() != "1110a243fdf4706b3f48f1d95db1a4f5529b4d41") throw std::runtime_error("Install the pinned neural assets with native/tools/fetch-neural-assets.py");
    const auto files = get(impl->manifest, "files");
    if (get(files, "model.onnx").toString() != "6fd5d72fe4589f189f8ebc006442dbb529bb7ce38f8082112682524616046452" ||
        get(files, "vocab.txt").toString() != "07eced375cec144d27c900241f3e339478dec958f92fddbc551f295c992038a3") throw std::runtime_error("Unsupported neural model or vocabulary revision");
    for (const char* name : {"model.onnx", "vocab.txt", "tokenizer.json", "tokenizer_config.json", "config.json", "pooling.json"})
        if (fileHash(directory.getChildFile("minilm").getChildFile(name)) != files.getProperty(name, {}).toString()) throw std::runtime_error("Neural model or tokenizer checksum mismatch");
    const auto vocabulary = juce::StringArray::fromLines(directory.getChildFile("minilm/vocab.txt").loadFileAsString());
    for (int i = 0; i < vocabulary.size(); ++i) if (vocabulary[i].isNotEmpty()) impl->vocabulary.emplace(vocabulary[i].toStdString(), i);
    if (impl->vocabulary.size() != 30522 || impl->vocabulary.at("[CLS]") != 101) throw std::runtime_error("Unsupported BERT vocabulary");
    const auto runtime = directory.getChildFile("onnxruntime/lib/libonnxruntime.1.23.1.dylib");
    if (!impl->module.open(runtime.getFullPathName())) throw std::runtime_error("Cannot load native ONNX Runtime; macOS 13.4 or later is required");
    using Entry = const OrtApiBase* (ORT_API_CALL*)();
    const auto entry = reinterpret_cast<Entry>(impl->module.getFunction("OrtGetApiBase"));
    if (!entry) throw std::runtime_error("Native ONNX Runtime has no API entry point");
    const auto* base = entry();
    if (juce::String(base->GetVersionString()) != "1.23.1") throw std::runtime_error("Unexpected native ONNX Runtime version");
    impl->api = base->GetApi(ORT_API_VERSION);
    if (!impl->api) throw std::runtime_error("Unsupported native ONNX Runtime API");
    auto& api = *impl->api;
    impl->check(api.CreateEnv(ORT_LOGGING_LEVEL_ERROR, "RoliDrummer", &impl->environment));
    impl->check(api.DisableTelemetryEvents(impl->environment));
    impl->check(api.CreateSessionOptions(&impl->options));
    impl->check(api.SetIntraOpNumThreads(impl->options, 1));
    impl->check(api.SetInterOpNumThreads(impl->options, 1));
    impl->check(api.SetSessionGraphOptimizationLevel(impl->options, ORT_ENABLE_ALL));
    if (coreML) {
        using Append = OrtStatus* (ORT_API_CALL*)(OrtSessionOptions*, uint32_t);
        if (auto append = reinterpret_cast<Append>(impl->module.getFunction("OrtSessionOptionsAppendExecutionProvider_CoreML"))) {
            if (auto* status = append(impl->options, 0)) api.ReleaseStatus(status);
            else impl->provider = "CoreML";
        }
    }
    const auto model = directory.getChildFile("minilm/model.onnx").getFullPathName();
    impl->check(api.CreateSession(impl->environment, model.toRawUTF8(), impl->options, &impl->session));
    impl->check(api.CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &impl->memory));
    impl->check(api.GetAllocatorWithDefaultOptions(&impl->allocator));
    char* output = nullptr;
    impl->check(api.SessionGetOutputName(impl->session, 0, impl->allocator, &output));
    impl->outputName = output;
    impl->allocator->Free(impl->allocator, output);
    impl->identity = juce::SHA256(juce::JSON::toString(impl->manifest, true).toUTF8()).toHexString() + ":bert-unicode15-v1:" + impl->provider;
   #else
    juce::ignoreUnused(directory, coreML);
    throw std::runtime_error("Build neural assets with native/tools/fetch-neural-assets.py and reconfigure CMake");
   #endif
}
TextEmbedding::~TextEmbedding() = default;
std::shared_ptr<TextEmbedding> TextEmbedding::acquire(const juce::File& directory, bool coreML) {
    static std::mutex mutex;
    static std::map<juce::String, std::weak_ptr<TextEmbedding>> models;
    std::lock_guard lock(mutex);
    auto& cached = models[directory.getFullPathName() + (coreML ? ":coreml" : ":cpu")];
    if (auto result = cached.lock()) return result;
    auto result = std::shared_ptr<TextEmbedding>(new TextEmbedding(directory, coreML)); cached = result; return result;
}
std::vector<int64_t> TextEmbedding::tokenize(const juce::String& text) const { return impl->tokenize(text); }
juce::String TextEmbedding::version() const { return impl->identity; }
juce::var TextEmbedding::info() const { return object({{"model", get(impl->manifest, "model")}, {"revision", get(impl->manifest, "revision")}, {"dimensions", textEmbeddingDimensions}, {"provider", impl->provider}, {"version", version()}}); }
std::vector<TextVector> TextEmbedding::encode(const std::vector<juce::String>& texts) {
    if (texts.size() > 512) throw std::runtime_error("Batch at most 512 embedding descriptions per request");
   #if RD_HAVE_ONNX
    std::lock_guard lock(impl->mutex);
    std::vector<TextVector> result;
    auto& api = *impl->api;
    for (size_t start = 0; start < texts.size(); start += 8) {
        const auto count = std::min(size_t(8), texts.size() - start);
        std::vector<std::vector<int64_t>> tokenized;
        size_t width = 2;
        for (size_t i = 0; i < count; ++i) { tokenized.push_back(impl->tokenize(texts[start + i])); width = std::max(width, tokenized.back().size()); }
        std::array<std::vector<int64_t>, 3> data;
        for (auto& values : data) values.resize(count * width);
        for (size_t i = 0; i < count; ++i) for (size_t j = 0; j < tokenized[i].size(); ++j) { data[0][i * width + j] = tokenized[i][j]; data[1][i * width + j] = 1; }
        const int64_t shape[] {(int64_t) count, (int64_t) width};
        std::array<OrtValue*, 3> inputs {};
        OrtValue* output = nullptr;
        auto release = [&] { for (auto* value : inputs) if (value) api.ReleaseValue(value); if (output) api.ReleaseValue(output); };
        try {
            for (int i = 0; i < 3; ++i) impl->check(api.CreateTensorWithDataAsOrtValue(impl->memory, data[(size_t) i].data(), data[(size_t) i].size() * sizeof(int64_t), shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &inputs[(size_t) i]));
            const char* names[] {"input_ids", "attention_mask", "token_type_ids"};
            const OrtValue* values[] {inputs[0], inputs[1], inputs[2]};
            const char* outputs[] {impl->outputName.toRawUTF8()};
            impl->check(api.Run(impl->session, nullptr, names, values, 3, outputs, 1, &output));
            OrtTensorTypeAndShapeInfo* type = nullptr;
            impl->check(api.GetTensorTypeAndShape(output, &type));
            size_t dimensions = 0;
            auto* status = api.GetDimensionsCount(type, &dimensions);
            int64_t actual[3] {};
            if (!status && dimensions == 3) status = api.GetDimensions(type, actual, 3);
            api.ReleaseTensorTypeAndShapeInfo(type);
            impl->check(status);
            if (dimensions != 3 || actual[0] != (int64_t) count || actual[1] != (int64_t) width || actual[2] != textEmbeddingDimensions) throw std::runtime_error("Unexpected neural model output shape");
            float* hidden = nullptr;
            impl->check(api.GetTensorMutableData(output, reinterpret_cast<void**>(&hidden)));
            for (size_t i = 0; i < count; ++i) {
                TextVector vector {};
                double norm = 0;
                for (int d = 0; d < textEmbeddingDimensions; ++d) {
                    double sum = 0;
                    for (size_t token = 0; token < tokenized[i].size(); ++token) sum += hidden[(i * width + token) * textEmbeddingDimensions + (size_t) d];
                    vector[(size_t) d] = (float) (sum / tokenized[i].size()); norm += vector[(size_t) d] * vector[(size_t) d];
                }
                if (!std::isfinite(norm) || norm < 1e-20) throw std::runtime_error("Neural model produced an invalid embedding");
                for (auto& value : vector) value = (float) (value / std::sqrt(norm));
                result.push_back(vector);
            }
        } catch (...) { release(); throw; }
        release();
    }
    return result;
   #else
    juce::ignoreUnused(texts);
    throw std::runtime_error("Native embedding inference was not built");
   #endif
}
}
