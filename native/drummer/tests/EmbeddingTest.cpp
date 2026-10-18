#include <drummer/TextEmbedding.h>
#include <drummer/Json.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
double dot(const drummer::TextVector& a, const drummer::TextVector& b) { double sum = 0; for (size_t i = 0; i < a.size(); ++i) sum += a[i] * b[i]; return sum; }
}
int main(int argc, char** argv) { try {
    using namespace drummer;
    using namespace drummer::json;
    if (argc != 3) throw std::runtime_error("Expected neural assets and tokenizer reference paths");
    const juce::File directory(juce::String::fromUTF8(argv[1]));
    auto model = TextEmbedding::acquire(directory);
    check(model == TextEmbedding::acquire(directory), "Instrument instances do not share their embedding session");
    juce::var reference;
    const auto parsed = juce::JSON::parse(juce::File(juce::String::fromUTF8(argv[2])).loadFileAsString(), reference);
    if (parsed.failed()) throw std::runtime_error("Invalid tokenizer reference: " + parsed.getErrorMessage().toStdString());
    auto cases = get(reference, "cases");
    check(cases.isArray(), "Tokenizer reference must contain cases");
    for (const auto& test : *cases.getArray()) {
        const auto text = get(test, "text").toString();
        const auto tokens = model->tokenize(text);
        auto expected = get(test, "tokens");
        bool equal = (int) tokens.size() == expected.getArray()->size();
        for (int i = 0; equal && i < (int) tokens.size(); ++i) equal = tokens[(size_t) i] == (juce::int64) (*expected.getArray())[i];
        if (!equal) throw std::runtime_error("Native tokenizer differs from pinned BERT reference: " + text.substring(0, 80).toStdString());
    }
    const std::vector<juce::String> texts {"heavy rock drum groove", "hard rock drum beat", "gentle bossa nova percussion", "swinging jazz brushes"};
    const auto vectors = model->encode(texts);
    check(vectors.size() == texts.size(), "Embedding batch lost a description");
    for (const auto& vector : vectors) check(std::abs(dot(vector, vector) - 1) < 1e-5, "Sentence embedding is not L2 normalized");
    const auto vectorReference = juce::JSON::parse(juce::File(juce::String::fromUTF8(argv[2])).getSiblingFile("embedding-reference.json").loadFileAsString());
    auto referenceCases = get(vectorReference, "cases");
    check(referenceCases.isArray() && referenceCases.getArray()->size() == (int) vectors.size(), "Independent embedding reference is missing");
    double maximumError = 0;
    for (size_t i = 0; i < vectors.size(); ++i) {
        auto expected = get((*referenceCases.getArray())[(int) i], "vector");
        check(expected.isArray() && expected.getArray()->size() == textEmbeddingDimensions, "Independent embedding reference has the wrong dimensions");
        for (int d = 0; d < textEmbeddingDimensions; ++d) maximumError = std::max(maximumError, std::abs(double(vectors[i][(size_t) d]) - (double) (*expected.getArray())[d]));
    }
    check(maximumError < .00001, "Native vectors differ from independent pinned ONNX mean-pooling reference");
    check(dot(vectors[0], vectors[1]) > dot(vectors[0], vectors[2]) + .1, "Model does not separate hard rock from bossa nova");
    const auto single = model->encode({texts[0]});
    check(dot(single[0], vectors[0]) > .999999, "Padding or batch size changed the sentence embedding");
    check(model->encode({}).empty(), "Empty embedding request did not return an empty result");
    std::cout << "embedding: pinned tokenizer, Unicode, truncation, shared native session, normalized MiniLM vectors, style separation and batch invariance passed; reference max error " << maximumError << '\n';
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
