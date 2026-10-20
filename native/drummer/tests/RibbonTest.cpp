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
 Configuration cfg;cfg.launchGrid=cfg.switchGrid=0;cfg.keyboardMode=KeyboardMode::toggle;
 cfg.clips={{{},1,1,0,{{0,.1,36,1}}}};prepareClip(cfg.clips[0]);
 for(int note=48;note<64;++note){auto& key=cfg.keys[(size_t)note];key.enabled=true;key.parts[36]=true;key.zones={Zone{.33f,0},Zone{.67f,0},Zone{1,0}};}
 prepareKeyMapping(cfg);return cfg;
}
int playing(const Scheduler& s,int key=-1){int count=0;for(const auto& f:s.contacts())count+=f.held&&(key<0||f.key==key);for(const auto& f:s.sustainedContacts())count+=f.held&&(key<0||f.key==key);return count;}
void process(Scheduler& s,const Configuration& cfg,double beat,std::initializer_list<Midi> input={}){s.process(cfg,{beat,120,48000,true},64,input.begin(),(int)input.size(),sink,nullptr);}
Midi top(int note,int ch=1){return {0,(uint8_t)(0x90|ch),(uint8_t)note,100,Midi::ribbonTop};}
Midi stop(int note){return {0,0,(uint8_t)note,0,Midi::ribbonStop};}
Midi off(int note,int ch=1){return {0,(uint8_t)(0x80|ch),(uint8_t)note,64};}
Midi hardware(uint8_t status,uint8_t a,uint8_t b){return {0,status,a,b,Midi::message,true};}
Midi bend(int semitones,int ch=1,float range=48){const int value=8192+int(std::lround(semitones*8191./range));return hardware((uint8_t)(0xe0|ch),(uint8_t)(value&127),(uint8_t)(value>>7));}
}
int main(){try{
 // Every tap is scoped to one origin; many independent parts can stay latched.
 for(const auto mode:{KeyboardMode::normal,KeyboardMode::hold,KeyboardMode::toggle})for(bool quantized:{false,true}){
  auto cfg=fixture();cfg.keyboardMode=mode;cfg.latchGrid=quantized?4:0;Scheduler s;
  process(s,cfg,.1,{top(48)});check(playing(s)==(quantized&&mode!=KeyboardMode::normal?0:1),"Top lane did not follow the keyboard mode's launch policy");
  check(s.pendingLatchAction(49)==0,"A top touch queued an unrelated key");
  if(quantized&&mode!=KeyboardMode::normal)process(s,cfg,4);
  check(playing(s,48)==1&&playing(s)==1,"Top ribbon broadcast a start");
  process(s,cfg,4.1,{off(48)});check(playing(s)==(mode==KeyboardMode::normal?0:1),"Top ribbon did not use the normal release/hold/toggle rules");
 }
 for(bool quantized:{false,true}){
  auto cfg=fixture();cfg.latchGrid=quantized?4:0;Scheduler s;
  for(int note=48;note<58;++note)process(s,cfg,.1+(note-48)*.01,{top(note),off(note)});
  if(quantized)process(s,cfg,4);
  check(playing(s)==10,"Toggle retained only the most recently played key");
  process(s,cfg,4.1,{top(50),off(50)});if(quantized)process(s,cfg,8);
  check(playing(s)==9&&playing(s,50)==0&&playing(s,48)==1&&playing(s,57)==1,"Same-zone toggle stopped other keys");
  process(s,cfg,8.1,{{0,0xb1,74,64},{1,0x91,48,100},{2,0x81,48,64}});if(quantized)process(s,cfg,12);
  check(playing(s)==9&&playing(s,48)==1,"Different-zone replacement affected another origin or added a second owner");
  process(s,cfg,12.1,{stop(49)});check(s.pendingLatchAction(51)==0,"Bottom ribbon queued an unrelated stop");if(quantized)process(s,cfg,16);
  check(playing(s)==8&&playing(s,49)==0&&playing(s,48)==1&&playing(s,51)==1,"Bottom ribbon broadcast a stop");
 }
 // Cancel only this key's pending launch, preserving the other queued key.
 auto cfg=fixture();cfg.latchGrid=4;Scheduler s;process(s,cfg,.1,{top(48),off(48),top(49),off(49),stop(48)});
 check(!s.hasPendingLatch(48)&&s.pendingLatchAction(49)==1,"Stop lane canceled another key's pending start");process(s,cfg,4);check(playing(s)==1&&playing(s,49)==1,"Canceled launch sounded at the boundary");
 // Stops respect the bar even if a Normal-mode finger is lifted afterward.
 cfg.keyboardMode=KeyboardMode::normal;s.reset();process(s,cfg,.1,{{0,0x91,48,100}});process(s,cfg,.2,{stop(48),off(48)});
 check(playing(s,48)==1&&s.pendingLatchAction(48)==2,"Normal note-off bypassed the queued stop");process(s,cfg,4);check(!s.hasHeldNotes(),"Quantized stop stranded a finger");
 // A Normal-mode launch that has not reached its grid must be canceled,
 // rather than begin playing just to wait for a later quantized stop.
 cfg.launchGrid=1;s.reset();process(s,cfg,.1,{{0,0x91,48,100},{1,0x92,50,100}});process(s,cfg,.2,{stop(48)});
 check(playing(s,48)==0&&!s.hasPendingLatch(48)&&playing(s,50)==1,"Stopping a pending Normal launch canceled the wrong key or let it start later");
 // A top-ribbon sweep visits each chromatic lane, including skipped packets.
 cfg=fixture();s.reset();process(s,cfg,.1,{hardware(0xb1,74,127),hardware(0x91,48,100)});
 check(playing(s)==1&&playing(s,48)==1,"Hardware top tap activated multiple keys");
 process(s,cfg,.2,{bend(4)});check(playing(s)==5,"Hardware sweep did not visit all crossed chromatic keys");
 for(int note=48;note<=52;++note)check(playing(s,note)==1,"Top sweep missed a white or black key");
 check(s.contacts()[1].inputNote==52&&s.contacts()[1].anchorNote==48,"Ribbon sweep lost its current lane or original note-off identity");
 process(s,cfg,.3,{bend(4)});check(playing(s)==5,"Stationary ribbon gesture retriggered a toggle");
 process(s,cfg,.4,{hardware(0x81,49,64)});check(s.contacts()[1].down,"Stale note-off ended a swept ribbon gesture");
 process(s,cfg,.5,{hardware(0x81,48,64),hardware(0xb1,74,0)});check(playing(s)==5,"Note-off or post-release Slide reset stopped other keys");
 process(s,cfg,.6,{top(60,2),off(60,2)});
 process(s,cfg,.7,{bend(0),hardware(0x91,48,100)});check(playing(s)==5&&playing(s,48)==0&&playing(s,60)==1,"Bottom tap stopped more than its individual key");
 process(s,cfg,.8,{bend(4)});check(playing(s)==1&&playing(s,60)==1,"Bottom sweep did not stop only crossed keys");
 // Top/body transitions preserve the current touch, not a second toggle strike.
 s.reset();process(s,cfg,.1,{hardware(0xb1,74,100),hardware(0x91,48,100)});const auto owner=s.contacts()[1].owner;
 process(s,cfg,.2,{hardware(0xb1,74,127)});check(playing(s)==1&&s.contacts()[1].owner==owner&&s.contacts()[1].zone==2,"Sliding into the top ribbon toggled the current key off");
 process(s,cfg,.3,{hardware(0xb1,74,40)});check(s.contacts()[1].zone==0&&!s.contacts()[1].fixedTop,"Leaving the ribbon did not restore the key matrix");
 // Below a black key is white territory; its stop lane reappears at the ribbon.
 s.reset();process(s,cfg,.1,{hardware(0xb1,74,40),hardware(0x91,49,100)});check(s.contacts()[1].inputNote==48,"Black-key gap incorrectly activated the black key");
 process(s,cfg,.2,{hardware(0xb1,74,127)});check(playing(s,49)==1&&playing(s,48)==1,"Black-key top lane did not reappear independently");
 process(s,cfg,.3,{hardware(0xb1,74,0)});check(playing(s,49)==0&&playing(s,48)==1,"Black-key stop lane stopped the intervening white key");
 // Pitch sensitivity follows member RPN 0, and channel reuse cannot end peers.
 s.reset();process(s,cfg,.1,{hardware(0xb1,101,0),hardware(0xb1,100,0),hardware(0xb1,6,12),hardware(0xb1,74,127),hardware(0x91,48,100)});
 process(s,cfg,.2,{bend(1,1,12)});check(playing(s,49)==1&&playing(s)==2,"Ribbon lane tracking ignored RPN pitch sensitivity");
 // Fractional pitch sensitivity, null selection and NRPN data cannot
 // turn a small ribbon movement into an unintended wide sweep.
 s.reset();process(s,cfg,.1,{hardware(0xb1,101,0),hardware(0xb1,100,0),hardware(0xb1,6,12),hardware(0xb1,38,25),hardware(0xb1,74,127),hardware(0x91,48,100)});
 process(s,cfg,.2,{bend(1,1,12.25f)});check(s.contacts()[1].inputNote==49,"Fractional pitch sensitivity lost the adjacent ribbon lane");
 process(s,cfg,.3,{hardware(0xb1,99,1),hardware(0xb1,6,96),bend(2,1,12.25f)});check(s.contacts()[1].inputNote==50&&playing(s)==3,"NRPN data changed ribbon pitch sensitivity");
 process(s,cfg,.4,{hardware(0xb1,101,127),hardware(0xb1,100,127),hardware(0xb1,6,96),bend(3,1,12.25f)});check(s.contacts()[1].inputNote==51&&playing(s)==4,"Null RPN selection changed ribbon pitch sensitivity");
 // A top touch stays at the top for alternate matrix sources, while body
 // expression can resume normally without creating a second owner.
 cfg.keys[48].zoneSource=1;s.reset();process(s,cfg,.1,{top(48),{1,0xb1,74,127}});
 check(s.contacts()[1].zone==2&&playing(s)==1,"Stationary top ribbon lost the highest zone for a non-Slide matrix");
 process(s,cfg,.2,{{0,0xb1,74,64}});check(s.contacts()[1].zone==1,"Body movement did not restore the configured matrix source");
 cfg.keys[48].zoneSource=2;
 // Physical aliases share only their origin; other origins remain independent.
 s.reset();process(s,cfg,.1,{top(64),off(64),top(49,2),off(49,2)});process(s,cfg,.2,{stop(80)});
 check(playing(s,48)==0&&playing(s,49)==1,"Parallel stop changed an unrelated keywave");
 // Reusing a member channel keeps the previous key's latched strike and
 // owner independent of the new physical strike on that same channel.
 s.reset();process(s,cfg,.1,{hardware(0xb1,74,80),hardware(0x91,48,40)});const auto firstOwner=s.contacts()[1].owner;
 process(s,cfg,.2,{hardware(0x91,50,120)});check(playing(s)==2,"Reusing an MPE member stopped an unrelated toggle");
 const auto previous=std::find_if(s.sustainedContacts().begin(),s.sustainedContacts().end(),[&](const Contact& f){return f.held&&f.owner==firstOwner;});
 check(previous!=s.sustainedContacts().end()&&previous->values.strike==40/127.f&&s.contacts()[1].values.strike==120/127.f,"New strike changed a previous key's latched expression");
 // A stop through a parallel lane keeps feedback on that physical lane
 // even when the original playing finger subsequently lifts.
 auto feedback=cfg;feedback.latchGrid=4;feedback.keyboardMode=KeyboardMode::normal;s.reset();
 process(s,feedback,.1,{{0,0x91,64,100}});process(s,feedback,.2,{stop(80),off(64)});
 check(s.pendingLatchAction(48)==2&&s.pendingLatchNote(48)==80,"Lifting the original finger moved the parallel stop indicator to the wrong lane");
 // A conventional keyboard on the MPE master channel still releases normally.
 auto conventional=cfg;conventional.keyboardMode=KeyboardMode::normal;s.reset();
 process(s,conventional,.1,{hardware(0x90,48,100)});check(playing(s,48)==1,"Master-channel note-on stopped working");
 process(s,conventional,.2,{hardware(0x80,48,64)});check(!s.hasHeldNotes(),"Master-channel note-off stranded a conventional key");
 // Per-key ribbon edges retain sample timing across host block sizes.
 auto render=[](int block){auto config=fixture();config.latchGrid=4;Scheduler scheduler;std::vector<std::tuple<int,int,int>> result;
  struct Output{int start=0;std::vector<std::tuple<int,int,int>>* events;static void take(void* p,const Event& e){auto& o=*static_cast<Output*>(p);if(e.type==Event::hit||e.type==Event::killContact)o.events->emplace_back(o.start+e.offset,(int)e.type,e.key);}} output{0,&result};
  Midi events[]={top(48),off(48),top(49),off(49),stop(48),stop(49)};const int positions[]={1234,1235,1400,1401,110000,130000};int cursor=0;
  for(int at=0;at<200000;at+=block){const int frames=std::min(block,200000-at);Midi input[6];int count=0;while(cursor<6&&positions[cursor]<at+frames){input[count]=events[cursor];input[count++].offset=positions[cursor++]-at;}output.start=at;scheduler.process(config,{at/24000.,120,48000,true},frames,input,count,Output::take,&output);}check(!scheduler.hasHeldNotes(),"Partitioned ribbon stop left a part");return result;};
 const auto baseline=render(64);for(int block:{1,257,2048})check(render(block)==baseline,"Per-key ribbon timing depends on host buffer size");
 std::cout<<"ribbons: independent top/stop lanes, multi-key toggles, chromatic hardware sweeps, black-key gaps, aliases and quantization passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
