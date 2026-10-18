#include <drummer/Scheduler.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <tuple>

using namespace drummer;
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
struct Capture {
    int frame=0;
    bool full=false;
    std::vector<Event> events;
    Capture(){events.reserve(65536);}
    static void take(void* context,const Event& source){
        auto& self=*static_cast<Capture*>(context);
        if(self.events.size()==self.events.capacity()){self.full=true;return;}
        auto event=source;event.offset+=self.frame;self.events.push_back(event);
    }
};
void process(Scheduler& scheduler,const Configuration& config,int at,int frames,const Midi* midi,int count,Capture& capture,double bpm=120){
    capture.frame=at;realtime::watching=true;
    scheduler.process(config,{at*bpm/(60*48000.),bpm,48000,true},frames,midi,count,Capture::take,&capture);
    realtime::watching=false;
    check(!realtime::allocations&&!realtime::deletions,"Intent callback allocated or deleted memory");
    check(!scheduler.overflow&&!capture.full,"Combined intent/contact workload exceeded its bounded storage");
}
Configuration pair(){
    Configuration config;config.launchGrid=0;config.switchGrid=0;
    config.clips.resize(2);
    for(int i=0;i<2;++i){config.clips[size_t(i)].id=uint64_t(i+1);prepareClip(config.clips[size_t(i)]);}
    PatternGroup group;group.count=2;group.candidates[0]={0,{{0,0,0,0}}};group.candidates[1]={1,{{1,1,1,1}}};config.groups.push_back(group);
    auto& key=config.keys[60];key.enabled=true;key.zoneCount=1;key.zones[0]={1,0,0};key.intentEnabled=true;
    key.intentWeights={{0,1,0,0}};key.intentPenalty=0;key.intentDwell=0;key.intentSlewMs=0;
    return config;
}
std::vector<int> switches(const Capture& capture){
    std::vector<int> result;for(const auto& event:capture.events)if(event.type==Event::switchZone)result.push_back(event.offset);return result;
}
std::vector<int> pressureStep(int block,float slew,double dwell,double grid){
    auto config=pair();config.keys[60].intentSlewMs=slew;config.keys[60].intentDwell=dwell;config.switchGrid=grid;
    Scheduler scheduler;Capture capture;const Midi source[]={{0,0x91,60,100},{100,0xd1,127,0}};size_t cursor=0;
    for(int at=0;at<10000;at+=block){const int count=std::min(block,10000-at);Midi input[2];int used=0;
        while(cursor<std::size(source)&&source[cursor].offset<at+count){input[used]=source[cursor++];input[used++].offset-=at;}
        process(scheduler,config,at,count,input,used,capture);
    }
    check(scheduler.contacts()[1].clip==1,"A sustained pressure step never reached its intended candidate");return switches(capture);
}
void keyboardModes(){
    for(auto mode:{KeyboardMode::hold,KeyboardMode::toggle}){
        auto config=pair();config.keyboardMode=mode;
        for(auto& key:config.keys)key=config.keys[60];
        Scheduler scheduler;Capture capture;std::array<Midi,512> input{};
        for(int pass=0;pass<3;++pass){int used=0;
            for(int note=0;note<128;++note){const auto ch=uint8_t(1+note%15);
                input[size_t(used++)]={note*3,uint8_t(0xb0|ch),74,uint8_t(pass==2?127:0)};
                input[size_t(used++)]={note*3,uint8_t(0x90|ch),uint8_t(note),100};
                input[size_t(used++)]={note*3+1,uint8_t(0x80|ch),uint8_t(note),64};
            }
            process(scheduler,config,pass*512,512,input.data(),used,capture);
        }
        auto replacement=config;replacement.keyboardMode=KeyboardMode::normal;
        realtime::watching=true;scheduler.reconfigure(config,replacement,1536/24000.,Capture::take,&capture);realtime::watching=false;
        check(!realtime::allocations&&!realtime::deletions,"Keyboard mode retargeting allocated on audio");
        process(scheduler,replacement,1536,512,nullptr,0,capture);
        check(!scheduler.hasHeldNotes(),"Mode-change stress stranded latches");
    }
}
void stabilization(){
    for(int block:{64,257,2048}){
        check(pressureStep(block,0,0,0)==std::vector<int>{100},"Immediate pressure selection added unexpected latency");
        // A 50 ms one-pole reaches 0.5 after 34.66 ms. The fixed 1/32-beat
        // intent clock observes it at sample 2250, independent of host blocks.
        check(pressureStep(block,50,0,0)==std::vector<int>{2250},"Intent slew or its observation clock depends on host blocks");
        check(pressureStep(block,0,.25,.125)==std::vector<int>{6000},"Minimum dwell did not defer a same-region candidate change");
    }
    auto config=pair();config.keys[60].intentPenalty=.025f;
    Scheduler scheduler;Capture capture;std::vector<Midi> source{{0,0x91,60,100}};
    for(int sample=100;sample<3000;sample+=100)source.push_back({sample,0xd1,uint8_t(sample%200?63:64),0});
    source.push_back({3000,0xd1,127,0});
    for(int sample=3100;sample<6000;sample+=100)source.push_back({sample,0xd1,uint8_t(sample%200?63:64),0});
    source.push_back({6000,0xd1,0,0});
    process(scheduler,config,0,7000,source.data(),int(source.size()),capture);
    check(switches(capture)==std::vector<int>({3000,6000}),"Pressure noise chattered across a candidate boundary");
    config=pair();auto& key=config.keys[60];key.zoneCount=2;key.zones[0]={.5f,0,-1};key.zones[1]={1,1,-1};key.intentEnabled=false;
    scheduler.reset();capture.events.clear();source={{0,0xb1,74,60},{0,0x91,60,100}};
    for(int sample=100;sample<2000;sample+=100)source.push_back({sample,0xb1,74,uint8_t(sample%200?63:65)});
    source.push_back({2000,0xb1,74,68});
    for(int sample=2100;sample<4000;sample+=100)source.push_back({sample,0xb1,74,uint8_t(sample%200?63:65)});
    source.push_back({4000,0xb1,74,60});
    process(scheduler,config,0,5000,source.data(),int(source.size()),capture);
    check(switches(capture)==std::vector<int>({2000,4000}),"Slide boundary noise bypassed zone hysteresis");
}
Configuration dense(){
    Configuration config;config.launchGrid=0;config.switchGrid=.125;
    // Exercise the legal clip capacity, while only a bounded active subset
    // participates in each contact's sixteen-candidate regions.
    config.clips.resize(maxRuntimeClips);
    for(int i=0;i<maxRuntimeClips;++i){auto& clip=config.clips[size_t(i)];clip.id=uint64_t(i+1);clip.length=4;
        for(int note=0;note<12;++note)clip.notes.push_back({note/3.+(i%7)*.0001, .7+(i%5)*.1,36+(note%3==1?2:note%3==2?6:0),.3f+float(i%16)/32});
        prepareClip(clip);
    }
    for(int region=0;region<3;++region){PatternGroup group;group.count=maxCandidates;
        for(int i=0;i<maxCandidates;++i)group.candidates[size_t(i)]={region*maxCandidates+i,{{i/15.f,i/15.f,(15-i)/15.f,i/15.f}}};
        config.groups.push_back(group);
    }
    for(int pitch=60;pitch<63;++pitch){auto& key=config.keys[size_t(pitch)];key.enabled=true;key.parts[size_t(pitch==60?36:pitch==61?38:42)]=true;
        key.zones={Zone{.25f,0,0},Zone{.75f,16,1},Zone{1,32,2}};key.intentEnabled=true;key.intentSlewMs=35;key.intentDwell=.125;
    }
    return config;
}
using Record=std::tuple<int,int,int,int,int,uint64_t,uint64_t,float,float,float,float,float,float>;
std::vector<Record> combined(int block){
    auto original=dense(),replacement=original;std::reverse(replacement.clips.begin(),replacement.clips.end());
    for(auto& group:replacement.groups)for(int i=0;i<group.count;++i)group.candidates[size_t(i)].clip=maxRuntimeClips-1-group.candidates[size_t(i)].clip;
    for(auto& key:replacement.keys)for(int i=0;i<key.zoneCount;++i)if(key.zones[size_t(i)].clip>=0)key.zones[size_t(i)].clip=maxRuntimeClips-1-key.zones[size_t(i)].clip;
    replacement.clips.back().id=999999;replacement.clips.back().notes.clear();prepareClip(replacement.clips.back());
    Scheduler scheduler;Capture capture;std::vector<Midi> events{{0,0xb0,64,127}};
    for(int owner=0;owner<Scheduler::maxSustained;++owner){const int channel=owner%maxContacts;const auto status=uint8_t(channel);
        events.push_back({owner*3,uint8_t(0xb0|status),74,uint8_t(owner%128)});
        events.push_back({owner*3,uint8_t(0xd0|status),uint8_t((owner*13)%128),0});
        events.push_back({owner*3,uint8_t(0x90|status),uint8_t(60+channel%3),uint8_t(50+owner%70)});
        events.push_back({owner*3+1,uint8_t(0x80|status),uint8_t(60+channel%3),uint8_t(owner%128)});
    }
    // All sixteen scheduler channels are a capacity test; a conventional lower
    // MPE zone uses fifteen members and reserves its master for global control.
    for(int ch=0;ch<maxContacts;++ch){events.push_back({512, uint8_t(0x90|ch),uint8_t(60+ch%3),100});events.push_back({1200+ch,uint8_t(0x90|ch),uint8_t(60+ch%3),90});}
    for(int sample=600;sample<47000;sample+=137){const int ch=(sample/137)%maxContacts;
        events.push_back({sample,uint8_t(0xd0|ch),uint8_t((sample/137)%128),0});
        events.push_back({sample,uint8_t(0xb0|ch),74,uint8_t((sample/311)%128)});
        const int raw=(sample*17)%16384;events.push_back({sample,uint8_t(0xe0|ch),uint8_t(raw&127),uint8_t(raw>>7)});
    }
    events.push_back({20000,0xb0,64,0});
    for(int ch=0;ch<maxContacts;++ch)events.push_back({48000,uint8_t(0x80|ch),uint8_t(60+ch%3),uint8_t(30+ch*5)});
    std::stable_sort(events.begin(),events.end(),[](const Midi& a,const Midi& b){return a.offset<b.offset;});
    std::map<uint64_t,Expression> frozen;std::map<uint64_t,float> lifts;size_t cursor=0;int iteration=0;
    const Configuration* current=&original;std::array<Midi,1024> input{};
    const int checkpoints[]={512,1024,12000,20000,24000,48000,80000};
    for(int at=0;at<80000;){
        if(at==12000||at==24000){const auto* next=at==12000?&replacement:&original;capture.frame=at;realtime::watching=true;
            scheduler.reconfigure(*current,*next,at*137/(60*48000.),Capture::take,&capture);realtime::watching=false;current=next;
            check(!realtime::allocations&&!realtime::deletions,"Runtime replacement allocated or deleted memory on audio");
        }
        if(at==512||at==1024){int held=0,physical=0;for(const auto& f:scheduler.sustainedContacts())if(f.held){++held;frozen[f.owner]=f.values;lifts[f.owner]=f.releaseVelocity;}
            for(const auto& f:scheduler.contacts())physical+=f.held;
            check(held==Scheduler::maxSustained&&physical==(at==512?0:maxContacts),"Workload failed to occupy every sustained/physical contact slot");
        }
        if(at>=1024&&at<=20000)for(const auto& f:scheduler.sustainedContacts())if(f.held){const auto old=frozen.find(f.owner);
            if(old!=frozen.end())check(std::tie(f.values.strike,f.values.glide,f.values.slide,f.values.press)==std::tie(old->second.strike,old->second.glide,old->second.slide,old->second.press),"Channel reuse or intent refresh altered a sustained owner's expression");
        }
        int count=std::min(block?block:std::array<int,4>{13,409,37,1024}[size_t(iteration++%4)],80000-at);
        for(int point:checkpoints)if(point>at)count=std::min(count,point-at);
        int used=0;while(cursor<events.size()&&events[cursor].offset<at+count){check(used<int(input.size()),"Test input capacity is insufficient");input[size_t(used)]=events[cursor++];input[size_t(used++)].offset-=at;}
        process(scheduler,*current,at,count,input.data(),used,capture,137);
        check(scheduler.evaluations<256,"Intent selection evaluated an unbounded candidate pool");at+=count;
    }
    check(!scheduler.hasHeldNotes(),"Combined sustain/reuse/refresh left a held owner after release");
    std::set<uint64_t> owners,ended,released;std::set<std::pair<uint64_t,uint64_t>> hits;std::map<uint64_t,int> sounding;std::vector<Record> result;
    for(const auto& e:capture.events){
        if(e.type==Event::hit){check(e.drumNote==(e.key==60?36:e.key==61?38:42),"Selected candidate bypassed the per-key drum gate");
            check(!ended.contains(e.token)&&hits.emplace(e.token,e.owner).second,"Hit token was reused for the same owner");
            const auto [position,inserted]=sounding.emplace(e.token,e.offset);check(inserted||position->second==e.offset,"A shared clip token was reused at another onset");owners.insert(e.owner);}
        if(e.type==Event::endHit){check(sounding.erase(e.token)==1,"A pending note ending lost its originating hit");ended.insert(e.token);}
        if(e.type==Event::releaseContact){check(released.insert(e.owner).second,"An owner was released twice after capacity eviction or reuse");if(lifts.contains(e.owner))check(e.values.lift==lifts[e.owner],"Sustain/replacement lost the owner's release velocity");}
        result.emplace_back(e.offset,int(e.type),e.key,e.channel,e.drumNote,e.owner,e.token,e.velocity,e.values.strike,e.values.glide,e.values.slide,e.values.press,e.values.lift);
    }
    check(released.size()==160&&sounding.empty()&&!owners.empty(),"Combined lifecycle lost an owner or outstanding note ending");return result;
}
}
int main(){try{
    keyboardModes();stabilization();const auto reference=combined(64);
    for(int block:{257,2048,0})check(combined(block)==reference,"Intent/lifecycle event trace depends on host buffer partition");
    uint64_t fingerprint=14695981039346656037ULL;
    for(const auto& event:reference)std::apply([&](const auto&... fields){
        auto feed=[&](const auto& value){std::array<unsigned char,sizeof(value)> bytes{};std::memcpy(bytes.data(),&value,sizeof(value));for(auto byte:bytes){fingerprint^=byte;fingerprint*=1099511628211ULL;}};
        (feed(fields),...);
    },event);
    std::cout<<"intent: analytical slew/dwell, pressure/slide jitter, 16 physical + 128 sustained contacts, eviction/reuse, runtime replacement, gating, pending endings and allocation-free buffer invariance passed\n"
             <<"events="<<reference.size()<<" trace="<<std::hex<<fingerprint<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
