#include <drummer/PatternFamilies.h>
#include <drummer/PatchResolver.h>
#include <drummer/State.h>
#include <drummer/TomPatterns.h>
#include <tuple>
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
void tomRolls(const juce::File& assets){
    Fixture fixture;
    for(const auto* role:{"Grooves","Fills"})for(int variation=0;variation<4;++variation){
        auto file=fixture.root.getChildFile("source/Hard Rock/"+juce::String(role)+"/Tom roll "+juce::String(variation)+".mid");file.getParentDirectory().createDirectory();
        juce::MidiMessageSequence track;track.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4,4),0);
        const double step=juce::String(role)=="Fills"?.25:.5;
        for(int i=0;i<int(8/step);++i){const int note=std::array<int,3>{50,47,43}[(size_t)((i+variation)%3)];
            const double beat=i*step;track.addEvent(juce::MidiMessage::noteOn(10,note,(juce::uint8)(70+(variation*7+i)%40)),beat*960);track.addEvent(juce::MidiMessage::noteOff(10,note),beat*960+24);
        }
        track.addEvent(juce::MidiMessage::noteOn(10,36,(juce::uint8)100),0);track.addEvent(juce::MidiMessage::noteOff(10,36),24);
        track.addEvent(juce::MidiMessage::endOfTrack(),7680);juce::MidiFile midi;midi.setTicksPerQuarterNote(960);midi.addTrack(track);juce::FileOutputStream output(file);check(midi.writeTo(output),"Cannot write tom roll fixture");
    }
    PatternLibrary library(fixture.root.getChildFile("library.sqlite"));library.importSource(fixture.root.getChildFile("source"));NeuralIndex index(library.file(),assets);index.build();
    auto input=defaultDocument();set(input,"clips",juce::Array<juce::var>{});set(input,"launchGrid",0.);set(input,"switchGrid",0.);
    juce::Array<juce::var> keys;for(const auto [note,part]:{std::pair{49,50},std::pair{50,47},std::pair{51,43},std::pair{52,36}})keys.add(object({{"note",note},{"parts",juce::Array<juce::var>{part}},{"sound",object({{"path","factory:snare"}})}}));set(input,"keys",keys);
    for(bool automatic:{false,true})for(int generated:{0,2}){
        const auto options=object({{"query","Heavy Rock"},{"autoMode",automatic},{"numerator",4},{"denominator",4},{"feel","straight"},{"variationCount",generated},{"seed","shared-roll"}});
        const auto resolved=resolveNeuralPatch(library,index,input,options);const auto reference=get(key(resolved,49),"zones");
        for(int note:{49,50,51}){
            const auto tom=key(resolved,note);check(juce::JSON::toString(get(tom,"zones"),true)==juce::JSON::toString(reference,true),"Tom zones chose different source performances");
            check(!(bool)get(get(tom,"patternIntent"),"enabled",true),"Independent tom expression can split a roll");
            for(const auto& zone:*get(tom,"zones").getArray())check(get(zone,"candidates").isVoid(),"A tom zone retained independently selectable candidates");
        }
        check(juce::JSON::toString(resolved,true)==juce::JSON::toString(resolveNeuralPatch(library,index,input,options),true),"Tom group is not seed deterministic");
        const auto children=number(get(resolved,"neural"),"generatedCount",0);check(generated?(!automatic||children<=2):children==0,"Tom grouping changed the generation budget or Patterns Only policy");
        auto runtime=compileDocument(resolved);const auto& config=runtime->config;
        for(int zone=0;zone<3;++zone)check(config.keys[49].zones[(size_t)zone].clip==config.keys[50].zones[(size_t)zone].clip&&config.keys[49].zones[(size_t)zone].clip==config.keys[51].zones[(size_t)zone].clip,"Compiled toms lost their shared pattern bus");
        // Different fingers/pressure still extract the exact three-note roll
        // from one performance, entering at song beat 5.2 rather than beat zero.
        using Hit=std::tuple<int,int,int>;std::vector<Hit> expected,actual;
        const auto& clip=config.clips[(size_t)config.keys[49].zones[2].clip];
        for(int cycle=0;cycle<4;++cycle)for(const auto& note:clip.notes){const double beat=cycle*clip.length+clip.origin+note.beat;
            const int frame=(int)std::llround((beat-5.2)*24000);if(frame<0||frame>=192000||!isTomNote(note.note))continue;
            const int destination=note.note==50?49:note.note==47?50:51;expected.emplace_back(frame,note.note,destination);
        }
        std::sort(expected.begin(),expected.end());Scheduler scheduler;
        struct Capture{std::vector<Hit>* hits;int at;static void take(void* context,const Event& event){auto& capture=*static_cast<Capture*>(context);if(event.type==Event::hit)capture.hits->emplace_back(capture.at+event.offset,event.drumNote,event.key);}} capture{&actual,0};
        for(int at=0;at<192000;at+=257){capture.at=at;Midi messages[9]{};int count=0;
            if(at==0)for(int i=0;i<3;++i){messages[count++]={0,(uint8_t)(0xb1+i),74,127};messages[count++]={0,(uint8_t)(0xd1+i),(uint8_t)(i*60),0};messages[count++]={0,(uint8_t)(0x91+i),(uint8_t)(49+i),100};}
            scheduler.process(config,{5.2+at/24000.,120,48000,true},std::min(257,192000-at),messages,count,Capture::take,&capture);check(!scheduler.overflow,"Shared tom roll overflowed");
        }
        std::sort(actual.begin(),actual.end());check(!expected.empty()&&actual==expected,"Tom gates lost, duplicated or restarted notes in the shared roll");
    }
    // Registered families use a batched part pool. It must group the tom mask
    // the same way as broad semantic search and preserve CPU/GPU determinism.
    PatternFamilies families(library.file());families.registerFamily(object({{"query","Heavy Rock"},{"numerator",4},{"denominator",4},{"feel","straight"}}));families.reconcile(index);
    auto options=object({{"query","Heavy Rock"},{"autoMode",true},{"seed","registered-toms"},{"variationCount",0}});
    const auto registered=resolveNeuralPatch(library,index,input,options);const auto zones=get(key(registered,49),"zones");
    for(int note:{50,51})check(juce::JSON::toString(get(key(registered,note),"zones"),true)==juce::JSON::toString(zones,true),"Registered family split its tom pool by drum");
    auto cpu=options.clone();set(cpu,"cpu",true);check(juce::JSON::toString(registered,true)==juce::JSON::toString(resolveNeuralPatch(library,index,input,cpu),true),"CPU/GPU disagree on shared tom patterns");
    auto locked=registered.clone();auto lockedKeys=get(locked,"keys");for(auto& entry:*lockedKeys.getArray())if((int)get(entry,"note")==50)set(entry,"neuralLock",true);
    auto manual=options.clone();set(manual,"autoMode",false);set(manual,"seed","another-roll");const auto retained=resolveNeuralPatch(library,index,locked,manual);
    for(int note:{49,50,51})check(juce::JSON::toString(get(key(retained,note),"zones"),true)==juce::JSON::toString(zones,true),"A locked tom let another tom reroll independently");
    const auto unlocked=resolveNeuralPatch(library,index,locked,options);for(int note:{49,50,51})check(!(bool)get(key(unlocked,note),"neuralLock",true),"Auto failed to unlock every grouped tom");
    // Existing saved patches converge by pitch, independently of key array order.
    auto old=defaultDocument();auto oldKeys=get(old,"keys");for(auto& entry:*oldKeys.getArray())if(isTomKey(entry)){auto zones=get(entry,"zones");set(zones[2],"clipId",(int)get(entry,"note")==52?"factory-0":"factory-2");}
    auto migrated=compileDocument(old);check(migrated->config.keys[51].zones[2].clip==migrated->config.keys[52].zones[2].clip&&migrated->config.keys[51].zones[2].clip==migrated->config.keys[53].zones[2].clip,"Saved tom matrices remained independent");
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
    tomRolls(juce::File(juce::String::fromUTF8(argv[1])));
    std::cout<<"part coverage: rare groove, fourteen fills, seeded children, registered/unregistered selection and independent compiled gates passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
