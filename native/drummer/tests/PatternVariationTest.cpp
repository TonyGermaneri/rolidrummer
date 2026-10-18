#include <drummer/PatternVariation.h>
#include <drummer/Json.h>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace drummer;
using namespace drummer::json;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
juce::String encode(const juce::var& value){return juce::JSON::toString(value,true);}
bool sameNote(const juce::var& a,const juce::var& b){for(const auto* field:{"beat","duration","note","velocity"})if(get(a,field)!=get(b,field))return false;return true;}
juce::var parent(int variant){
    juce::Array<juce::var> notes;
    for(int i=0;i<16;++i)notes.add(object({{"beat",i*.25+(i==0?.0125:(variant%7)*.001)},
        {"duration",.06+variant*.001},{"note",42},{"velocity",i%4==0?.9:.3+(variant*7+i*3)%35/100.0}}));
    // Full-kit parents must not leak these other drums into the generated part.
    notes.add(object({{"beat",0.0},{"duration",.12},{"note",36},{"velocity",.95}}));
    notes.add(object({{"beat",1.01},{"duration",.11},{"note",38},{"velocity",.9}}));
    return object({{"id","parent-"+juce::String(variant)},{"name","Fill "+juce::String(variant)},{"length",4.0},{"notes",notes},
        {"metadata",object({{"meter",object({{"numerator",4},{"denominator",4}})}})}});
}
}
int main(){try{
    std::vector<juce::var> parents;for(int i=0;i<14;++i)parents.push_back(parent(i));
    PatternVariationOptions settings;settings.parts={42};settings.role="fill";settings.seed="seaboard-session-21";
    const auto children=generatePatternVariations(parents,settings);
    check(children.size()==2,"Compatible fill parents did not produce the requested distinct variations");
    const auto repeat=generatePatternVariations(parents,settings);
    check(encode(juce::Array<juce::var>(children.data(),(int)children.size()))==encode(juce::Array<juce::var>(repeat.data(),(int)repeat.size())),"Same seed and parents changed generated MIDI/provenance");
    auto other=settings;other.seed="another-session";
    const auto changed=generatePatternVariations(parents,other);
    check(changed.size()==2&&get(children[0],"id")!=get(changed[0],"id"),"Changing the seed did not change the generated performance");
    std::set<juce::String> identities;
    for(const auto& child:children){
        check(identities.insert(get(child,"id").toString()).second,"Generator returned duplicate performances");
        const auto generation=get(child,"generation"),sources=get(generation,"parents");
        check((int)get(generation,"version")==patternVariationVersion&&sources.getArray()->size()==2,"Generated MIDI lacks reproducible provenance");
        std::vector<juce::var> originals;
        for(const auto& id:*sources.getArray())for(const auto& source:parents)if(get(source,"id")==id)originals.push_back(source);
        check(originals.size()==2,"Generation refers to an unknown parent");
        const auto notes=get(child,"notes");
        check(notes.getArray()->size()>=12&&notes.getArray()->size()<=20,"Generated density escaped its declared bounds");
        for(const auto& note:*notes.getArray()){
            check((int)get(note,"note")==42,"A full-kit parent leaked another drum through the part gate");
            bool retained=false;
            for(const auto& source:originals){const auto raw=get(source,"notes");for(const auto& original:*raw.getArray())retained=retained||sameNote(note,original);}
            check(retained,"Recombination changed source microtiming, velocity or duration");
        }
        const auto anchorNotes=get(originals[0],"notes");
        for(const auto& anchor:*anchorNotes.getArray())if((int)get(anchor,"note")==42&&(number(anchor,"beat",0)>=3||number(anchor,"beat",0)<.08)){
            bool found=false;for(const auto& note:*notes.getArray())found=found||sameNote(note,anchor);
            check(found,"A fill lost its anchor parent's downbeat or final beat");
        }
        check(encode(juce::JSON::parse(encode(child)))==encode(child),"Saved generated notes do not round-trip independently of the generator");
    }
    auto incompatible=parents[1].clone();set(incompatible,"metadata",object({{"meter",object({{"numerator",7},{"denominator",8}})}}));
    check(generatePatternVariations({parents[0],incompatible},settings).empty(),"Generator recombined incompatible meters");
    auto longParent=parents[1].clone();set(longParent,"length",360.0);
    check(generatePatternVariations({parents[0],longParent},settings).empty(),"A song-length source was stretched into a fill");
    std::vector<juce::var> triplets;
    for(int variant=0;variant<3;++variant){auto clip=parent(variant);juce::Array<juce::var> notes;
        for(int i=0;i<12;++i)notes.add(object({{"beat",i/3.0+variant*.001},{"duration",.06+variant*.003},{"note",42},{"velocity",i%3==0?.9:.4+variant*.1}}));
        set(clip,"notes",notes);triplets.push_back(clip);
    }
    check(generatePatternVariations(triplets,settings).empty(),"Triplet parents were mixed into a straight family");
    other=settings;other.feel="triplet";check(generatePatternVariations(triplets,other).size()==2,"A compatible triplet family lost its generated variations");
    check(generatePatternVariations({parents[0],parents[0]},settings).empty(),"Duplicate parents invented a generated variation");
    other=settings;other.parts={81};check(generatePatternVariations(parents,other).empty(),"A missing part generated unrelated drum events");
    other=settings;other.count=0;check(generatePatternVariations(parents,other).empty(),"Disabled generation changed the candidate set");
    std::cout<<"variations: deterministic seeded fills, distinct notes, parent timing/dynamics, anchors, density, canonical gating, meter/phrase rejection and portable recall passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
