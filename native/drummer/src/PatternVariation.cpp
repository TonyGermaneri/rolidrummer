#include <drummer/PatternVariation.h>
#include <drummer/PatternAnalysis.h>
#include <drummer/Json.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace drummer {
using namespace json;
namespace {
// Explicit integer PRNG: no implementation-dependent std::distribution state.
struct Random {
    uint64_t value;
    uint64_t next() { value += UINT64_C(0x9e3779b97f4a7c15); auto x=value; x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31); }
};
juce::String identity(const juce::var& value) { return juce::SHA256(juce::JSON::toString(value,true).toUTF8()).toHexString(); }
struct Parent { juce::String id;juce::var clip;std::vector<juce::var> notes;double length; };
bool sameAttack(const juce::var& a,const juce::var& b) { return get(a,"note")==get(b,"note")&&std::abs(number(a,"beat",0)-number(b,"beat",0))<1e-9; }
}

std::vector<juce::var> generatePatternVariations(const std::vector<juce::var>& inputs,const PatternVariationOptions& options) {
    if(options.numerator<1||options.numerator>64||(options.denominator!=1&&options.denominator!=2&&options.denominator!=4&&options.denominator!=8&&options.denominator!=16&&options.denominator!=32))throw std::runtime_error("Unsupported variation meter");
    if(options.phraseBars<1||options.phraseBars>64||options.count<0||options.count>4)throw std::runtime_error("Unsupported variation limits");
    if(options.role!="fill"&&options.role!="groove"&&options.role!="sparse")throw std::runtime_error("Unsupported variation role");
    if(options.feel!="straight"&&options.feel!="triplet")throw std::runtime_error("Unsupported variation feel");
    const std::set<int> parts(options.parts.begin(),options.parts.end());
    auto mapping=object({{"id","custom"},{"notes",object({})}});
    auto overrides=get(mapping,"notes"),canonical=get(drumMapDefinitions(),"canonical");
    if(auto* values=canonical.getDynamicObject())for(const auto& value:values->getProperties())if(parts.contains((int)value.value))set(overrides,juce::String((int)value.value).toRawUTF8(),value.name.toString());
    if(parts.empty()||options.count==0)return {};
    for(int part:parts)if(part<0||part>127)throw std::runtime_error("Variation requires valid drum parts");
    const double unit=4.0/options.denominator,bar=options.numerator*unit;
    const auto path=options.role=="fill"?"Generated/Fills/Variation.mid":options.role=="sparse"?"Generated/Sparse/Variation.mid":"Generated/Grooves/Variation.mid";
    std::map<int,std::vector<Parent>> cohorts;
    std::set<juce::String> known;
    for(const auto& input:inputs){
        if(cohorts.size()>64)break;
        const double length=number(input,"length",0),bars=length/bar;
        if(length<=0||length>options.phraseBars*bar+.001)continue;
        const int integerBars=(int)std::llround(bars);
        if(!std::isfinite(bars)||integerBars<1||integerBars>options.phraseBars||std::abs(bars-integerBars)>.001||options.phraseBars%integerBars)continue;
        const auto metadata=get(input,"metadata"),meter=get(metadata,"meter");
        if(number(meter,"numerator",options.numerator)!=options.numerator||number(meter,"denominator",options.denominator)!=options.denominator)continue;
        bool compatible=true;auto signatures=get(metadata,"signatures");
        if(auto* changes=signatures.getArray())for(const auto& change:*changes)if(number(change,"numerator",options.numerator)!=options.numerator||number(change,"denominator",options.denominator)!=options.denominator)compatible=false;
        if(!compatible)continue;
        auto copy=input.clone();juce::Array<juce::var> notes;
        const auto inputNotes=get(input,"notes");
        if(auto* source=inputNotes.getArray())for(const auto& note:*source){
            if(!parts.contains((int)number(note,"note",-1)))continue;
            if(!overrides.hasProperty(juce::String((int)get(note,"note"))))continue;
            const double beat=number(note,"beat",-1),duration=number(note,"duration",0),velocity=number(note,"velocity",0);
            if(!std::isfinite(beat)||!std::isfinite(duration)||!std::isfinite(velocity)||beat<0||beat>=length||duration<=0||velocity<=0||velocity>1)throw std::runtime_error("Invalid variation parent note");
            notes.add(object({{"beat",beat},{"duration",duration},{"note",get(note,"note")},{"velocity",velocity}}));
            if(notes.size()>100000)throw std::runtime_error("Variation parent exceeds note budget");
        }
        if(notes.isEmpty())continue;
        set(copy,"notes",notes);set(copy,"metadata",object({{"meter",object({{"numerator",options.numerator},{"denominator",options.denominator}})}}));
        auto analyzed=analyzePattern(copy,path,mapping);
        const auto measured=get(analyzed,"features");
        const double triplets=number(measured,"tripletFit",0),straight=number(measured,"sixteenthFit",0);
        if((options.feel=="straight"&&triplets>.9&&straight<.8)||(options.feel=="triplet"&&straight>.9&&triplets<.8))continue;
        const auto key=get(analyzed,"id").toString();
        if(!known.insert(key).second)continue;
        const auto normalized=get(analyzed,"notes");
        cohorts[integerBars].push_back({get(input,"id",key).toString(),analyzed,std::vector<juce::var>(normalized.getArray()->begin(),normalized.getArray()->end()),length});
    }
    juce::Array<juce::var> parentIds;for(const auto& [bars,parents]:cohorts)for(const auto& parent:parents)parentIds.add(parent.id);
    const auto entropy=identity(object({{"version",patternVariationVersion},{"seed",options.seed},{"role",options.role},{"feel",options.feel},{"parents",parentIds},{"parts",overrides}}));
    Random random {static_cast<uint64_t>(entropy.substring(0,16).getHexValue64())};
    std::vector<juce::var> results;
    std::vector<const std::vector<Parent>*> eligible;
    for(const auto& [bars,parents]:cohorts)if(parents.size()>1)eligible.push_back(&parents);
    if(eligible.empty())return results;
    for(int attempt=0;attempt<options.count*64&&(int)results.size()<options.count;++attempt){
        const auto& parents=*eligible[random.next()%eligible.size()];
        const size_t first=random.next()%parents.size();
        // Adjacent parents follow the resolver's fixed PCA ordering.
        const size_t second=first+1<parents.size()?first+1:first-1;
        const auto& a=parents[first];const auto& b=parents[second];
        const int cells=(int)std::llround(a.length/unit);
        std::vector<int> selection((size_t)cells);for(auto& cell:selection)cell=(int)(random.next()&1);
        selection.front()=0;if(options.role=="fill")selection.back()=0;
        auto cellOf=[&](const juce::var& note){return std::clamp((int)std::floor(number(note,"beat",0)/unit+.04),0,cells-1);};
        std::vector<juce::var> notes;
        for(int parent=0;parent<2;++parent)for(const auto& note:(parent?b:a).notes)if(selection[(size_t)cellOf(note)]==parent)notes.push_back(note.clone());
        // Anchors come from one parent, preventing mutually conflicting backbeats.
        for(const auto& note:a.notes){
            const double beat=number(note,"beat",0),phase=std::fmod(beat,bar),velocity=number(note,"velocity",0);
            const int drum=(int)get(note,"note");
            const bool downbeat=std::min(phase,bar-phase)<unit*.08;
            const bool backbeat=options.numerator==4&&options.denominator==4&&(drum==38||drum==37)&&(std::abs(phase-1)<.08||std::abs(phase-3)<.08);
            const bool accent=velocity>=.82&&std::abs(beat/unit-std::round(beat/unit))<.08;
            if(!downbeat&&!backbeat&&!accent)continue;
            notes.erase(std::remove_if(notes.begin(),notes.end(),[&](const auto& other){return (int)get(other,"note")==drum&&std::abs(number(other,"beat",0)-beat)<unit*.08;}),notes.end());
            notes.push_back(note.clone());
        }
        const auto minimum=(size_t)std::max(1.0,std::floor(a.notes.size()*.75)),maximum=(size_t)std::ceil(a.notes.size()*1.25);
        std::sort(notes.begin(),notes.end(),[](const auto& left,const auto& right){const double a=number(left,"beat",0),b=number(right,"beat",0);return a!=b?a<b:(int)get(left,"note")<(int)get(right,"note");});
        notes.erase(std::unique(notes.begin(),notes.end(),sameAttack),notes.end());
        if(notes.size()<minimum||notes.size()>maximum)continue;
        juce::Array<juce::var> events;for(const auto& note:notes)events.add(note);
        auto generated=a.clip.clone();set(generated,"notes",events);
        generated=analyzePattern(generated,path,mapping);
        if(!known.insert(get(generated,"id").toString()).second)continue;
        set(generated,"name","Generated "+options.role+" "+juce::String((int)results.size()+1));
        set(generated,"path","");
        juce::Array<juce::var> choices;for(int choice:selection)choices.add(choice);
        set(generated,"generation",object({{"version",patternVariationVersion},{"method","beat-fragment-recombination"},{"seed",options.seed},{"attempt",attempt},
            {"parents",juce::Array<juce::var>{a.id,b.id}},{"fragmentParents",choices},{"unitBeats",unit},{"role",options.role},{"feel",options.feel},
            {"densityBounds",juce::Array<juce::var>{.75,1.25}},{"preserveAnchors",true},{"preserveFinalBeat",options.role=="fill"}}));
        results.push_back(generated);
    }
    return results;
}
}
