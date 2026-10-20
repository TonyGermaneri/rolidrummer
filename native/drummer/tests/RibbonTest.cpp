#include <drummer/Scheduler.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <tuple>
using namespace drummer;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void sink(void*,const Event&){}
Configuration fixture(){
 Configuration cfg;cfg.launchGrid=cfg.switchGrid=0;cfg.latchGrid=4;
 cfg.clips={{{},1,1,0,{{0,.1,36,1}}}};prepareClip(cfg.clips[0]);
 for(int note=48;note<64;++note){auto& key=cfg.keys[(size_t)note];key.enabled=true;key.parts[36]=true;key.zones={Zone{.33f,0},Zone{.67f,0},Zone{1,0}};}
 prepareKeyMapping(cfg);return cfg;
}
int playing(const Scheduler& s){int count=0;for(const auto& f:s.contacts())count+=f.held;for(const auto& f:s.sustainedContacts())count+=f.held;return count;}
void process(Scheduler& s,const Configuration& cfg,double beat,std::initializer_list<Midi> input={}){s.process(cfg,{beat,120,48000,true},64,input.begin(),(int)input.size(),sink,nullptr);}
Midi action(bool start){return {0,0,0,0,start?Midi::ribbonTop:Midi::ribbonStop};}
Midi hardware(uint8_t status,uint8_t a,uint8_t b){return {0,status,a,b,Midi::message,true};}
}
int main(){try{
 for(const auto mode:{KeyboardMode::normal,KeyboardMode::hold,KeyboardMode::toggle})for(bool quantized:{false,true}){
  auto cfg=fixture();cfg.keyboardMode=mode;cfg.latchGrid=quantized?4:0;Scheduler s;
  process(s,cfg,.1,{action(true)});
  check(playing(s)==(quantized?0:16),"Top ribbon ignored its quantization setting");
  if(quantized){for(int note=48;note<64;++note)check(s.pendingLatchAction(note)==1,"Missing green all-key start indicator");process(s,cfg,4);}
  check(playing(s)==16,"Top ribbon did not start every origin exactly once");
  for(const auto& f:s.sustainedContacts())if(f.held)check(f.zone==2&&f.fixedTop&&f.values.glide==.5f,"Top ribbon changed tuning or did not hold the top zone");
  process(s,cfg,4.1,{action(true)});check(playing(s)==16&&!s.hasPendingLatch(48),"Repeated top action toggled or replaced an already open key");
  process(s,cfg,4.2,{action(false)});
  if(quantized){check(playing(s)==16,"Bottom ribbon stopped before its bar");for(int note=48;note<64;++note)check(s.pendingLatchAction(note)==2,"Missing red all-key stop indicator");process(s,cfg,8);}
  check(!s.hasHeldNotes(),"Bottom ribbon left a pattern or queued action running");
 }
 // Starting and stopping before the boundary must never leak a pending start.
 auto cfg=fixture();Scheduler s;process(s,cfg,.1,{action(true)});process(s,cfg,.2,{action(false)});process(s,cfg,4);check(!s.hasHeldNotes(),"Stop-all retained a queued start");
 // A stop holds a Normal-mode released finger until its selected boundary.
 process(s,cfg,4.1,{{0,0x91,48,100}});process(s,cfg,4.2,{action(false),{1,0x81,48,64}});
 check(playing(s)==1&&s.pendingLatchAction(48)==2,"Releasing the ribbon finger bypassed a quantized stop");
 process(s,cfg,8);check(!s.hasHeldNotes(),"Quantized stop stranded a released finger");
 // A new top action reverses a queued stop without losing any origins.
 process(s,cfg,8.1,{action(true)});process(s,cfg,12);process(s,cfg,12.1,{action(false)});process(s,cfg,12.2,{action(true)});
 check(playing(s)==16&&!s.hasPendingLatch(48),"Top ribbon could not cancel a queued all-key stop");
 // Hardware resets and master-channel Slide cannot invoke a ribbon action.
 s.reset();process(s,cfg,.1,{hardware(0xb1,74,127),hardware(0xb1,74,0),hardware(0xb0,74,127)});
 check(!s.hasHeldNotes(),"A pre-note or master Slide value invoked a ribbon");
 process(s,cfg,.2,{hardware(0xb1,74,127),hardware(0x91,64,100),hardware(0x81,64,64)});
 check(s.pendingLatchAction(48)==1&&playing(s)==0,"Hardware top-ribbon tap did not queue all keys independently of its note-off");process(s,cfg,4);check(playing(s)==16,"Hardware top-ribbon tap did not open all origins");
 // Release/reset CCs after the ribbon tap do not unexpectedly stop everything.
 process(s,cfg,4.1,{hardware(0xb1,74,0)});check(!s.hasPendingLatch(48),"Post-release Slide reset stopped the kit");
 process(s,cfg,4.2,{hardware(0x91,64,100),hardware(0x81,64,64)});check(s.pendingLatchAction(48)==2,"Hardware bottom-ribbon tap did not queue all-key stops");
 process(s,cfg,8);check(!s.hasHeldNotes(),"Hardware bottom ribbon left the kit running");
 // White and black bodies cover the full matrix without entering either ribbon.
 cfg.latchGrid=0;s.reset();
 process(s,cfg,.1,{hardware(0xb1,74,16),hardware(0x91,48,100),hardware(0xb2,74,59),hardware(0x92,49,100)});
 check(playing(s)==2&&s.contacts()[1].zone==0&&s.contacts()[2].zone==0,"Key-body bottoms became ribbon commands");
 process(s,cfg,.2,{hardware(0xb1,74,65),hardware(0xb2,74,87)});
 check(s.contacts()[1].zone==1&&s.contacts()[2].zone==1,"White/black Slide did not scale to each actual key height");
 process(s,cfg,.3,{hardware(0xb1,74,115),hardware(0xb2,74,115)});
 check(playing(s)==2&&s.contacts()[1].zone==2&&s.contacts()[2].zone==2,"Key-body tops became an all-key launch");
 process(s,cfg,.4,{hardware(0xb1,74,116)});check(playing(s)==16,"Sliding a held finger into the top ribbon did not open the kit");
 process(s,cfg,.5,{hardware(0xb1,74,117)});check(playing(s)==16,"Sweeping within the top ribbon toggled keys off");
 process(s,cfg,.6,{hardware(0xb1,74,15)});check(!s.hasHeldNotes(),"Sliding a held finger into the bottom ribbon did not stop all keys");
 // Ribbon owners force the top matrix even when a patch uses another source.
 s.reset();for(int note=48;note<64;++note)cfg.keys[(size_t)note].zoneSource=note%5;
 process(s,cfg,.1,{action(true)});process(s,cfg,.2);
 for(const auto& f:s.sustainedContacts())if(f.held)check(f.zone==2,"A non-Slide zone source closed a ribbon-opened part");
 // Group transitions retain sample timing under arbitrary host buffers.
 auto render=[](int block){
  auto config=fixture();Scheduler scheduler;std::vector<std::tuple<int,int,int>> result;
  struct Output {int start=0;std::vector<std::tuple<int,int,int>>* events;static void take(void* p,const Event& e){auto& out=*static_cast<Output*>(p);if(e.type==Event::hit||e.type==Event::killContact)out.events->emplace_back(out.start+e.offset,(int)e.type,e.key);}} output{0,&result};
  Midi events[]={{1234,0,0,0,Midi::ribbonTop},{110000,0,0,0,Midi::ribbonStop}};int cursor=0;
  for(int at=0;at<200000;at+=block){const int frames=std::min(block,200000-at);Midi input[2];int count=0;
   while(cursor<2&&events[cursor].offset<at+frames){input[count]=events[cursor++];input[count++].offset-=at;}
   output.start=at;scheduler.process(config,{at/24000.,120,48000,true},frames,input,count,Output::take,&output);
  }
  check(!scheduler.hasHeldNotes(),"Partitioned ribbon stop retained a part");return result;
 };
 const auto baseline=render(64);for(int block:{1,257,2048})check(render(block)==baseline,"Ribbon timing depends on host buffer size");
 // The group path has no fifteen-member-channel limit and never duplicates aliases.
 cfg=fixture();cfg.latchGrid=0;for(auto& key:cfg.keys)key=cfg.keys[48];prepareKeyMapping(cfg);s.reset();
 process(s,cfg,.1,{action(true)});check(playing(s)==128,"Ribbon launch is limited by MPE member-channel count");
 process(s,cfg,.2,{action(false)});check(!s.hasHeldNotes(),"A full-keyboard stop stranded an owner");
 std::cout<<"ribbons: full-height key matrices, hardware and screen actions, all modes, quantization, cancellation and reset isolation passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
