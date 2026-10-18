#include <drummer/PatchResolver.h>
#include <drummer/PatternAnalysis.h>
#include <drummer/PatternVariation.h>
#include <drummer/PatternFamilies.h>
#include <drummer/RhythmSpace.h>
#include <drummer/Scheduler.h>
#include <drummer/Json.h>
#include "PartQuery.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>

namespace drummer {
using namespace json;
namespace {
juce::String hash(const juce::String& text) { return juce::SHA256(text.toUTF8()).toHexString(); }
int feel(const juce::var& row) {
    const auto description = " " + get(row, "semanticDescription").toString() + " ";
    if (description.contains(" triplet ") || description.contains(" swing ") || description.contains(" shuffle ")) return 1;
    if (description.contains(" straight ")) return 0;
    auto features = get(row, "features");
    if (number(features, "tripletFit", 0) > .9 && number(features, "sixteenthFit", 1) < .8) return 1;
    if (number(features, "sixteenthFit", 0) > .9 && number(features, "tripletFit", 1) < .8) return 0;
    return -1;
}
struct PartFeatures {
    double notes = 0, density = 0, velocity = 0, ending = 0, syncopation = 0;
    double accents=0,ghosts=0,microtiming=0,triplets=0;
};
constexpr double metadataPrecision=1e-10;
double quantize(double value,double precision=metadataPrecision){const double rounded=std::round(value/precision)*precision;return rounded==0?0:rounded;}
RhythmVector rhythm(const PartFeatures& f){RhythmVector result{{std::log1p(f.density),f.velocity,f.accents,f.ghosts,f.ending,f.syncopation,f.microtiming,f.triplets}};for(auto& value:result)value=quantize(value,1e-12);return result;}
juce::var canonicalMetrics(const juce::var& input){
    if(input.isDouble())return quantize((double)input);
    if(auto* entries=input.getArray()){juce::Array<juce::var> result;for(const auto& entry:*entries)result.add(canonicalMetrics(entry));return result;}
    if(auto* entries=input.getDynamicObject()){auto result=object({});for(const auto& entry:entries->getProperties())set(result,entry.name.toString().toRawUTF8(),canonicalMetrics(entry.value));return result;}
    return input;
}
juce::var serializeSpace(const RhythmSpace& space){
    auto vector=[](const RhythmVector& values){juce::Array<juce::var> result;for(auto v:values)result.add(quantize(v));return result;};
    juce::Array<juce::var> components;for(const auto& axis:space.components)components.add(vector(axis));
    return object({{"version",RhythmSpace::version},{"precision",metadataPrecision},{"mean",vector(space.mean)},{"scale",vector(space.scale)},{"eigenvalues",vector(space.eigenvalues)},{"components",components},
        {"features",juce::Array<juce::var>{"logDensity","velocity","accentShare","ghostShare","endingShare","syncopation","microtiming","tripletFit"}}});
}
PartFeatures partFeatures(const juce::var& row, const std::set<int>& notes) {
    static const auto canonical = get(drumMapDefinitions(), "canonical");
    PartFeatures result;
    auto parts = get(get(row, "features"), "parts");
    if (auto* object = parts.getDynamicObject()) for (const auto& part : object->getProperties()) {
        if (!notes.contains((int) canonical.getProperty(part.name, -1))) continue;
        const auto count = number(part.value, "notes", 0);
        result.notes += count;
        result.density += number(part.value, "density", 0);
        result.velocity += count * number(part.value, "velocity", 0);
        result.ending += count * number(part.value, "endingShare", 0);
        result.syncopation += count * number(part.value, "syncopation", 0);
        result.accents+=count*number(part.value,"accentShare",0);result.ghosts+=count*number(part.value,"ghostShare",0);
        result.microtiming+=count*number(part.value,"microtiming",0);result.triplets+=count*number(part.value,"tripletFit",0);
    }
    if (result.notes > 0) { result.velocity /= result.notes; result.ending /= result.notes; result.syncopation /= result.notes;result.accents/=result.notes;result.ghosts/=result.notes;result.microtiming/=result.notes;result.triplets/=result.notes; }
    return result;
}
struct Candidate { juce::var row; PartFeatures part; };
}

juce::var resolveNeuralPatch(PatternLibrary& library, NeuralIndex& index, const juce::var& input, const juce::var& options,PatchResolveMetrics* metrics) {
    PatchResolveMetrics measured;const double started=juce::Time::getMillisecondCounterHiRes();
    if ((int) get(input, "version") != 1 || !get(input, "keys").isArray()) throw std::runtime_error("Resolve requires a supported instrument patch");
    const auto query = get(options, "query").toString().trim().substring(0, 256);
    if (query.isEmpty()) throw std::runtime_error("Enter a musical patch name to resolve");
    const int phraseBars=std::clamp((int)number(options,"phraseBars",8),1,64);
    const int variationCount=std::clamp((int)number(options,"variationCount",0),0,4);
    PatternFamilies families(library.file());
    juce::var found,indexId,familySelection;
    int numerator=0,denominator=0,selectedFeel=0;double threshold=0;bool familyBound=false;
    const auto known=families.lookup(object({{"query",query},{"numerator",get(options,"numerator",0)},{"denominator",get(options,"denominator",0)},{"feel",get(options,"feel")},{"familyId",get(options,"familyId")}}));
    // Registered meter/feel are explicit intent. Avoid redoing broad catalog
    // searches to infer them; the committed membership already used GPU search.
    if(get(known,"id").toString().isNotEmpty()&&(int)get(known,"numerator")>0&&get(known,"feel").toString()!="any"){
        numerator=(int)get(known,"numerator");denominator=(int)get(known,"denominator");selectedFeel=get(known,"feel").toString()=="triplet"?1:0;
        indexId=get(options,"indexId");if(indexId.toString().isEmpty())indexId=get(index.status(),"readyIndexId");
        familySelection=families.selection(index,object({{"query",query},{"indexId",indexId},{"numerator",numerator},{"denominator",denominator},{"feel",selectedFeel?"triplet":"straight"},{"familyId",get(known,"id")},{"familyBuildId",get(options,"familyBuildId")},{"cpu",get(options,"cpu",false)}}));
        familyBound=(bool)get(familySelection,"available",false);
        if(familyBound)found=object({{"indexId",indexId},{"revision",get(familySelection,"libraryRevision")},{"modelVersion",get(familySelection,"modelVersion")},{"interpretedQuery",get(familySelection,"interpretedQuery")}});
    }
    if(!familyBound){
        auto searchOptions = object({{"query", query}, {"indexId", get(options, "indexId")}, {"limit", 200}, {"groups", 128}, {"cpu", get(options, "cpu", false)}});
        found = index.search(searchOptions);
        auto matches = get(found, "rows");
        if (!matches.isArray() || matches.getArray()->isEmpty()) throw std::runtime_error("No usable patterns match this patch name");
        indexId = get(found, "indexId"); set(searchOptions, "indexId", indexId);
        numerator = (int) number(options, "numerator", 0); denominator = (int) number(options, "denominator", 0);
        if (numerator <= 0 || denominator <= 0) {
            const auto queryText = query.toStdString();
            static const std::regex meter(R"(\b([1-9][0-9]?)\s*[/#]\s*(1|2|4|8|16|32)\b)");
            std::smatch match;
            if (std::regex_search(queryText, match, meter)) { numerator = std::stoi(match[1]); denominator = std::stoi(match[2]); }
            else {
                // Choose the best-supported compatible meter, counting a semantic
                // family once rather than letting thousands of exports vote.
                std::map<std::pair<int,int>, std::map<juce::String,double>> evidence;
                for (const auto& row : *matches.getArray()) {
                    const auto signature = std::make_pair((int) get(row, "numerator"), (int) get(row, "denominator"));
                    auto& weight = evidence[signature][get(row, "semanticId").toString()];
                    weight = std::max(weight, number(row, "similarity", 0));
                }
                double best = -1;
                for (const auto& [signature, groups] : evidence) {
                    double weight = 0; for (const auto& [id, score] : groups) weight += score * score * score * score;
                    if (weight > best) { best = weight; numerator = signature.first; denominator = signature.second; }
                }
            }
        }
        if (numerator < 1 || numerator > 64 || (denominator != 1 && denominator != 2 && denominator != 4 && denominator != 8 && denominator != 16 && denominator != 32)) throw std::runtime_error("Unsupported musical meter");
        set(searchOptions, "numerator", numerator); set(searchOptions, "denominator", denominator);
        const auto grooves = index.search(searchOptions);
        const auto topRows = get(grooves, "rows");
        if (topRows.getArray()->isEmpty()) throw std::runtime_error("No patterns match the requested meter");
        const auto top = (*topRows.getArray())[0];
        threshold = number(top, "similarity", 0) - .12;
        selectedFeel = feel(top);
        const auto queryWords = " " + query.toLowerCase() + " ";
        if (queryWords.contains(" swing ") || queryWords.contains(" shuffle ") || queryWords.contains(" triplet ")) selectedFeel = 1;
        if (queryWords.contains(" straight ")) selectedFeel = 0;
        if (selectedFeel < 0) selectedFeel = 0;
        const auto requestedFeel=get(options,"feel").toString();if(requestedFeel=="triplet")selectedFeel=1;else if(requestedFeel=="straight")selectedFeel=0;
        familySelection=families.selection(index,object({{"query",query},{"indexId",indexId},{"numerator",numerator},{"denominator",denominator},{"feel",selectedFeel?"triplet":"straight"},{"familyId",get(options,"familyId")},{"familyBuildId",get(options,"familyBuildId")},{"cpu",get(options,"cpu",false)}}));
        familyBound=(bool)get(familySelection,"available",false);
    }
    if(familyBound){const auto rows=get(familySelection,"rows");if(rows.getArray()->isEmpty())throw std::runtime_error("This family has no confirmed compatible patterns. Include an ambiguous pattern or revise its decisions.");}
    SemanticAffinities familyContext;
    if(familyBound)familyContext=index.affinities(query,indexId.toString(),(bool)get(options,"cpu",false));
    measured.intentMilliseconds=juce::Time::getMillisecondCounterHiRes()-started;
    std::map<std::set<int>,std::map<juce::String,juce::var>> partPools;
    if(familyBound){
        const double begin=juce::Time::getMillisecondCounterHiRes();
        for(const auto& key:*get(input,"keys").getArray())if(!(bool)get(key,"neuralLock",false)){const auto mask=PartQuery::mask(get(key,"parts"));if(!mask.empty())partPools.try_emplace(mask);}
        if(!partPools.empty()){
            juce::Array<juce::var> masks;for(const auto& [parts,unused]:partPools){juce::Array<juce::var> notes;for(int note:parts)notes.add(note);masks.add(notes);}
            const auto selection=families.selection(index,object({{"query",query},{"indexId",indexId},{"partMasks",masks},{"numerator",numerator},{"denominator",denominator},{"feel",selectedFeel?"triplet":"straight"},{"phraseBars",phraseBars},{"familyId",get(familySelection,"familyId")},{"familyBuildId",get(familySelection,"familyBuildId")},{"cpu",get(options,"cpu",false)}}),&familyContext);
            for(const auto& pool:*get(selection,"pools").getArray()){auto& target=partPools.at(PartQuery::mask(get(pool,"parts")));for(const auto& row:*get(pool,"rows").getArray())target.insert_or_assign(get(row,"patternId").toString()+":"+get(row,"role").toString(),row);}
        }
        measured.partMasks=(int)partPools.size();measured.partCandidateMilliseconds=juce::Time::getMillisecondCounterHiRes()-begin;
    }
    auto candidatesFor=[&](const std::set<int>& parts)->const std::map<juce::String,juce::var>&{
        auto [entry,inserted]=partPools.try_emplace(parts);if(!inserted)return entry->second;
        const double begin=juce::Time::getMillisecondCounterHiRes();++measured.partMasks;
        juce::Array<juce::var> mask;for(int note:parts)mask.add(note);
        auto select=object({{"query",query},{"indexId",indexId},{"parts",mask},{"numerator",numerator},{"denominator",denominator},{"feel",selectedFeel?"triplet":"straight"},{"phraseBars",phraseBars},{"cpu",get(options,"cpu",false)},{"limit",200},{"groups",128},{"minSimilarity",std::max(.25,threshold)}});
        auto append=[&](const juce::var& result){const auto rows=get(result,"rows");if(auto* list=rows.getArray())for(const auto& row:*list)entry->second.insert_or_assign(get(row,"patternId").toString()+":"+get(row,"role").toString(),row);};
        if(familyBound){set(select,"familyId",get(familySelection,"familyId"));set(select,"familyBuildId",get(familySelection,"familyBuildId"));append(families.selection(index,select,&familyContext));}
        else{append(index.search(select));set(select,"role","fill");append(index.search(select));}
        measured.partCandidateMilliseconds+=juce::Time::getMillisecondCounterHiRes()-begin;
        return entry->second;
    };
    auto document = input.clone();
    auto keys = get(document, "keys");
    std::map<juce::String, juce::var> prepared;
    std::unique_ptr<PatternReader> reader;
    std::set<juce::String> incompatible;
    juce::Array<juce::var> resolved, warnings;
    auto bind = [&](const Candidate& candidate) {
        const auto id = get(candidate.row, "patternId").toString();
        if(incompatible.contains(id))return juce::String();
        if (!prepared.contains(id)) {
            const double begin=juce::Time::getMillisecondCounterHiRes();++measured.clipLoads;
            if(!reader)reader=std::make_unique<PatternReader>(library.file());
            auto clip = reader->pattern(get(candidate.row, "id").toString());
            measured.clipLoadMilliseconds+=juce::Time::getMillisecondCounterHiRes()-begin;
            auto metadata = get(clip, "metadata");
            auto meterMap = get(metadata, "signatures");
            if (auto* changes = meterMap.getArray()) for (const auto& change : *changes)
                if ((int) get(change, "numerator") != numerator || (int) get(change, "denominator") != denominator){incompatible.insert(id);return juce::String();}
            prepared.emplace(id, clip);
        }
        return id;
    };
    int changed = 0,generatedCount=0;
    for (auto& key : *keys.getArray()) {
        if ((bool) get(key, "neuralLock", false)) continue;
        const auto parts=PartQuery::mask(get(key,"parts"));
        if (parts.empty()) continue;
        std::vector<Candidate> steady, fill;
        const double bar = numerator * 4.0 / denominator;
        for (const auto& [id, row] : candidatesFor(parts)) {
            if ((!familyBound&&number(row, "similarity", 0) < threshold) || (feel(row) >= 0 && feel(row) != selectedFeel)) continue;
            const double length = number(row, "length", 0), bars = length / bar;
            const int barCount=(int)std::llround(bars);
            if (bars < .99 || bars > phraseBars || std::abs(bars - barCount) > .001 || phraseBars%std::max(1,barCount)!=0) continue;
            auto measured = partFeatures(row, parts);
            if (measured.notes == 0) continue;
            const auto role = get(row, "role").toString();
            if (role == "fill") fill.push_back({row, measured});
            else if (role == "groove" || role == "sparse") steady.push_back({row, measured});
        }
        auto activityOrder = [](const Candidate& a, const Candidate& b) {
            if (a.part.density != b.part.density) return a.part.density < b.part.density;
            if (a.part.velocity != b.part.velocity) return a.part.velocity < b.part.velocity;
            return get(a.row, "patternId").toString() < get(b.row, "patternId").toString();
        };
        std::sort(steady.begin(), steady.end(), activityOrder);
        std::sort(fill.begin(), fill.end(), activityOrder);
        auto silence=[&]{set(key,"zones",juce::Array<juce::var>{object({{"upper",1.0},{"clipId",""}})});set(key,"zoneSource",2);};
        if (steady.empty()&&fill.empty()) {
            silence();warnings.add(object({{"key", get(key, "note")}, {"reason", "No compatible pattern contains this drum part; key is silent"}}));
            continue;
        }
        const auto& sparse = steady.empty()?fill.front():steady.front();
        const auto& normal = steady.empty()?fill.front():steady[steady.size() / 2];
        const auto& ending = fill.empty() ? normal : fill.back();
        std::vector<RhythmVector> samples;std::set<juce::String> identities;
        for(const auto* list:{&steady,&fill})for(const auto& candidate:*list)if(identities.insert(get(candidate.row,"patternId").toString()).second)samples.push_back(rhythm(candidate.part));
        const auto space=RhythmSpace::fit(samples);
        struct GroupSelection{juce::String primary;juce::Array<juce::var> candidates;double density=0;};
        auto makeGroup=[&](std::vector<Candidate> choices,const Candidate& preferred,const juce::String& region){
            GroupSelection result;
            auto order=[&](const Candidate& a,const Candidate& b){const double left=quantize(space.project(rhythm(a.part))[1],1e-9),right=quantize(space.project(rhythm(b.part))[1],1e-9);return left!=right?left<right:get(a.row,"patternId").toString()<get(b.row,"patternId").toString();};
            std::sort(choices.begin(),choices.end(),order);
            std::vector<Candidate> selected;std::set<juce::String> ids;
            const int originalBudget=maxCandidates-variationCount;
            auto add=[&](const Candidate& candidate,int budget){if(selected.size()>=(size_t)budget)return;const auto id=get(candidate.row,"patternId").toString();if(ids.contains(id)||bind(candidate).isEmpty())return;ids.insert(id);selected.push_back(candidate);};
            add(preferred,originalBudget);
            const auto count=std::min((size_t)originalBudget,choices.size());
            for(size_t i=0;i<count;++i)add(choices[count==1?0:(size_t)std::llround(i*double(choices.size()-1)/(count-1))],originalBudget);
            for(const auto& candidate:choices)add(candidate,originalBudget);
            if(selected.empty())return result;
            std::sort(selected.begin(),selected.end(),order);
            if(variationCount){
                PatternVariationOptions settings;settings.count=variationCount;settings.numerator=numerator;settings.denominator=denominator;settings.phraseBars=phraseBars;settings.role=region;settings.feel=selectedFeel?"triplet":"straight";
                settings.seed=get(options,"seed","0").toString()+":"+get(key,"note").toString()+":"+region;settings.parts.assign(parts.begin(),parts.end());
                std::vector<juce::var> parents;for(const auto& candidate:selected)parents.push_back(prepared.at(get(candidate.row,"patternId").toString()));
                for(const auto& child:generatePatternVariations(parents,settings)){
                    const auto id=get(child,"id").toString();prepared.insert_or_assign(id,child);
                    auto row=object({{"patternId",id},{"features",get(child,"features")},{"generated",true}});
                    add({row,partFeatures(row,parts)},maxCandidates);++generatedCount;
                }
                // If conservative generation produced fewer children, keep the
                // available original variations in the remaining bounded slots.
                for(const auto& candidate:choices)add(candidate,maxCandidates);
                std::sort(selected.begin(),selected.end(),order);
            }
            const auto preferredId=get(preferred.row,"patternId").toString();
            result.primary=ids.contains(preferredId)?preferredId:get(selected[selected.size()/2].row,"patternId").toString();
            double low=selected.front().part.density,high=low;for(const auto& candidate:selected){low=std::min(low,candidate.part.density);high=std::max(high,candidate.part.density);}
            for(size_t i=0;i<selected.size();++i){const auto& candidate=selected[i];const auto id=get(candidate.row,"patternId").toString();
                const double activity=high>low?(candidate.part.density-low)/(high-low):.5;
                result.candidates.add(object({{"clipId",id},{"position",juce::Array<juce::var>{quantize(activity),quantize(candidate.part.accents),quantize(selected.size()>1?i/double(selected.size()-1):.5),quantize(candidate.part.velocity)}},{"libraryBinding",get(candidate.row,"id")},{"generated",get(candidate.row,"generated",false)}}));
                if(id==result.primary)result.density=candidate.part.density;
            }
            return result;
        };
        GroupSelection sparseGroup,steadyGroup;
        if(!steady.empty()){
            const auto split=std::max(size_t(1),steady.size()/3);
            sparseGroup=makeGroup(std::vector<Candidate>(steady.begin(),steady.begin()+(ptrdiff_t)split),sparse,"sparse");
            steadyGroup=makeGroup(std::vector<Candidate>(steady.begin()+(ptrdiff_t)std::min(split,steady.size()-1),steady.end()),normal,"groove");
        }
        if(steadyGroup.primary.isEmpty())steadyGroup=sparseGroup;
        if(sparseGroup.primary.isEmpty())sparseGroup=steadyGroup;
        auto fillGroup=fill.empty()?steadyGroup:makeGroup(fill,ending,"fill");
        const bool fallback=fill.empty()||fillGroup.primary.isEmpty();if(fallback)fillGroup=steadyGroup;
        if(steadyGroup.primary.isEmpty()&&fillGroup.primary.isEmpty()){silence();warnings.add(object({{"key",get(key,"note")},{"reason","Source patterns change meter inside their phrase; key is silent"}}));continue;}
        if(steadyGroup.primary.isEmpty())warnings.add(object({{"key",get(key,"note")},{"reason","This part has fills only; low and middle slide regions are silent"}}));
        const auto sparseId=sparseGroup.primary,steadyId=steadyGroup.primary,fillId=fillGroup.primary;
        auto zones = juce::Array<juce::var> {
            object({{"upper", .25}, {"clipId", sparseId}, {"role", "sparse"},{"candidates",sparseGroup.candidates}}),
            object({{"upper", .75}, {"clipId", steadyId}, {"role", "steady"},{"candidates",steadyGroup.candidates}}),
            object({{"upper", 1.0}, {"clipId", fillId}, {"role", fallback ? "steady" : "fill"},{"candidates",fillGroup.candidates}})};
        set(key, "zones", zones); set(key, "zoneSource", 2);
        set(key,"rhythmSpace",serializeSpace(space));
        if(!get(key,"patternIntent").isObject())set(key,"patternIntent",object({{"enabled",true},{"weights",juce::Array<juce::var>{.4,.2,.3,.1}},{"smoothingMs",35},{"minimumDwell",.25},{"transitionPenalty",.025}}));
        resolved.add(object({{"key", get(key, "note")}, {"sparse", sparseId}, {"steady", steadyId}, {"fill", fillId},
                             {"sparseDensity", sparseGroup.density}, {"steadyDensity", steadyGroup.density}, {"fillDensity", fillGroup.density},
                             {"fillFallback", fallback}, {"grooveCandidates", (int) steady.size()}, {"fillCandidates", (int) fill.size()}}));
        if (fallback) warnings.add(object({{"key", get(key, "note")}, {"reason", "No compatible fill contains this drum part; high slide keeps its steady groove"}}));
        ++changed;
    }
    if (!changed) throw std::runtime_error("No unlocked keys have drum parts covered by this query");
    const double documentStart=juce::Time::getMillisecondCounterHiRes();
    measured.selectionMilliseconds=documentStart-started-measured.intentMilliseconds-measured.partCandidateMilliseconds-measured.clipLoadMilliseconds;
    // Retain edited/unbound clips as a patch library; replace an equal stable
    // ID with its canonical selected notes and add only the prepared subset.
    juce::Array<juce::var> clips;
    auto existing = get(document, "clips");
    if (auto* list = existing.getArray()) for (const auto& clip : *list) if (!prepared.contains(get(clip, "id").toString())) clips.add(clip);
    for (const auto& [id, clip] : prepared){auto stored=clip.clone();set(stored,"features",canonicalMetrics(get(stored,"features")));clips.add(stored);}
    generatedCount=0;for(const auto& [id,clip]:prepared)if(get(clip,"generation").isObject())++generatedCount;
    set(document, "clips", clips); set(document, "name", query);
    const auto familyId=familyBound?get(familySelection,"familyId").toString():familyIdentity(query,numerator,denominator,selectedFeel?"triplet":"straight");
    const auto resolvedId="neural-"+hash(familyId);
    set(document, "neural", object({{"version", 1}, {"patchId", resolvedId}, {"familyId",familyId},{"familyPolicyVersion",familyPolicyVersion},{"query", query}, {"indexId", indexId},
        {"libraryRevision", get(found, "revision")}, {"modelVersion", get(found, "modelVersion")}, {"preprocessingVersion", semanticDescriptionVersion},
        {"queryVersion", musicalQueryVersion}, {"interpretedQuery", get(found, "interpretedQuery")},
        {"featureVersion", patternFeatureVersion}, {"resolverVersion", patchResolverVersion}, {"seed", get(options, "seed", "0")},
        {"numerator", numerator}, {"denominator", denominator}, {"feel", selectedFeel ? "triplet" : "straight"}, {"keys", resolved}, {"warnings", warnings}}));
    auto resolution=get(document,"neural");set(resolution,"phraseBars",phraseBars);
    set(resolution,"variationCount",variationCount);set(resolution,"generationVersion",patternVariationVersion);set(resolution,"generatedCount",generatedCount);
    set(resolution,"numericPrecision",metadataPrecision);
    if(familyBound){set(resolution,"familyBuildId",get(familySelection,"familyBuildId"));set(resolution,"familyRevision",get(familySelection,"familyRevision"));set(resolution,"familyPolicyVersion",get(familySelection,"familyPolicyVersion"));}
    const auto previous=get(input,"neural");const auto identity=get(input,"id").toString();
    bool sameFamily=false;
    if(get(previous,"familyId").toString().isNotEmpty())sameFamily=get(previous,"familyId").toString()==familyId;
    else if(get(previous,"query").toString().isNotEmpty())try{sameFamily=familyIdentity(get(previous,"query").toString(),(int)get(previous,"numerator"),(int)get(previous,"denominator"),get(previous,"feel").toString())==familyId;}catch(const std::exception&){/* Legacy incomplete intent gets a new canonical identity. */}
    if(identity.isEmpty()||identity=="builtin:factory"||!sameFamily)set(document,"id",resolvedId);
    measured.documentMilliseconds=juce::Time::getMillisecondCounterHiRes()-documentStart;
    measured.totalMilliseconds=juce::Time::getMillisecondCounterHiRes()-started;
    if(metrics)*metrics=measured;
    return document;
}
}
