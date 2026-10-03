#include <ssb/Engine.h>
#include <ssb/Parameters.h>
#include <ssb/ContactEffects.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <cstdlib>
#include <new>

namespace realtime {
thread_local bool watching=false;
thread_local size_t allocations=0, deletions=0;
}
void* operator new(std::size_t size){if(realtime::watching)++realtime::allocations;if(auto* p=std::malloc(std::max(size,size_t(1))))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept {if(p&&realtime::watching)++realtime::deletions;std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
void* operator new(std::size_t size,std::align_val_t alignment){if(realtime::watching)++realtime::allocations;void* p=nullptr;if(posix_memalign(&p,(size_t)alignment,std::max(size,size_t(1)))==0)return p;throw std::bad_alloc();}
void* operator new[](std::size_t size,std::align_val_t alignment){return ::operator new(size,alignment);}
void operator delete(void* p,std::align_val_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::align_val_t) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {::operator delete(p);}

void check(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
using Samples=std::vector<float>;
ssb::SamplePtr fixture(){
 auto sample=std::make_shared<ssb::Sample>();sample->rate=48000;sample->channels.resize(1);
 for(int i=0;i<12000;++i)sample->channels[0].push_back(float(.25*std::sin(i*.043)+.13*std::sin(i*.317)+.04*std::cos(i*.727)));
 return sample;
}
struct Run {
 ssb::Engine engine;
 std::shared_ptr<ssb::Sound> sound=std::make_shared<ssb::Sound>();
 std::shared_ptr<ssb::Performance> performance=std::make_shared<ssb::Performance>();
 Run(){sound->id="part";sound->sample=fixture();sound->s.mode=ssb::TriggerMode::hold;sound->s.repeat=0;performance->volume=1;}
 void ready(){performance->byId[sound->id]=sound;engine.prepare(48000,257);engine.setPerformance(performance);float scratch=0;engine.process(&scratch,&scratch,0,nullptr,0);}
 Samples render(int frames,int block=64){Samples out((size_t)frames*2);std::array<float,257> left{},right{};for(int start=0;start<frames;start+=block){const int count=std::min(block,frames-start);engine.process(left.data(),right.data(),count,nullptr,0);for(int i=0;i<count;++i){out[(size_t)(start+i)*2]=left[(size_t)i];out[(size_t)(start+i)*2+1]=right[(size_t)i];}}return out;}
};
void set(ssb::Settings& s,int index,float value){const auto& p=ssb::parameters[index];if(p.integer)s.*(p.integer)=(int)std::lround(value);else s.*(p.member)=value;}
double energy(const Samples& s){double sum=0;for(float x:s){check(std::isfinite(x),"Non-finite audio");sum+=x*x;}return sum;}
double difference(const Samples&a,const Samples&b){check(a.size()==b.size(),"Mismatched render length");double maximum=0;for(size_t i=0;i<a.size();++i)maximum=std::max(maximum,double(std::abs(a[i]-b[i])));return maximum;}
Samples mapped(const std::string&name,float value,bool route,int source=3){
 Run run;const int index=ssb::parameterIndex(name);check(index!=-1,"Unknown target "+name);
 if(name.starts_with("grain"))run.sound->s.grain=true;
 if(name.starts_with("delay"))run.sound->s.delayMix=.7f;
 if(name.starts_with("reverb"))run.sound->s.reverbMix=.7f;
 if(route){run.sound->expressionRouteCount=1;run.sound->expressionRoutes[0]={source,index,value,value,1,0};}
 else if(index==-2)run.sound->s.reverse=value>=.5f;else set(run.sound->s,index,value);
 run.ready();run.engine.drumHit(run.sound,1,1,.71f,{.6f,.5f,.4f,.8f,.2f});return run.render(7000);
}
int main(){try{
 const std::pair<const char*,float> controls[]={
  {"delayTime",.07f},{"delayFeedback",.6f},{"delayMix",.4f},{"reverbSize",.6f},{"reverbDecay",1.3f},{"reverbMix",.5f},
  {"volume",.27f},{"pan",-.73f},{"pitch",7},{"fine",42},{"speed",1.7f},{"reverse",1},
  {"clipIn",.31f},{"clipOut",.62f},{"repeat",2},{"rootNote",67},{"velAmount",.25f},
  {"attack",.018f},{"decay",.047f},{"sustain",.41f},{"release",.11f},
  {"cutoff",1800},{"resonance",2.4f},{"fEnvAmount",2},{"fAttack",.004f},{"fDecay",.08f},{"fSustain",.3f},{"fRelease",.12f},
  {"eqLow",-8},{"eqMid",7},{"eqHigh",-5},
  {"grainSize",33},{"grainPos",.23f},{"grainWidth",.42f},{"grainRate",110},{"grainShape",.3f},{"grainJitter",5},
  {"grainReverse",.8f},{"grainSpread",.8f},{"grainStreams",4},{"grainScatter",.9f},{"grainDrift",.6f}
 };
 for(auto [name,value]:controls)check(difference(mapped(name,value,false),mapped(name,value,true))<.00002,std::string("Mapped value differs from equivalent knob setting: ")+name);
 // Every contact/global dimension controls actual audio, not just an editor meter.
 for(int source=0;source<10;++source){Run run;run.sound->expressionRouteCount=1;run.sound->expressionRoutes[0]={source,ssb::parameterIndex("volume"),0,1,1,0};run.ready();std::array<float,5> values{};if(source<5)values[(size_t)source]=1;else run.engine.drumGlobal(source-5,1);run.engine.drumHit(run.sound,1,1,1,values);check(energy(run.render(1000))>1,"Expression source produces no audio: "+std::to_string(source));}
 // A pressure sweep must be identical across host buffer partitions.
 auto sweep=[](int block){Run run;run.sound->expressionRouteCount=1;run.sound->expressionRoutes[0]={3,ssb::parameterIndex("volume"),0,1,1,12};run.ready();run.engine.drumHit(run.sound,1,1,1,{1,.5f,0,0,0});auto out=run.render(511,block);run.engine.drumExpression(1,{1,.5f,0,1,0});auto tail=run.render(6000,block);out.insert(out.end(),tail.begin(),tail.end());return out;};
 check(difference(sweep(64),sweep(257))<.00002,"Expression smoothing depends on host block size");
 // Expression for one owner must not reach another key or a channel's later owner.
 auto isolated=[](bool change){Run run;run.sound->s.pan=-1;run.sound->expressionRouteCount=1;run.sound->expressionRoutes[0]={3,ssb::parameterIndex("volume"),0,1,1,0};auto other=std::make_shared<ssb::Sound>(*run.sound);other->id="other";other->s.pan=1;run.performance->byId[other->id]=other;run.ready();run.engine.drumHit(run.sound,10,1,1,{1,.5f,0,1,0});run.engine.drumHit(other,11,2,1,{1,.5f,0,1,0});run.render(512);if(change)run.engine.drumExpression(10,{1,.5f,0,0,0});return run.render(1000);};
 auto baseline=isolated(false),changed=isolated(true);double left=0;for(size_t i=0;i<baseline.size();i+=2){left+=std::abs(baseline[i]-changed[i]);check(std::abs(baseline[i+1]-changed[i+1])<.00001,"One contact changed another contact's voice");}check(left>1,"Contact expression did not change its own voice");
 auto release=[](float lift){Run run;run.sound->expressionRouteCount=1;run.sound->expressionRoutes[0]={4,ssb::parameterIndex("release"),.01f,.3f,1,0};run.ready();run.engine.drumHit(run.sound,1,1,1,{1,.5f,0,1,0});run.render(1000);run.engine.drumRelease(1,{1,.5f,0,1,lift},false);run.render(2400);return energy(run.render(2400));};
 check(release(1)>release(0)+1,"Lift does not change the audible release tail");
 // Choke uses the mapped group, and must not choke an unrelated key.
 auto choke=[](bool sameGroup){Run run;run.sound->s.pan=-1;run.sound->expressionRouteCount=1;run.sound->expressionRoutes[0]={3,ssb::parameterIndex("choke"),0,3,1,0};auto other=std::make_shared<ssb::Sound>(*run.sound);other->id="choke-other";other->s.pan=1;run.performance->byId[other->id]=other;run.ready();run.engine.drumHit(run.sound,1,1,1,{1,.5f,0,1,0});run.render(1000);run.engine.drumHit(other,2,2,1,{1,.5f,0,sameGroup?1.f:0.f,0});run.render(512);return run.render(1000);};
 auto choked=choke(true),unchoked=choke(false);double stopped=0,ringing=0;for(size_t i=0;i<choked.size();i+=2){stopped+=choked[i]*choked[i];ringing+=unchoked[i]*unchoked[i];}check(stopped<1e-8&&ringing>1,"Mapped choke did not stop only the matching group");
 // Changing grain stream count while sounding must affect the actual source,
 // and all live grain controls must retain buffer-invariant timing.
 for(const std::string target:{"grainStreams","grainDrift","grainSize","grainRate"}){
  auto grains=[&](bool change,int block){Run run;run.sound->s.grain=true;run.sound->s.grainSize=40;const int index=ssb::parameterIndex(target);float lo=0,hi=1;if(target=="grainStreams"){lo=1;hi=6;}else if(target=="grainSize"){lo=20;hi=90;}else if(target=="grainRate"){lo=25;hi=120;}
   run.sound->expressionRouteCount=1;run.sound->expressionRoutes[0]={3,index,lo,hi,1,8};run.ready();run.engine.drumHit(run.sound,1,1,1,{1,.5f,0,0,0});run.render(1023,block);if(change)run.engine.drumExpression(1,{1,.5f,0,1,0});return run.render(7000,block);};
  auto changing=grains(true,64);check(difference(changing,grains(false,64))>.001,"Live grain control has no audible effect: "+target);check(difference(changing,grains(true,257))<.00003,"Live grain control depends on buffer size: "+target);
 }
 // Continuous effect sweeps retain their wet output and sample timing across
 // more settings than SSB's original fixed delay/reverb slot pools can hold.
 for(const std::string target:{"delayTime","delayFeedback","reverbSize","reverbDecay","reverbMix"}){
  auto effects=[&](int block,bool wet){Run run;run.sound->s.delayMix=wet?.6f:0;run.sound->s.reverbMix=wet?.4f:0;
   const int index=ssb::parameterIndex(target);const auto& parameter=ssb::parameters[index];
   if(wet){run.sound->expressionRouteCount=1;run.sound->expressionRoutes[0]={3,index,parameter.min,parameter.max,1,8};}
   run.ready();run.engine.drumHit(run.sound,1,1,1,{1,.5f,0,0,0});Samples result;
   for(int i=0;i<24;++i){run.engine.drumExpression(1,{1,.5f,0,float(i%12)/11,0});auto piece=run.render(511,block);result.insert(result.end(),piece.begin(),piece.end());}
   return result;};
  const auto wet=effects(64,true);check(difference(wet,effects(257,true))<.00003,"Effect sweep depends on host block size: "+target);
  check(difference(wet,effects(64,false))>.001,"Effect sweep loses its wet signal: "+target);
 }
 // Glide behaves as conventional pitch bend until it is explicitly assigned.
 auto bent=[](float glide,float range,bool cutoffRoute){Run run;run.sound->s.bendRange=range;if(cutoffRoute){run.sound->expressionRouteCount=1;run.sound->expressionRoutes[0]={1,ssb::parameterIndex("cutoff"),3000,3000,1,0};}run.ready();run.engine.drumHit(run.sound,1,1,1,{1,glide,0,1,0});return run.render(4000);};
 check(difference(bent(.8f,12,false),bent(.8f,0,false))>.01,"Bend range does not change Glide pitch");
 check(difference(bent(.8f,12,true),bent(.5f,12,true))<.00002,"A Glide cutoff binding also bends pitch");
 // A second contact's effects remain unchanged when the first one's room
 // and delay sweep. Summing separately rendered contacts must equal the mix.
 auto contactMix=[](bool first,bool second){ssb::ContactEffects fx;fx.prepare(48000);const int a=fx.acquire(1),b=fx.acquire(2);Samples out(12000*2);std::array<float,32> l{},r{};
  for(int start=0;start<12000;start+=32){const int n=std::min(32,12000-start);fx.begin();l.fill(0);r.fill(0);
   for(int i=0;i<n;++i){const int frame=start+i;const float pulse=frame%1400==0?.7f:0;const float control=float(frame%4000)/4000;
    if(first)fx.send(a,1,1,i,pulse,0,pulse,0,{.012f+control*.02f,.6f,.1f+control*5,2});
    if(second)fx.send(b,2,2,i,0,pulse,0,pulse,{.06f,.4f,3,4});
   }
   fx.process(l.data(),r.data(),n);for(int i=0;i<n;++i){out[size_t(start+i)*2]=l[size_t(i)];out[size_t(start+i)*2+1]=r[size_t(i)];}
  }return out;};
 auto both=contactMix(true,true),first=contactMix(true,false),second=contactMix(false,true);
 for(size_t i=0;i<both.size();++i)check(std::abs(both[i]-first[i]-second[i])<.00001,"One contact's effect sweep changed another's tail");
 check(energy(both)>1,"Contact effects produced no wet audio");
 // Exercise contact turnover and continuous effects with the heap guard on.
 {ssb::ContactEffects fx;fx.prepare(48000);std::array<float,32> l{},r{};bool allocatedAll=true;
  realtime::watching=true;
  for(uint64_t generation=1;generation<=128;++generation){const int slot=fx.acquire(generation);allocatedAll=allocatedAll&&slot>=0;fx.begin();l.fill(0);r.fill(0);
   for(int i=0;i<32;++i)fx.send(slot,generation,uint32_t(generation),i,.01f,.02f,.03f,.04f,{.012f,.8f,float(generation%6)+.1f,5},true);
   fx.process(l.data(),r.data(),32);
  }
  fx.reset();realtime::watching=false;
  check(allocatedAll,"Contact effects did not recycle released tails");check(realtime::allocations==0&&realtime::deletions==0,"Contact effect sweeps allocated or destroyed heap memory");
 }
 // Exhaustion replaces a released tail, never an active contact. Reusing
 // its ring storage must not reveal samples from the previous owner.
 {ssb::ContactEffects fx;fx.prepare(48000);std::array<int,32> slots{};for(int i=0;i<32;++i)slots[size_t(i)]=fx.acquire(uint64_t(i+1));
  check(fx.acquire(33)==-1,"Effect pool steals an active contact");
  fx.begin();fx.send(slots[1],2,1,0,1,1,1,1,{.01f,.5f,2,3});std::array<float,32> l{},r{};fx.process(l.data(),r.data(),32);fx.release(2);
  check(fx.acquire(33)==slots[1],"Effect pool did not replace the released tail");check(fx.acquire(1)==slots[0],"Tail replacement displaced an active contact");
  double stale=0;for(int block=0;block<160;++block){fx.begin();l.fill(0);r.fill(0);fx.process(l.data(),r.data(),32);for(int i=0;i<32;++i)stale+=std::abs(l[size_t(i)])+std::abs(r[size_t(i)]);}
  check(stale==0,"Reused effect storage leaked the previous contact's samples");
 }
 // Offline bounces can run without a message-thread timer. Repeated notes
 // using the current immutable kit must not consume retirement capacity.
 Run offline;offline.ready();std::array<float,128> offlineLeft{},offlineRight{};
 realtime::watching=true;
 for(int i=0;i<20000;++i){offline.engine.drumHit(offline.sound,1,(uint64_t)i+1,1,{1,.5f,0,1,0});if(i%64==0)offline.engine.process(offlineLeft.data(),offlineRight.data(),128,nullptr,0);}
 realtime::watching=false;
 check(offline.engine.canActivatePerformance(),"Long offline rendering exhausted retirement capacity");
 check(realtime::allocations==0 && realtime::deletions==0,"Offline rendering allocated or destroyed C++ heap objects");
 // Starve the non-audio collector while replacing thousands of unique sounds.
 // Old sounds have no external owner after each iteration, so a ring-overflow
 // fallback that destroys one on the audio thread is observable here.
 Run stressed;stressed.ready();std::array<float,128> leftBuffer{},rightBuffer{};
 for(int i=0;i<12000;++i){
  auto sound=std::make_shared<ssb::Sound>(*stressed.sound);sound->id="retirement-stress-sound-"+std::to_string(i);
  realtime::watching=true;
  stressed.engine.drumHit(sound,1,(uint64_t)i+1,1,{1,.5f,0,1,0});
  stressed.engine.drumExpression(1,{1,.5f,.3f,.7f,0});
  stressed.engine.drumGlobal(i%5,.5f);
  if(i%64==0)stressed.engine.process(leftBuffer.data(),rightBuffer.data(),128,nullptr,0);
  realtime::watching=false;
 }
 check(!stressed.engine.canActivatePerformance(),"Retirement backpressure was not exercised");
 check(realtime::allocations==0,"Audio path allocated C++ heap memory");check(realtime::deletions==0,"Audio path destroyed heap objects when the collector fell behind");
 stressed.engine.collectGarbage();check(stressed.engine.canActivatePerformance(),"Retirement backpressure did not recover");
 realtime::watching=true;stressed.engine.drumHit(stressed.sound,2,20000,1,{1,.5f,0,1,0});stressed.engine.process(leftBuffer.data(),rightBuffer.data(),128,nullptr,0);realtime::watching=false;
 check(realtime::allocations==0 && realtime::deletions==0,"Audio path allocates after retirement recovery");
 std::cout<<"expression: 42 knob equivalences, five contact and five global sources, buffer-invariant expression/FX sweeps, contact isolation, lift tails, Glide routing and real-time retirement stress passed\n";
 return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
