#include <drummer/PatternLibrary.h>
#include <drummer/NeuralIndex.h>
#include <drummer/PatternFamilies.h>
#include <drummer/Json.h>
#include <iostream>
#include <csignal>
#include <set>

namespace { volatile std::sig_atomic_t interrupted = 0; void interrupt(int) { interrupted = 1; } }

int main(int argc, char** argv) { try {
    std::signal(SIGINT, interrupt); std::signal(SIGTERM, interrupt);
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add(juce::String::fromUTF8(argv[i]));
    auto option = [&](const juce::String& name, juce::String fallback = {}) { const int at = args.indexOf(name); return at >= 0 && at + 1 < args.size() ? args[at + 1] : fallback; };
    const auto database = option("--db");
    if (args.isEmpty() || database.isEmpty()) {
        std::cerr << "Usage: drummer-library import SOURCE --db CATALOG [--map auto|gm|vdrums] [--cancel-after COUNT]\n"
                     "       drummer-library status|browse|pattern|errors --db CATALOG [--query TEXT] [--id ID] [--role ROLE] [--offset N] [--limit N]\n"
                     "       drummer-library index|neural-status|search --db CATALOG [--assets DIR] [--query TEXT] [--index ID] [--cpu]\n"
                     "       drummer-library evaluate --db CATALOG --queries QUERY_JSON [--index ID] [--cpu]\n"
                     "       drummer-library families|family-reconcile|family-suggestions --db CATALOG\n"
                     "       drummer-library family-register --db CATALOG --query TEXT [--name TEXT] [--numerator N --denominator D] [--feel straight|triplet|any]\n"
                     "       drummer-library family-members --db CATALOG --id FAMILY [--offset N --limit N]\n"
                     "       drummer-library family-update --db CATALOG --id FAMILY --revision N --action rename|include|exclude|auto [--name TEXT] [--pattern ID]\n";
        return 1;
    }
    drummer::PatternLibrary library {juce::File(database)};
    juce::var result;
    if (args[0] == "import") {
        if (args.size() < 2) throw std::runtime_error("Missing archive or directory");
        drummer::LibraryImportOptions options;
        options.mapping = drummer::json::object({{"id", option("--map", "auto")}});
        int64_t scanned = 0, last = 0;
        const auto stop = option("--cancel-after").getLargeIntValue();
        options.cancelled = [&] { return interrupted || (stop > 0 && scanned >= stop); };
        options.progress = [&](const auto& value) {
            scanned = (juce::int64) drummer::json::get(value, "scanned");
            const auto now = juce::Time::currentTimeMillis();
            if (now - last > 1000 || drummer::json::get(value, "state").toString() != "indexing") { std::cerr << juce::JSON::toString(value, true) << '\n'; last = now; }
        };
        result = library.importSource(juce::File(args[1]), options);
    } else if (args[0] == "status") result = library.status();
    else if (args[0] == "index" || args[0] == "neural-status" || args[0] == "search" || args[0] == "evaluate") {
        const auto assets = option("--assets");
        drummer::NeuralIndex neural(juce::File(database), assets.isEmpty() ? drummer::neuralAssetsDirectory() : juce::File(assets));
        if (args[0] == "index") {
            // Initialize all catalog layouts before the worker takes a long
            // metadata write, so another instrument instance can open read-only.
            drummer::PatternFamilies initialized {juce::File(database)};
            drummer::NeuralBuildOptions options;
            options.cancelled = [&] { return interrupted != 0; };
            int64_t last = 0;
            options.progress = [&](const auto& value) { const auto now = juce::Time::currentTimeMillis(); if (now - last > 1000 || drummer::json::get(value, "state").toString() != "building") { std::cerr << juce::JSON::toString(value, true) << '\n'; last = now; } };
            result = neural.build(options);
        } else if (args[0] == "neural-status") result = neural.status();
        else if (args[0] == "evaluate") {
            using namespace drummer::json;
            const auto suite = juce::JSON::parse(juce::File(option("--queries")).loadFileAsString());
            const auto queries = get(suite, "queries");
            if (!queries.isArray() || queries.getArray()->isEmpty()) throw std::runtime_error("Expected a JSON retrieval query suite");
            juce::Array<juce::var> reports;
            juce::String indexId = option("--index");
            double semanticTotal = 0, lexicalTotal = 0;
            for (const auto& test : *queries.getArray()) {
                if (interrupted) throw std::runtime_error("Retrieval evaluation cancelled");
                auto search = object({{"query", get(test, "query")}, {"role", get(test, "role", "")}, {"limit", 5}, {"indexId", indexId}, {"cpu", args.contains("--cpu")}});
                const auto semantic = neural.search(search), lexical = library.browse(search);
                indexId = get(semantic, "indexId").toString();
                auto measure = [&](const juce::var& results) {
                    const auto rows = get(results, "rows"), labels = get(test, "labels");
                    if (!rows.isArray() || !labels.isArray()) throw std::runtime_error("Relevance proxy requires result rows and expected path labels");
                    int hits = 0;
                    std::set<juce::String> families;
                    juce::Array<juce::var> matches;
                    for (const auto& row : *rows.getArray()) {
                        const auto path = get(row, "path").toString().toLowerCase().replace("_", " ");
                        bool relevant = false;
                        for (const auto& label : *labels.getArray()) relevant = relevant || path.contains(label.toString().toLowerCase());
                        hits += relevant;
                        families.insert(get(row, "semanticId", get(row, "family")).toString());
                        matches.add(object({{"patternId", get(row, "patternId")}, {"path", get(row, "path")}, {"role", get(row, "role")}, {"description", get(row, "description")}, {"similarity", get(row, "similarity")}, {"pathLabelMatch", relevant}}));
                    }
                    return object({{"precisionAt5", hits / 5.0}, {"returned", rows.getArray()->size()}, {"families", (int) families.size()}, {"matches", matches}});
                };
                const auto a = measure(semantic), b = measure(lexical);
                semanticTotal += number(a, "precisionAt5", 0); lexicalTotal += number(b, "precisionAt5", 0);
                reports.add(object({{"query", get(test, "query")}, {"role", get(test, "role", "")}, {"semantic", a}, {"lexical", b}, {"provider", get(semantic, "provider")}, {"milliseconds", get(semantic, "milliseconds")}}));
                std::cerr << get(test, "query").toString() << ": semantic " << number(a, "precisionAt5", 0) << ", lexical " << number(b, "precisionAt5", 0) << '\n';
            }
            result = object({{"description", get(suite, "description")}, {"indexId", indexId}, {"semanticMeanPrecisionAt5", semanticTotal / reports.size()}, {"lexicalMeanPrecisionAt5", lexicalTotal / reports.size()}, {"queries", reports}});
        }
        else result = neural.search(drummer::json::object({{"query", option("--query")}, {"indexId", option("--index")}, {"role", option("--role")}, {"limit", option("--limit", "25").getIntValue()}, {"cpu", args.contains("--cpu")}, {"numerator", option("--numerator", "0").getIntValue()}, {"denominator", option("--denominator", "0").getIntValue()}}));
    }
    else if(args[0].startsWith("family-")||args[0]=="families"){
        using namespace drummer::json;
        const auto assets=option("--assets");drummer::NeuralIndex index(juce::File(database),assets.isEmpty()?drummer::neuralAssetsDirectory():juce::File(assets));drummer::PatternFamilies families{juce::File(database)};
        if(args[0]=="families")result=families.status();
        else if(args[0]=="family-register")result=families.registerFamily(object({{"query",option("--query")},{"name",option("--name",option("--query"))},{"numerator",option("--numerator","0").getIntValue()},{"denominator",option("--denominator","0").getIntValue()},{"feel",option("--feel")}}));
        else if(args[0]=="family-reconcile"){
            drummer::NeuralBuildOptions options;options.cancelled=[&]{return interrupted!=0;};int64_t last=0;options.progress=[&](const auto& value){const auto now=juce::Time::currentTimeMillis();if(now-last>1000||get(value,"state").toString()!="building"){std::cerr<<juce::JSON::toString(value,true)<<'\n';last=now;}};result=families.reconcile(index,options);
        }else if(args[0]=="family-members")result=families.members(object({{"familyId",option("--id")},{"offset",option("--offset","0").getIntValue()},{"limit",option("--limit","25").getIntValue()}}));
        else if(args[0]=="family-suggestions")result=families.suggestions(option("--offset","0").getIntValue(),option("--limit","25").getIntValue());
        else if(args[0]=="family-update")result=families.update(object({{"familyId",option("--id")},{"action",option("--action")},{"name",option("--name")},{"patternId",option("--pattern")},{"revision",(juce::int64)option("--revision").getLargeIntValue()}}));
        else throw std::runtime_error("Unknown family command");
    }
    else if (args[0] == "browse") result = library.browse(drummer::json::object({{"query", option("--query")}, {"role", option("--role")}, {"offset", option("--offset", "0").getIntValue()}, {"limit", option("--limit", "25").getIntValue()}}));
    else if (args[0] == "pattern") result = library.pattern(option("--id"));
    else if (args[0] == "errors") result = library.errors(option("--id"), option("--offset", "0").getIntValue(), option("--limit", "25").getIntValue());
    else throw std::runtime_error("Unknown catalog command");
    std::cout << juce::JSON::toString(result, true) << '\n';
    const auto state=drummer::json::get(result,"state").toString();return state=="paused"?2:state=="obsolete"?3:state=="busy"?4:0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
