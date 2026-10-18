#include "PluginProcessor.h"
#include <drummer/Json.h>
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <sys/resource.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace realtime {
thread_local bool watching=false;
thread_local size_t allocations=0,deletions=0;
}
void* operator new(std::size_t size){if(realtime::watching)++realtime::allocations;if(auto* p=std::malloc(std::max(size,size_t(1))))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept{if(p&&realtime::watching)++realtime::deletions;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
void* operator new(std::size_t size,std::align_val_t alignment){if(realtime::watching)++realtime::allocations;void* p=nullptr;if(posix_memalign(&p,size_t(alignment),std::max(size,size_t(1)))==0)return p;throw std::bad_alloc();}
void* operator new[](std::size_t size,std::align_val_t alignment){return ::operator new(size,alignment);}
void operator delete(void* p,std::align_val_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::align_val_t) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept{::operator delete(p);}

namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Head:juce::AudioPlayHead {
    double beat=0,bpm=120;bool playing=true,reportTempo=true;
    juce::Optional<PositionInfo> getPosition()const override{PositionInfo p;if(reportTempo)p.setBpm(bpm);p.setPpqPosition(beat);p.setIsPlaying(playing);return p;}
};
void transport(Head& head,int frame,double rate){
    // Repeat a 24-second host script. During stop, the host freezes its PPQ;
    // the instrument must continue its internal performance clock and rejoin.
    const double second=std::fmod(frame/rate,24.);
    head.playing=second<9||second>=13;head.reportTempo=second<7||second>=8;
    if(second<4){head.bpm=120;head.beat=second*2;}
    else if(second<9){head.bpm=87;head.beat=8+(second-4)*87/60;}
    else if(second<13){head.bpm=87;head.beat=15.25;}
    else if(second<17){head.bpm=172;head.beat=15.25+(second-13)*172/60;}
    else if(second<21){head.bpm=95;head.beat=4.375+(second-17)*95/60;}
    else{head.bpm=95;head.beat=2+(second-std::floor(second))*95/60;}
}
juce::var document(bool refreshed,bool effects){
    using namespace drummer::json;
    auto doc=drummer::defaultDocument();auto first=(*get(doc,"keys").getArray())[0];
    juce::Array<juce::var> clips,keys;
    for(int i=0;i<48;++i){juce::Array<juce::var> notes;
        for(int n=0;n<16;++n)notes.add(object({{"beat",n*.125+i*.00002},{"duration",.35},{"note",n%4==3?42:36},{"velocity",.35+i*.006+(refreshed?.08:0)}}));
        clips.add(object({{"id","intent-"+juce::String(i)},{"length",2},{"notes",notes}}));
    }
    for(int pitch=48;pitch<64;++pitch){auto key=first.clone();set(key,"note",pitch);set(key,"parts",juce::Array<juce::var>{36});
        auto sound=get(key,"sound"),settings=get(sound,"s");set(settings,"volume",.05);set(settings,"mode","hold");set(settings,"release",.08);
        set(settings,"delayMix",effects?.15:0);set(settings,"reverbMix",effects?.15:0);
        if(!effects){auto routes=get(key,"routes");for(int i=routes.size()-1;i>=0;--i)if(get((*routes.getArray())[i],"target")=="reverbMix")routes.getArray()->remove(i);}
        juce::Array<juce::var> zones;
        for(int region=0;region<3;++region){juce::Array<juce::var> candidates;
            for(int i=0;i<16;++i)candidates.add(object({{"clipId","intent-"+juce::String(region*16+i)},{"position",juce::Array<juce::var>{i/15.,i/15.,(15-i)/15.,i/15.}}}));
            zones.add(object({{"upper",region==0?.25:region==1?.75:1.},{"clipId","intent-"+juce::String(region*16)},{"candidates",candidates}}));
        }
        set(key,"zones",zones);set(key,"patternIntent",object({{"enabled",true},{"smoothingMs",35},{"minimumDwell",.125},{"transitionPenalty",.025}}));keys.add(key);
    }
    set(doc,"keys",keys);set(doc,"clips",clips);set(doc,"launchGrid",0);set(doc,"switchGrid",.125);return doc;
}
double residentMiB(){
#if defined(__APPLE__)
    mach_task_basic_info_data_t info{};mach_msg_type_number_t size=MACH_TASK_BASIC_INFO_COUNT;
    if(task_info(mach_task_self(),MACH_TASK_BASIC_INFO,reinterpret_cast<task_info_t>(&info),&size)==KERN_SUCCESS)return double(info.resident_size)/1048576;
#endif
    return 0;
}
struct Run {
    std::vector<float> samples;
    double energy=0,total=0,p99=0,maximum=0,measuredAudio=0,p99Load=0,maximumLoad=0;
    double allMaximumLoad=0,startupMaximumLoad=0,drainMaximumLoad=0,setupSeconds=0,setupRss=0,maximumRss=0,endRss=0;
    int contacts=0,voices=0,buses=0,misses=0,callbacks=0,maximumFrame=0,allCallbacks=0,allMisses=0,startupMisses=0,drainMisses=0,refreshes=0,adoptions=0;
};
Run run(double rate,int block,bool effects,bool collect,int instances=1,double duration=2,bool variedTransport=false){
    check(instances>=1&&instances<=8&&duration>=2&&duration<=600&&(!collect||duration*instances<=240),"Invalid intent workload bounds");
    const auto setup=std::chrono::steady_clock::now();
    std::vector<std::unique_ptr<DrummerProcessor>> players;
    std::vector<std::unique_ptr<drummer::Runtime>> prepared;
    std::vector<Head> heads(static_cast<size_t>(instances));
    std::vector<juce::AudioBuffer<float>> audio(size_t(instances),juce::AudioBuffer<float>{});
    std::vector<juce::MidiBuffer> midi(size_t(instances),juce::MidiBuffer{});
    for(int i=0;i<instances;++i){
        players.push_back(std::make_unique<DrummerProcessor>());auto& player=*players.back();
        prepared.push_back(drummer::compileDocument(document(true,effects)));
        player.apply(drummer::compileDocument(document(false,effects)));player.prepareToPlay(rate,2048);player.setPlayHead(&heads[size_t(i)]);
        audio[size_t(i)].setSize(2,2048);midi[size_t(i)].ensureSize(32768);
        check(player.library&&player.library==players.front()->library,"Instances did not share their catalog service");
    }
    Run result;result.setupSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-setup).count();
    result.setupRss=result.maximumRss=residentMiB();if(collect)result.samples.resize(size_t(rate*duration*2*instances));
    struct Input{int at;juce::MidiMessage message;};std::vector<Input> events;
    events.push_back({0,juce::MidiMessage::controllerEvent(1,64,127)});
    for(int i=0;i<128;++i){const int channel=2+i%15,pitch=48+i%15;
        events.push_back({i*3,juce::MidiMessage::controllerEvent(channel,74,i%128)});
        events.push_back({i*3,juce::MidiMessage::channelPressureChange(channel,(i*7)%128)});
        events.push_back({i*3,juce::MidiMessage::noteOn(channel,pitch,(juce::uint8)(70+i%50))});
        events.push_back({i*3+1,juce::MidiMessage::noteOff(channel,pitch,(juce::uint8)(i%128))});
    }
    for(int ch=2;ch<=16;++ch){events.push_back({512,juce::MidiMessage::noteOn(ch,46+ch,(juce::uint8)100)});events.push_back({1200+ch,juce::MidiMessage::noteOn(ch,46+ch,(juce::uint8)90)});}
    const int request=collect?int(rate*.125):int(std::ceil(rate*.125/block))*block;
    const int pedalUp=int(rate*(duration-.75)),release=int(rate*(duration-.5)),total=int(rate*duration);
    for(int frame=600;frame<release;frame+=137){const int ch=2+(frame/137)%15;
        events.push_back({frame,juce::MidiMessage::channelPressureChange(ch,(frame/137)%128)});
        events.push_back({frame,juce::MidiMessage::controllerEvent(ch,74,(frame/311)%128)});
        events.push_back({frame,juce::MidiMessage::pitchWheel(ch,(frame*17)%16384)});
    }
    events.push_back({pedalUp,juce::MidiMessage::controllerEvent(1,64,0)});
    for(int ch=2;ch<=16;++ch)events.push_back({release,juce::MidiMessage::noteOff(ch,46+ch,(juce::uint8)(40+ch*4))});
    std::stable_sort(events.begin(),events.end(),[](const Input& a,const Input& b){return a.at<b.at;});
    DrummerProcessor::FingerSnapshot fingers;std::vector<double> times,loads;times.reserve(size_t(total/block+16));loads.reserve(times.capacity());size_t cursor=0;
    std::vector<double> stoppedEnergy(size_t(std::ceil(duration)),0);
    int nextRequest=request,boundary=int(rate),memoryAt=0;std::vector<bool> awaiting(size_t(instances),false);
    for(int at=0;at<total;){
        if(at==nextRequest&&at<pedalUp){
            for(int i=0;i<instances;++i){check(!awaiting[size_t(i)],"A refresh was stranded before the next prepared update");if(result.refreshes)prepared[size_t(i)]=drummer::compileDocument(document(result.refreshes%2==0,effects));players[size_t(i)]->apply(std::move(prepared[size_t(i)]),{},2);awaiting[size_t(i)]=true;}
            ++result.refreshes;boundary=int(std::ceil(at/rate-1e-10)*rate);nextRequest+=int(rate*2);
        }
        int count=std::min(block,total-at);for(int point:{1024,nextRequest})if(point>at&&point<pedalUp)count=std::min(count,point-at);
        // Host state changes have an exact absolute sample in every buffer
        // partition; MIDI and refreshes retain their own sample positions.
        if(variedTransport){const int second=int(std::floor(at/rate));count=std::min(count,int((second+1)*rate)-at);}
        for(int i=0;i<instances;++i){audio[size_t(i)].setSize(2,count,false,false,true);midi[size_t(i)].clear();if(variedTransport)transport(heads[size_t(i)],at,rate);else heads[size_t(i)].beat=at*2/rate;}
        while(cursor<events.size()&&events[cursor].at<at+count){for(auto& input:midi)input.addEvent(events[cursor].message,events[cursor].at-at);++cursor;}
        const auto begin=std::chrono::steady_clock::now();realtime::watching=true;
        // Serial processing models the combined deadline of instances assigned
        // to one host audio lane; the isolation check below renders concurrently.
        for(int i=0;i<instances;++i)players[size_t(i)]->processBlock(audio[size_t(i)],midi[size_t(i)]);
        realtime::watching=false;
        const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        check(!realtime::allocations&&!realtime::deletions,"Combined processor intent workload allocated or deleted memory on audio");
        const double load=seconds*rate/count;++result.allCallbacks;result.allMisses+=load>1;result.allMaximumLoad=std::max(result.allMaximumLoad,load);
        if(at<request){result.startupMisses+=load>1;result.startupMaximumLoad=std::max(result.startupMaximumLoad,load);}
        if(at>=pedalUp){result.drainMisses+=load>1;result.drainMaximumLoad=std::max(result.drainMaximumLoad,load);}
        if(at>=request&&at<pedalUp){times.push_back(seconds);loads.push_back(load);result.total+=seconds;result.measuredAudio+=count/rate;
            ++result.callbacks;result.misses+=load>1;if(seconds>result.maximum){result.maximum=seconds;result.maximumFrame=at;}}
        int contacts=0,voices=0,buses=0;
        for(int p=0;p<instances;++p){auto& player=*players[size_t(p)];
            check(!player.overflow.load(),"Combined processor intent workload overflowed");
            if(!variedTransport&&at>=request)check(player.activationPending()==(at+count<=boundary),"Full-contact refresh missed its musical boundary");
            if(awaiting[size_t(p)]&&!player.activationPending()){awaiting[size_t(p)]=false;++result.adoptions;}
            check(player.readFingers(fingers),"Could not read editor-free contact telemetry");contacts+=fingers.count;
            if(at==1024)check(fingers.count==143,"Processor fixture failed to occupy fifteen MPE members plus 128 sustained owners");
            if(at>=1024&&at+count<=pedalUp)check(fingers.count==143,"A tempo/transport change lost a held or sustained owner");
            voices+=player.engine.activeVoices();buses+=player.clipEvaluations.load();
            for(int i=0;i<count;++i)for(int ch=0;ch<2;++ch){const float value=audio[size_t(p)].getSample(ch,i);check(std::isfinite(value),"Combined processor intent audio is nonfinite");result.energy+=value*value;if(variedTransport&&!heads[size_t(p)].playing)stoppedEnergy[size_t((at+i)/int(rate))]+=value*value;if(collect)result.samples[(size_t(at+i)*instances+p)*2+ch]=value;}
        }
        result.contacts=std::max(result.contacts,contacts);result.voices=std::max(result.voices,voices);result.buses=std::max(result.buses,buses);
        if(at>=memoryAt){result.maximumRss=std::max(result.maximumRss,residentMiB());memoryAt+=int(rate);juce::Timer::callPendingTimersSynchronously();}
        at+=count;
    }
    for(const auto& player:players){check(player->readFingers(fingers),"Could not read final contact telemetry");check(fingers.count==0&&!player->activationPending(),"Processor left a held contact or queued refresh after release");}
    check(result.contacts==143*instances&&result.voices==64*instances&&result.energy>.01,"Processor workload did not reach intended contact/polyphony load");
    if(variedTransport)for(size_t second=0;second<stoppedEnergy.size();++second)if(second%24>=9&&second%24<13)check(stoppedEnergy[second]>.00001,"Stopped host silenced an internally running performance");
    check(result.adoptions==result.refreshes*instances,"Some prepared refreshes never became active");result.endRss=residentMiB();
    std::sort(times.begin(),times.end());std::sort(loads.begin(),loads.end());result.p99=times[size_t(times.size()*.99)];result.p99Load=loads[size_t(loads.size()*.99)];result.maximumLoad=loads.back();return result;
}

std::vector<float> isolated(bool editOther){
    constexpr int frames=96000,block=257;
    DrummerProcessor first,second;auto changed=drummer::compileDocument(document(true,true));
    first.apply(drummer::compileDocument(document(false,true)));second.apply(drummer::compileDocument(document(false,true)));
    first.prepareToPlay(48000,block);second.prepareToPlay(48000,block);Head a,b;first.setPlayHead(&a);second.setPlayHead(&b);
    check(first.library&&first.library==second.library,"Concurrent instances did not share the catalog service");
    juce::AudioBuffer<float> firstAudio(2,block),secondAudio(2,block);juce::MidiBuffer firstMidi,secondMidi;firstMidi.ensureSize(4096);secondMidi.ensureSize(4096);
    std::barrier gate(2);std::atomic<bool> failed=false;std::vector<float> result;result.reserve(frames*2);
    auto render=[&](DrummerProcessor& player,juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi){
        realtime::watching=true;player.processBlock(audio,midi);realtime::watching=false;
        if(realtime::allocations||realtime::deletions||player.overflow.load())failed=true;
        for(int ch=0;ch<2;++ch)for(int i=0;i<audio.getNumSamples();++i)if(!std::isfinite(audio.getSample(ch,i)))failed=true;
    };
    std::thread other([&]{for(int at=0;at<frames;at+=block){const int count=std::min(block,frames-at);a.beat=at/24000.;firstAudio.setSize(2,count,false,false,true);firstMidi.clear();
        if(editOther){if(!at)for(int ch=2;ch<=16;++ch)firstMidi.addEvent(juce::MidiMessage::noteOn(ch,46+ch,(juce::uint8)100),0);
            firstMidi.addEvent(juce::MidiMessage::channelPressureChange(2,(at/137)%128),0);firstMidi.addEvent(juce::MidiMessage::controllerEvent(2,74,(at/311)%128),0);
            if(at/block==12)first.apply(std::move(changed));
            if(at/block==30)firstMidi.addEvent(juce::MidiMessage::controllerEvent(1,123,0),0);
        }
        gate.arrive_and_wait();render(first,firstAudio,firstMidi);gate.arrive_and_wait();
    }});
    for(int at=0;at<frames;at+=block){const int count=std::min(block,frames-at);b.beat=at/24000.;secondAudio.setSize(2,count,false,false,true);secondMidi.clear();
        if(!at)secondMidi.addEvent(juce::MidiMessage::noteOn(2,48,(juce::uint8)100),0);
        secondMidi.addEvent(juce::MidiMessage::controllerEvent(2,74,64),0);
        if(at<=72000&&at+count>72000)secondMidi.addEvent(juce::MidiMessage::noteOff(2,48,(juce::uint8)90),72000-at);
        gate.arrive_and_wait();render(second,secondAudio,secondMidi);gate.arrive_and_wait();
        for(int i=0;i<count;++i)for(int ch=0;ch<2;++ch)result.push_back(secondAudio.getSample(ch,i));
    }
    other.join();check(!failed,"Concurrent instance rendering allocated, overflowed or produced nonfinite audio");return result;
}
void printHeader(){
    std::cout<<"architecture,translated,rate,block,profile,physical,sustained,max_voices,max_buses,measured_audio_seconds,callback_seconds,realtime_percent,p99_callback_ms,max_callback_ms,p99_deadline_percent,max_deadline_percent,deadline_misses,callbacks,worst_frame,peak_rss_mib,instances,duration_seconds,all_callbacks,all_deadline_misses,all_max_deadline_percent,startup_deadline_misses,startup_max_deadline_percent,drain_deadline_misses,drain_max_deadline_percent,setup_seconds,setup_rss_mib,max_sampled_rss_mib,end_rss_mib,refreshes\n";
}
void printRun(double rate,int block,bool effects,int instances,double duration){
    const char* architecture=
#if defined(__aarch64__) || defined(__arm64__)
        "arm64";
#elif defined(__x86_64__)
        "x86_64";
#else
        "other";
#endif
    int translated=0;
#if defined(__APPLE__)
    size_t length=sizeof(translated);sysctlbyname("sysctl.proc_translated",&translated,&length,nullptr,0);
#endif
    const auto r=run(rate,block,effects,false,instances,duration);rusage usage{};getrusage(RUSAGE_SELF,&usage);
    std::cout<<architecture<<','<<translated<<','<<int(rate)<<','<<block<<','<<(effects?"intent+effects":"intent")<<','<<15*instances<<','<<128*instances<<','<<r.voices<<','<<r.buses<<','<<r.measuredAudio<<','<<r.total<<','<<r.total/r.measuredAudio*100<<','<<r.p99*1000<<','<<r.maximum*1000<<','<<r.p99Load*100<<','<<r.maximumLoad*100<<','<<r.misses<<','<<r.callbacks<<','<<r.maximumFrame<<','<<double(usage.ru_maxrss)/1048576<<','<<instances<<','<<duration<<','<<r.allCallbacks<<','<<r.allMisses<<','<<r.allMaximumLoad*100<<','<<r.startupMisses<<','<<r.startupMaximumLoad*100<<','<<r.drainMisses<<','<<r.drainMaximumLoad*100<<','<<r.setupSeconds<<','<<r.setupRss<<','<<r.maximumRss<<','<<r.endRss<<','<<r.refreshes<<std::endl;
}
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
    if(argc>=2&&argc<=6&&juce::String(argv[1])=="--transport-soak"){
        using namespace drummer::json;
        const double seconds=argc>2?juce::String(argv[2]).getDoubleValue():90;const int instances=argc>3?juce::String(argv[3]).getIntValue():2;
        check(seconds>=24&&seconds<=120&&seconds==std::floor(seconds)&&instances>=1&&instances<=2,"Transport soak requires 24–120 whole seconds and one or two instances");
        check(argc<6||juce::String(argv[5])=="dry","The optional transport profile must be dry");const bool effects=argc<6;
        auto capture=[&](const Run& value,int block){if(argc<5)return;const juce::File directory(juce::String::fromUTF8(argv[4]));check(directory.createDirectory().wasOk(),"Cannot create transport capture directory");check(directory.getChildFile(juce::String(block)+".f32").replaceWithData(value.samples.data(),value.samples.size()*sizeof(float)),"Cannot write transport audio capture");};
        const auto reference=run(48000,64,effects,true,instances,seconds,true);capture(reference,64);juce::Array<juce::var> comparisons;bool matches=true;std::cerr<<"transport reference rendered\n";
        for(int block:{257,2048}){const auto other=run(48000,block,effects,true,instances,seconds,true);check(reference.samples.size()==other.samples.size(),"Transport render lengths differ");
            capture(other,block);double error=0;size_t worst=0,first=reference.samples.size(),firstLarge=first,large=0;
            for(size_t i=0;i<reference.samples.size();++i){const double difference=std::abs(reference.samples[i]-other.samples[i]);if(difference>0)first=std::min(first,i);if(difference>=.0001){firstLarge=std::min(firstLarge,i);++large;}if(difference>error){error=difference;worst=i;}}
            if(error>=.0001)std::cerr<<"transport block="<<block<<" first different frame="<<first/(2*instances)<<" first above tolerance="<<firstLarge/(2*instances)<<" worst frame="<<worst/(2*instances)<<" difference="<<error<<'\n';
            matches=matches&&error<.0001;
            comparisons.add(object({{"block",block},{"maximumAudioDifference",error},{"firstDifferentFrame",(juce::int64)(first/(2*instances))},{"firstAboveToleranceFrame",large?juce::var((juce::int64)(firstLarge/(2*instances))):juce::var()},{"samplesAboveTolerance",(juce::int64)large},{"worstFrame",(juce::int64)(worst/(2*instances))},{"physical",15*instances},{"sustained",128*instances},{"maximumVoices",other.voices},{"preparedRefreshesPerInstance",other.refreshes},{"adoptedRefreshes",other.adoptions}}));
            std::cerr<<"transport block "<<block<<" rendered\n";
        }
        std::cout<<juce::JSON::toString(object({{"state",matches?"complete":"failed"},{"seconds",seconds},{"instances",instances},{"rate",48000},{"effectsEnabled",effects},{"referenceBlock",64},{"comparisons",comparisons},{"scope","Repeated 24-second host script: 120/87/172/95 BPM, temporarily missing tempo, stopped/frozen host PPQ with continued internal playback, resume, backwards seeks and one-second loops. Full MPE/sustain/voice load, prepared refresh every two seconds, C++ callback heap guards, finite audio, retained owners, nonzero stopped-host seconds and final contact/refresh drain. Every sample is compared within .0001 against the 64-frame reference. This is an editor-free correctness workload, not a host deadline guarantee."}}),true)<<'\n';
        check(matches,"Full-contact tempo/transport audio depends on host buffer partition");
    }else if(argc>=2&&argc<=3&&juce::String(argv[1])=="--benchmark"){
        const int instances=argc==3?juce::String(argv[2]).getIntValue():1;printHeader();
        for(double rate:{48000.,96000.})for(int block:{64,256})for(bool effects:{false,true})printRun(rate,block,effects,instances,2);
    }else if(argc>=2&&argc<=6&&juce::String(argv[1])=="--soak"){
        const double seconds=argc>2?juce::String(argv[2]).getDoubleValue():60;const int instances=argc>3?juce::String(argv[3]).getIntValue():2;
        const int block=argc>4?juce::String(argv[4]).getIntValue():64;const double rate=argc>5?juce::String(argv[5]).getDoubleValue():96000;
        check(block>0&&block<=2048&&(rate==48000||rate==96000),"Soak requires 1–2048 frames at 48000 or 96000 Hz");printHeader();printRun(rate,block,true,instances,seconds);
    }else if(argc==2&&juce::String(argv[1])=="--profile"){
        for(int i=0;i<12;++i)run(96000,64,true,false);
    }else if(argc>=3&&argc<=5&&juce::String(argv[1])=="--capture"){
        const int block=argc>3?juce::String(argv[3]).getIntValue():64;const double rate=argc>4?juce::String(argv[4]).getDoubleValue():96000;
        check(block>0&&block<=2048&&(rate==48000||rate==96000),"Capture requires 1–2048 frames at 48000 or 96000 Hz");
        const auto result=run(rate,block,true,true);std::ofstream file(argv[2],std::ios::binary);
        file.write(reinterpret_cast<const char*>(result.samples.data()),std::streamsize(result.samples.size()*sizeof(float)));check(bool(file),"Could not write stress audio capture");
        std::cout<<"captured "<<result.samples.size()<<" float32 samples; energy="<<result.energy<<'\n';
    }else{
        const auto reference=isolated(false),changed=isolated(true);check(reference==changed,"One instance's MIDI, panic or runtime edit changed another instance's audio");
        check(std::any_of(reference.begin(),reference.end(),[](float value){return std::abs(value)>.01f;}),"Instance isolation compared silent renders");
        run(48000,257,true,false,2,4);
        for(bool effects:{false,true}){const auto reference=run(48000,64,effects,true);
            for(int block:{257,2048}){const auto other=run(48000,block,effects,true);check(reference.samples.size()==other.samples.size(),"Intent render lengths differ");double error=0;
                for(size_t i=0;i<reference.samples.size();++i)error=std::max(error,double(std::abs(reference.samples[i]-other.samples[i])));
                if(error>=.0001){size_t first=0,worst=0;while(first<reference.samples.size()&&std::abs(reference.samples[first]-other.samples[first])<.0001)++first;
                    for(size_t i=0;i<reference.samples.size();++i)if(std::abs(reference.samples[i]-other.samples[i])==error){worst=i;break;}
                    std::cerr<<"effects="<<effects<<" block="<<block<<" first difference="<<first/2<<" worst frame="<<worst/2<<" reference="<<reference.samples[worst]<<" actual="<<other.samples[worst]<<" maximum audio difference="<<error<<'\n';}
                check(error<.0001,"Full-contact intent audio depends on host buffer partition");
            }
        }
        std::cout<<"intent performance: editor-free full contact/sustain/voice load, concurrent instance isolation, repeated refresh, exact lifecycle, allocation-free callbacks and buffer-invariant audio passed\n";
    }
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
