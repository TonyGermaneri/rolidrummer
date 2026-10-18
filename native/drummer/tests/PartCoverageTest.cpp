#include <drummer/PatternFamilies.h>
#include <drummer/PatchResolver.h>
#include <drummer/State.h>
#include <drummer/Json.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace drummer;
using namespace drummer::json;
namespace {
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
struct Fixture {
    juce::File root=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("rd-part-coverage","");
    Fixture(){check(root.createDirectory().wasOk(),"Cannot create part coverage fixture");}
    ~Fixture(){root.deleteRecursively();}
    void write(const juce::String& role,int count){
        const bool rare=role=="Grooves"?count==2:count>=2&&count<=15;
        auto file=root.getChildFile("source/Hard Rock/"+role+"/"+juce::String(count)+".mid");file.getParentDirectory().createDirectory();
        juce::MidiMessageSequence events;events.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4,4),0);
        for(int i=0;i<count;++i){
            const int note=rare&&i>0?63:36;
            const double beat=rare&&role=="Fills"?i*.25:i*8./count;
            events.addEvent(juce::MidiMessage::noteOn(10,note,(juce::uint8)(60+(count*5+i*7)%65)),beat*960);
            events.addEvent(juce::MidiMessage::noteOff(10,note),beat*960+10);
        }
        events.addEvent(juce::MidiMessage::endOfTrack(),7680);
        juce::MidiFile midi;midi.setTicksPerQuarterNote(960);midi.addTrack(events);juce::FileOutputStream stream(file);check(midi.writeTo(stream),"Cannot write coverage MIDI");
    }
};
const juce::var key(const juce::var& document,int note){for(const auto& row:*get(document,"keys").getArray())if((int)get(row,"note")==note)return row;throw std::runtime_error("Missing resolved key");}
void verify(const juce::var& document){
    const auto zones=get(key(document,72),"zones");
    check(zones.isArray()&&zones.size()==3&&get(zones[0],"clipId").toString().isNotEmpty()&&get(zones[1],"clipId").toString().isNotEmpty(),"Whole-kit sampling hid the only compatible rare-part groove");
    const auto fills=get(zones[2],"candidates");int originals=0,generated=0;
    for(const auto& row:*fills.getArray())((bool)get(row,"generated",false)?generated:originals)++;
    check(originals==14&&generated==2,"A rare part did not retain all fourteen fills and two seeded children");
    const auto absent=get(key(document,69),"zones");for(const auto& zone:*absent.getArray())check(get(zone,"clipId").toString().isEmpty(),"Missing percussion adopted another part's pattern");
    auto runtime=compileDocument(document);Scheduler scheduler;
    struct Counts{int conga=0,kick=0;bool wrong=false;} counts;
    auto sink=+[](void* context,const Event& event){if(event.type!=Event::hit)return;auto& count=*static_cast<Counts*>(context);if(event.key==72){++count.conga;count.wrong=count.wrong||event.drumNote!=63;}else if(event.key==48){++count.kick;count.wrong=count.wrong||event.drumNote!=36;}else count.wrong=true;};
    for(int frame=0;frame<8*48000;frame+=256){
        Midi input[6]{};int size=0;
        if(frame==0){input[size++]={0,0x91,72,100};input[size++]={0,0x92,69,100};input[size++]={0,0x93,48,100};}
        if(frame==2*48000)input[size++]={0,0xb1,74,127};
        scheduler.process(runtime->config,{frame/24000.,120,48000,true},256,input,size,sink,&counts);
        check(!scheduler.overflow,"Rare-part playback exceeded scheduling bounds");
    }
    check(counts.conga>0&&counts.kick>0&&!counts.wrong,"Compiled rare-part candidates bypassed independent drum gates");
}
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
    check(argc==2,"Expected pinned neural assets");Fixture fixture;
    for(const auto* role:{"Grooves","Fills"})for(int count=1;count<=260;++count)fixture.write(role,count);
    const auto alias=fixture.root.getChildFile("source/Hard Rock/Swing/Fills/4.mid");alias.getParentDirectory().createDirectory();
    check(fixture.root.getChildFile("source/Hard Rock/Fills/4.mid").copyFileTo(alias),"Cannot create alternate-feel export alias");
    PatternLibrary library(fixture.root.getChildFile("library.sqlite"));library.importSource(fixture.root.getChildFile("source"));NeuralIndex index(library.file(),juce::File(juce::String::fromUTF8(argv[1])));index.build();
    auto search=object({{"query","Heavy Rock"},{"parts",juce::Array<juce::var>{63}},{"numerator",4},{"denominator",4},{"feel","straight"},{"phraseBars",1},{"limit",200},{"role","fill"}});
    check(get(index.search(search),"rows").size()==0,"Incompatible phrase lengths consumed part candidate slots");
    set(search,"phraseBars",2);const auto straight=index.search(search);
    check(get(straight,"rows").size()==14,"A part-conditioned fill query lost compatible original variations");
    set(search,"feel","triplet");const auto swing=index.search(search);
    check(get(swing,"rows").size()==1&&get(get(swing,"rows")[0],"path").toString().contains("Swing"),"Feel filtering failed to retain the compatible export alias before sampling");
    auto kickSearch=search.clone();set(kickSearch,"parts",juce::Array<juce::var>{36});set(kickSearch,"role","groove");set(kickSearch,"feel","straight");
    const auto kickRows=get(index.search(kickSearch),"rows");bool highest=false;for(const auto& row:*kickRows.getArray())highest=highest||number(get(get(get(row,"features"),"parts"),"kick"),"density",0)==32.5;
    check(kickRows.size()<=64&&highest,"Part sampling exceeded its bound or lost the highest activity endpoint");
    set(search,"parts",juce::Array<juce::var>{63.5});bool rejected=false;try{index.search(search);}catch(const std::exception&){rejected=true;}check(rejected,"A fractional part mask silently chose another MIDI note");
    auto document=defaultDocument();set(document,"clips",juce::Array<juce::var>{});juce::Array<juce::var> keys;
    for(const auto [note,part]:{std::pair{48,36},std::pair{72,63},std::pair{69,56}})keys.add(object({{"note",note},{"parts",juce::Array<juce::var>{part}},{"sound",object({{"path","factory:snare"}})}}));
    set(document,"keys",keys);const auto options=object({{"query","Heavy Rock"},{"numerator",4},{"denominator",4},{"feel","straight"},{"variationCount",2},{"seed","rare-fourteen"}});
    const auto unregistered=resolveNeuralPatch(library,index,document,options);verify(unregistered);
    PatternFamilies families(library.file());families.registerFamily(object({{"query","Heavy Rock"},{"numerator",4},{"denominator",4},{"feel","straight"}}));families.reconcile(index);
    // Batched eligibility must retain exactly the same aliases, density
    // endpoints, role budgets and ordering as individual part-conditioned reads.
    const auto indexId=get(index.status(),"readyIndexId").toString();const auto affinities=index.affinities("Heavy Rock",indexId);
    juce::Array<juce::var> masks;for(const auto& notes:std::vector<juce::Array<juce::var>>{{36},{63},{56},{36,63},{99},{},{63}})masks.add(notes);
    for(int phrase:{1,2,8})for(bool materialize:{false,true}){
        const auto common=object({{"query","Heavy Rock"},{"indexId",indexId},{"numerator",4},{"denominator",4},{"feel","straight"},{"phraseBars",phrase}});
        auto all=common.clone();set(all,"partMasks",masks);set(all,"materializeParts",materialize);const auto pools=get(families.selection(index,all,&affinities),"pools");check(pools.size()==masks.size(),"Batched selection lost a requested mask");
        for(int i=0;i<masks.size();++i){auto single=common.clone();set(single,"parts",masks[i]);
            check(juce::JSON::toString(get(pools[i],"rows"),true)==juce::JSON::toString(get(families.selection(index,single,&affinities),"rows"),true),"Batched family selection differs from an individual part query");
        }
    }
    auto badBatch=object({{"query","Heavy Rock"},{"indexId",indexId},{"numerator",4},{"denominator",4},{"feel","straight"},{"partMasks",juce::Array<juce::var>{juce::Array<juce::var>{63.5}}}});
    rejected=false;try{families.selection(index,badBatch,&affinities);}catch(const std::exception&){rejected=true;}check(rejected,"Batched family selection accepted a fractional part");
    const auto registered=resolveNeuralPatch(library,index,document,options);verify(registered);
    auto cpu=options.clone();set(cpu,"cpu",true);check(juce::JSON::toString(registered,true)==juce::JSON::toString(resolveNeuralPatch(library,index,document,cpu),true),"CPU and Metal selected different part-conditioned candidates");
    check(juce::JSON::toString(registered,true)==juce::JSON::toString(resolveNeuralPatch(library,index,document,options),true),"Part-conditioned family resolution changed on repeat");
    std::cout<<"part coverage: rare groove, fourteen fills, seeded children, registered/unregistered selection and independent compiled gates passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
