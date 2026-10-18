#include <drummer/TextEmbedding.h>
#include <drummer/Json.h>
#include <iostream>

int main(int argc, char** argv) { try {
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add(juce::String::fromUTF8(argv[i]));
    auto directory = drummer::neuralAssetsDirectory();
    if (const int at = args.indexOf("--assets"); at >= 0 && at + 1 < args.size()) { directory = juce::File(args[at + 1]); args.removeRange(at, 2); }
    const bool coreML = args.contains("--coreml"); args.removeString("--coreml");
    const bool tokensOnly = args.contains("--tokens"); args.removeString("--tokens");
    if (args.isEmpty()) { std::cerr << "Usage: drummer-embedding [--assets DIRECTORY] [--coreml] [--tokens] TEXT...\n"; return 1; }
    auto model = drummer::TextEmbedding::acquire(directory, coreML);
    std::vector<juce::String> texts;
    for (const auto& text : args) texts.push_back(text);
    const auto embeddings = tokensOnly ? std::vector<drummer::TextVector>() : model->encode(texts);
    juce::Array<juce::var> rows;
    for (size_t i = 0; i < texts.size(); ++i) {
        juce::Array<juce::var> tokens, vector;
        for (auto token : model->tokenize(texts[i])) tokens.add((juce::int64) token);
        if (!tokensOnly) for (auto value : embeddings[i]) vector.add(value);
        rows.add(drummer::json::object({{"text", texts[i]}, {"tokens", tokens}, {"vector", vector}}));
    }
    auto result = model->info(); drummer::json::set(result, "rows", rows);
    std::cout << juce::JSON::toString(result, true) << '\n';
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
