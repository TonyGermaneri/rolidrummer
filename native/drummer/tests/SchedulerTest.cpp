#include <drummer/Scheduler.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <tuple>
using namespace drummer;
void check(bool x,const char* why) { if(!x)throw std::runtime_error(why); }
struct Capture { std::vector<Event> events;static void take(void* ctx,const Event& e){static_cast<Capture*>(ctx)->events.push_back(e);} };
Configuration fixture() {
 Configuration c;
 c.clips={{{},1,4,0,{{0,.1,36,1},{.25,.1,42,.8f},{1,.1,36,.7f},{1.013,.12,42,.5f}}}, {{},2,4,0,{{.5,.1,42,1},{1.5,.1,42,1}}}};
 for(int key:{60,61}) { auto& k=c.keys[(size_t)key];k.enabled=true;k.parts[key==60?36:42]=true;k.zones={Zone{.33f,0},Zone{.67f,1},Zone{1,0}}; }
 for(auto&clip:c.clips)prepareClip(clip);
 return c;
}
std::vector<std::tuple<int,int,float>> render(int block) {
 auto c=fixture();Scheduler s;Capture out;std::vector<std::tuple<int,int,float>> hits;
 const int length=48000;for(int start=0;start<length;start+=block) {
  int n=std::min(block,length-start);Midi midi[2]={{0,0x91,60,127},{0,0x92,61,127}};
  out.events.clear();s.process(c,{start/24000.,120,48000,true},n,midi,start==0?2:0,Capture::take,&out);
  for(auto&e:out.events)if(e.type==Event::hit)hits.emplace_back(start+e.offset,e.key,e.velocity);
 }
 return hits;
}
// Pedal-held owners survive MPE channel reuse, while their last expression is frozen.
std::vector<std::tuple<int,int,int,uint64_t,float>> pedalRender(int block,int cc=64) {
 auto c=fixture();c.launchGrid=0;c.switchGrid=0;c.sustainCC=cc;c.keys[62]=c.keys[61];
 Scheduler scheduler;Capture capture;std::vector<std::tuple<int,int,int,uint64_t,float>> events;
 const Midi input[]={{0,0x91,60,100},{0,0x92,61,90},{500,0xb0,(uint8_t)cc,64},
  {1000,0x81,60,99},{2000,0xb1,74,127},{2100,0x91,62,80},{2500,0x81,62,70},
  {30000,0xb0,(uint8_t)cc,63},{40000,0x82,61,55}};
 size_t mi=0;
 for(int at=0;at<48000;at+=block){const int n=std::min(block,48000-at);std::vector<Midi> midi;
  while(mi<std::size(input)&&input[mi].offset<at+n){auto m=input[mi++];m.offset-=at;midi.push_back(m);}
  capture.events.clear();scheduler.process(c,{at/24000.,120,48000,true},n,midi.data(),(int)midi.size(),Capture::take,&capture);
  for(const auto&e:capture.events)if(e.type==Event::hit||e.type==Event::releaseContact||e.type==Event::expression)
   events.emplace_back(at+e.offset,e.type,e.key,e.owner,e.type==Event::releaseContact?e.values.lift:e.values.slide);
 }
 check(!scheduler.hasHeldNotes(),"Pedal up left a sustained pattern running");return events;
}
void sustainTests(){
 const auto events=pedalRender(64);for(int block:{1,257,2048})check(pedalRender(block)==events,"Pedal timing depends on buffer partition");
 check(pedalRender(257,66)==events,"Reassigned pedal behaves differently from CC64");
 int releases=0;bool sustainedKick=false,reusedChannel=false;
 for(const auto&[at,type,key,owner,value]:events){
  if(type==Event::releaseContact){++releases;check(at==30000||at==40000,"Key released before pedal up");
   if(owner==1)check(at==30000&&value==99/127.f,"Sustain lost original lift velocity");
   if(owner==2)check(at==40000,"Pedal release stopped a physical finger");}
  if(type==Event::hit&&at==24000&&owner==1){sustainedKick=true;check(value==0,"Channel reuse changed sustained expression");}
  if(type==Event::hit&&at>2500&&owner==3)reusedChannel=true;
  check(type!=Event::expression||owner!=1,"Expression was routed to a pedal-held owner");
 }
 check(releases==3&&sustainedKick&&reusedChannel,"Sustain lost notes on channel reuse");
 auto c=fixture();Scheduler s;Capture out;
 // Member pedals hold only their own channel; global release respects a local pedal.
 Midi start[]={{0,0x91,60,100},{0,0x92,61,100},{1,0xb1,64,127},{2,0x81,60,64},{3,0x82,61,64}};
 s.process(c,{0,120,48000,true},64,start,5,Capture::take,&out);
 check(s.hasHeldNotes(),"Member pedal did not hold its note");
 check(std::count_if(out.events.begin(),out.events.end(),[](const Event&e){return e.type==Event::releaseContact;})==1,"Member pedal held another channel");
 out.events.clear();Midi up{0,0xb0,64,0};s.process(c,{64/24000.,120,48000,true},64,&up,1,Capture::take,&out);
 check(s.hasHeldNotes(),"Master pedal up overrides a held member pedal");
 c.sustainCC=66;out.events.clear();s.process(c,{128/24000.,120,48000,true},64,nullptr,0,Capture::take,&out);
 check(!s.hasHeldNotes()&&out.events.front().type==Event::releaseContact,"Reassignment stranded a latched note");
 s.reset();c.sustainCC=64;s.process(c,{0,120,48000,true},64,start,5,Capture::take,&out);
 Midi panic{0,0xb0,123,0};s.process(c,{64/24000.,120,48000,true},64,&panic,1,Capture::take,&out);
 check(!s.hasHeldNotes(),"Panic retained pedal-held notes");
 // Repeated same-key strikes replace the prior latch; pedal up releases the final owner.
 s.reset();out.events.clear();std::vector<Midi> many{{0,0xb0,64,127}};
 for(int i=0;i<140;++i){many.push_back({i*2,0x91,60,100});many.push_back({i*2+1,0x81,60,64});}
 many.push_back({300,0xb0,64,0});s.process(c,{0,120,48000,true},512,many.data(),(int)many.size(),Capture::take,&out);
 check(!s.hasHeldNotes(),"Sustain capacity eviction stranded notes");
 check(std::count_if(out.events.begin(),out.events.end(),[](const Event&e){return e.type==Event::releaseContact||e.type==Event::killContact;})==140,"Sustain capacity lost owner releases");
}
int playing(const Scheduler& scheduler,int key=-1){
 int count=0;for(const auto& f:scheduler.contacts())if(f.held&&(key<0||f.key==key))++count;
 for(const auto& f:scheduler.sustainedContacts())if(f.held&&(key<0||f.key==key))++count;return count;
}
void singleZoneTests(){
 for(auto mode:{KeyboardMode::normal,KeyboardMode::hold,KeyboardMode::toggle})for(bool lifted:{false,true})for(bool pedal:{false,true})for(bool different:{false,true}){
  auto c=fixture();c.keyboardMode=mode;c.launchGrid=c.switchGrid=0;Scheduler scheduler;Capture capture;int at=0;
  auto run=[&](std::initializer_list<Midi> midi){capture.events.clear();scheduler.process(c,{at/24000.,120,48000,true},64,midi.begin(),(int)midi.size(),Capture::take,&capture);at+=64;};
  run({{0,0xb0,64,(uint8_t)(pedal?127:0)},{0,0x91,60,90},{1,0x93,61,100}});
  const auto old=scheduler.contacts()[1].owner;
  if(lifted)run({{0,0x81,60,80}});
  const bool existed=!lifted||pedal||mode!=KeyboardMode::normal;
  run({{0,0xb2,74,(uint8_t)(different?127:0)},{1,0x92,60,110}});
  const bool active=!(mode==KeyboardMode::toggle&&existed&&!different);
  check(playing(scheduler,60)==int(active)&&playing(scheduler,61)==1,"A key retained multiple zones or changed another key");
  const auto current=scheduler.contacts()[2].owner;
  check(!scheduler.contacts()[1].held&&(!active||scheduler.contacts()[2].zone==(different?2:0)),"Newest key press did not own its selected zone");
  if(existed)check(std::any_of(capture.events.begin(),capture.events.end(),[&](const Event&e){return e.type==Event::killContact&&e.owner==old;}),"Replacement did not stop the old zone owner");
  auto edited=c;edited.keys[60].zones[0].clip=1;
  scheduler.reconfigure(c,edited,at/24000.,Capture::take,&capture);c=edited;
  check(playing(scheduler,60)==int(active),"A pattern edit revived a replaced physical finger");
  run({{0,0xb1,74,64},{1,0xd1,127,0},{2,0x81,60,64}});
  check(playing(scheduler,60)==int(active)&&(!active||scheduler.contacts()[2].owner==current),"Stale member input stopped a replacement");
  check(std::none_of(capture.events.begin(),capture.events.end(),[&](const Event&e){return (e.type==Event::expression||e.type==Event::switchZone)&&e.owner==old;}),"Stale expression reached a replaced owner");
  run({{0,0x82,60,64},{1,0xb0,64,0}});
  check(playing(scheduler,60)==0&&scheduler.contacts()[3].held,"Pedal up retained a released zone or stopped another physical key");
 }
}
void keyboardModeTests(){
 auto c=fixture();c.launchGrid=0;c.switchGrid=0;c.keyboardMode=KeyboardMode::toggle;
 Scheduler scheduler;Capture capture;int at=0;
 auto run=[&](std::initializer_list<Midi> input){capture.events.clear();scheduler.process(c,{at/24000.,120,48000,true},64,input.begin(),(int)input.size(),Capture::take,&capture);at+=64;};
 run({{0,0x91,60,100},{1,0x81,60,99},{2,0x92,61,90}});
 check(playing(scheduler,60)==1&&playing(scheduler,61)==1,"Toggle did not latch independently per key");
 const auto first=scheduler.sustainedContacts()[0];
 check(first.toggled&&!first.down&&first.releaseVelocity==99/127.f,"Toggle lost latch telemetry or Lift");
 // A new member channel pressing the same zone stops only this key, even under pedal.
 run({{0,0xb0,64,127},{1,0x93,60,100}});
 check(playing(scheduler,60)==0&&playing(scheduler,61)==1,"Same-zone toggle failed or stopped another key");
 check(scheduler.contacts()[3].down&&!scheduler.contacts()[3].toggled&&scheduler.contacts()[3].suppressed,"Stopped toggle lost physical/suppressed state");
 check(std::count_if(capture.events.begin(),capture.events.end(),[&](const auto&e){return e.type==Event::killContact&&e.owner==first.owner;})==1,"Toggle off did not stop the previous owner");
 auto edited=c;edited.keys[60].zones[0].clip=1;
 scheduler.reconfigure(c,edited,at/24000.,Capture::take,&capture);
 check(playing(scheduler,60)==0,"A pattern edit resurrected an explicitly toggled-off touch");
 run({{0,0x83,60,64},{1,0xb0,64,0}});
 check(playing(scheduler,60)==0&&scheduler.contacts()[2].held,"Pedal up stopped a physically held key");
 // Different-zone presses replace the old owner immediately, with no duplicated gate.
 run({{0,0xb3,74,127},{1,0x93,60,100},{2,0x83,60,64}});
 const auto old=scheduler.sustainedContacts()[0].owner;
 run({{0,0xb4,74,64},{1,0x94,60,100}});
 check(playing(scheduler,60)==1&&scheduler.contacts()[4].held&&scheduler.contacts()[4].zone==1&&scheduler.contacts()[4].toggled,"A different zone failed to replace the latched owner");
 check(std::any_of(capture.events.begin(),capture.events.end(),[&](const auto&e){return e.type==Event::killContact&&e.owner==old;}),"Zone replacement left old voices owned by the key");
 // Overlap while both fingers remain down: stale releases/pressure cannot affect the replacement.
 run({{0,0xb5,74,0},{1,0x95,60,100},{2,0xd4,127,0},{3,0x84,60,64}});
 check(playing(scheduler,60)==1&&scheduler.contacts()[5].held&&scheduler.contacts()[5].zone==0&&scheduler.contacts()[5].values.press==0,"An old finger changed/stopped the replacement");
 scheduler.reconfigure(c,edited,at/24000.,Capture::take,&capture);
 check(playing(scheduler,60)==1,"Retargeting a toggled key duplicated its owner");
 run({{0,0x85,60,64}});
 // A member pedal release is scoped; master release clears software latches even without prior down.
 run({{0,0xb2,64,0}});check(playing(scheduler,60)==1,"Member pedal release cleared another key's software latch");
 run({{0,0xb0,64,0}});check(playing(scheduler,60)==0&&scheduler.contacts()[2].held&&scheduler.contacts()[2].toggled,"Pedal release lost physical Toggle state");
 run({{0,0x82,61,64}});check(playing(scheduler,61)==1,"A physical key could not latch after pedal release");
 c.keyboardMode=KeyboardMode::normal;run({});check(!scheduler.hasHeldNotes(),"Leaving Toggle stranded a software latch");
 // Hold behaves like sustain, including retriggers rather than toggle-off.
 c.keyboardMode=KeyboardMode::hold;
 run({{0,0x91,60,100},{1,0x81,60,64},{2,0x91,60,100},{3,0x81,60,64},{4,0x92,61,100}});
 check(playing(scheduler,60)==1&&scheduler.contacts()[2].held,"Hold did not retrigger with one owner per key");
 run({{0,0xb0,64,0}});check(playing(scheduler,60)==0&&scheduler.contacts()[2].held,"Hold pedal release failed to retain the physical key");
 run({{0,0x82,61,64}});c.keyboardMode=KeyboardMode::normal;run({});check(!scheduler.hasHeldNotes(),"Leaving Hold stranded a latch");
 // A real pedal retains the latest owner when switching modes.
 run({{0,0xb0,64,127},{1,0x91,60,100},{2,0x81,60,64},{3,0x93,60,100}});
 check(playing(scheduler,60)==1,"Normal mode retained an overlapping owner for the same key");
 c.keyboardMode=KeyboardMode::toggle;run({});check(playing(scheduler,60)==1&&scheduler.contacts()[3].toggled,"Entering Toggle did not select one newest owner per key");
 run({{0,0x83,60,64}});c.keyboardMode=KeyboardMode::normal;run({});check(playing(scheduler,60)==1,"Mode change ignored a real sustain pedal");
 run({{0,0xb0,64,0}});check(!scheduler.hasHeldNotes(),"Real pedal release stranded a mode-change owner");
 c.keyboardMode=KeyboardMode::toggle;run({{0,0x91,60,100},{1,0x81,60,64},{2,0x92,61,100},{3,0x82,61,64}});
 run({{0,0xb1,120,0}});check(playing(scheduler,60)==0&&playing(scheduler,61)==1,"Member all-sound-off leaked between toggle keys");
 run({{0,0xb0,123,0}});check(!scheduler.hasHeldNotes(),"Panic left toggled keys running");
 // The new press's Strike is used when Strike selects the zone.
 c.keys[60].zoneSource=0;run({{0,0x91,60,10},{1,0x81,60,64},{2,0x92,60,127}});
 check(playing(scheduler,60)==1&&scheduler.contacts()[2].zone==2,"Strike-based zone replacement used the old velocity");
 c.sustainCC=66;run({});check(scheduler.contacts()[2].held,"Pedal reassignment stopped a physical toggle");
 run({{0,0x82,60,64},{1,0xb0,64,0}});check(playing(scheduler,60)==1,"Unassigned CC64 cleared a reassigned toggle pedal");
 run({{0,0xb0,66,0}});check(!scheduler.hasHeldNotes(),"Reassigned pedal failed to clear Toggle");
 scheduler.reset();check(!scheduler.hasHeldNotes(),"Reset retained keyboard mode contacts");
}
std::vector<std::tuple<int,int,int,uint64_t,int>> keyboardModeRender(KeyboardMode mode,int block){
 auto c=fixture();c.keyboardMode=mode;c.launchGrid=0;c.switchGrid=.25;
 Scheduler scheduler;Capture capture;std::vector<std::tuple<int,int,int,uint64_t,int>> result;
 const Midi input[]={{0,0x91,60,100},{0,0x92,61,100},{100,0x81,60,64},
  {20000,0x93,60,100},{20001,0x83,60,64},{24000,0xb3,74,127},{24001,0x93,60,100},{24100,0x83,60,64},
  {30000,0xb4,74,64},{30001,0x94,60,100},{30100,0x84,60,64},{32000,0xb0,64,0},
  {40000,0x82,61,64},{41000,0xb0,64,0},{45000,0xb0,123,0}};
 size_t index=0;
 for(int at=0;at<48000;at+=block){const int n=std::min(block,48000-at);std::vector<Midi> midi;
  while(index<std::size(input)&&input[index].offset<at+n){auto m=input[index++];m.offset-=at;midi.push_back(m);}
  capture.events.clear();scheduler.process(c,{at/24000.,120,48000,true},n,midi.data(),(int)midi.size(),Capture::take,&capture);
  for(const auto&e:capture.events)result.emplace_back(at+e.offset,e.type,e.key,e.owner,e.drumNote);
 }
 check(!scheduler.hasHeldNotes(),"Keyboard mode render retained notes");return result;
}
void reconfigureTests(){
 auto original=fixture();original.launchGrid=0;original.switchGrid=1;
 original.clips[0].notes={{0,1.25,36,1}};prepareClip(original.clips[0]);
 original.keys[60].zoneCount=1;original.keys[60].intentEnabled=true;original.keys[60].intentWeights={{0,1,0,0}};
 original.keys[60].intentSlewMs=0;original.keys[60].intentDwell=0;original.keys[60].intentPenalty=0;original.keys[60].zones[0]={1,0,0};
 PatternGroup group;group.count=2;group.candidates[0]={0,{{0,0,0,0}}};group.candidates[1]={1,{{1,1,1,1}}};original.groups.push_back(group);
 Scheduler scheduler;Capture capture;Midi start[]={{0,0x91,60,100},{2400,0xd1,127,0}};
 scheduler.process(original,{0,120,48000,true},12000,start,2,Capture::take,&capture);
 const auto owner=scheduler.contacts()[1].owner;const auto token=std::find_if(capture.events.begin(),capture.events.end(),[](const Event&e){return e.type==Event::hit;})->token;
 check(scheduler.contacts()[1].clip==0&&scheduler.contacts()[1].wantedClip==1&&scheduler.contacts()[1].switches==1,"Refresh fixture lacks a pending candidate transition");
 auto next=original;std::swap(next.clips[0],next.clips[1]);next.groups[0].candidates[0].clip=1;next.groups[0].candidates[1].clip=0;next.keys[60].zones[0].clip=1;
 capture.events.clear();scheduler.reconfigure(original,next,.5,Capture::take,&capture);
 const auto& contact=scheduler.contacts()[1];
 check(contact.owner==owner&&contact.clip==1&&contact.wantedClip==0&&contact.switches==1,"Runtime remapping lost stable clip identity or its pending same-region switch");
 check(capture.events.empty(),"Reordering unchanged candidates released the playing owner");
 scheduler.process(next,{.5,120,48000,true},24000,nullptr,0,Capture::take,&capture);
 check(scheduler.contacts()[1].clip==0,"Pending transition was not performed after runtime remapping");
 const auto ending=std::find_if(capture.events.begin(),capture.events.end(),[&](const Event&e){return e.type==Event::endHit&&e.token==token;});
 check(ending!=capture.events.end()&&ending->offset==18000,"Remapping lost an old runtime's outstanding note ending");
 const auto change=std::find_if(capture.events.begin(),capture.events.end(),[](const Event&e){return e.type==Event::switchZone;});
 check(change!=capture.events.end()&&change->offset==12000&&change->owner==owner,"Runtime remapping changed transition phase or owner");
 auto disabled=next;disabled.keys[60].enabled=false;capture.events.clear();scheduler.reconfigure(next,disabled,1.5,Capture::take,&capture);
 scheduler.process(disabled,{1.5,120,48000,true},64,nullptr,0,Capture::take,&capture);
 check(!scheduler.contacts()[1].held&&scheduler.contacts()[1].down&&std::any_of(capture.events.begin(),capture.events.end(),[&](const Event&e){return e.type==Event::releaseContact&&e.owner==owner;}),"Removing a mapped key stranded its owner");
 capture.events.clear();scheduler.reconfigure(disabled,next,1.5+64/24000.,Capture::take,&capture);
 check(scheduler.contacts()[1].held&&scheduler.contacts()[1].owner==owner,"Remapping an already physical key lost its contact");
}
void intentTests(){
 Configuration config;config.launchGrid=0;config.switchGrid=0;
 PatternGroup variations;variations.count=14;
 for(int i=0;i<14;++i){Clip clip;clip.id=(uint32_t)i+1;clip.length=4;clip.notes={{0,.1,42,.1f+i*.05f}};prepareClip(clip);config.clips.push_back(clip);variations.candidates[(size_t)i].clip=i;variations.candidates[(size_t)i].position[2]=i/13.f;}
 config.groups.push_back(variations);
 auto& key=config.keys[60];key.enabled=true;key.parts[42]=true;key.zoneCount=1;key.zones[0]={1,0,0};key.intentEnabled=true;key.intentWeights={{0,0,1,0}};key.intentPenalty=0;key.intentDwell=0;key.intentSlewMs=0;
 // Fourteen fills remain reachable in one slide region, beyond eight zones.
 for(int i=0;i<14;++i){Scheduler scheduler;Capture out;const int raw=(int)std::llround(i*16383./13);
  Midi input[]={{0,0xe1,(uint8_t)(raw&127),(uint8_t)(raw>>7)},{0,0x91,60,100}};
  scheduler.process(config,{0,120,48000,true},128,input,2,Capture::take,&out);
  check(scheduler.contacts()[1].clip==i,"Glide cannot reach a candidate within its region");
  check(scheduler.evaluations==1,"A candidate group evaluated inactive alternatives");
  const auto hit=std::find_if(out.events.begin(),out.events.end(),[](const Event&e){return e.type==Event::hit;});
  check(hit!=out.events.end()&&std::abs(hit->velocity-(.1f+i*.05f))<1e-6,"Selected candidate did not reach the drum gate");
 }
 config.groups[0].count=2;config.groups[0].candidates[0].position={{0,0,.5f,0}};config.groups[0].candidates[1].position={{1,1,.5f,1}};
 key.intentWeights={{1,0,0,0}};config.keys[61]=key;config.switchGrid=.25;
 Scheduler scheduler;Capture out;Midi press[]={{0,0x91,60,64},{0,0x92,61,64},{100,0xd1,127,0}};
 scheduler.process(config,{0,120,48000,true},12000,press,3,Capture::take,&out);
 check(scheduler.contacts()[1].clip==1&&scheduler.contacts()[2].clip==0,"Pressure did not select independently per MPE contact");
 const auto change=std::find_if(out.events.begin(),out.events.end(),[](const Event&e){return e.type==Event::switchZone;});
 check(change!=out.events.end()&&change->offset==6000&&change->key==60,"Candidate transition lost musical quantization");
 // A separate slide region is a hard constraint, even under maximum pressure.
 config.keys[60].zoneCount=2;config.keys[60].zones[0].upper=.7f;config.keys[60].zones[1]={1,13,-1};scheduler.reset();out.events.clear();
 Midi fill[]={{0,0xb1,74,127},{0,0x91,60,127},{1,0xd1,127,0}};
 scheduler.process(config,{0,120,48000,true},128,fill,3,Capture::take,&out);
 check(scheduler.contacts()[1].zone==1&&scheduler.contacts()[1].clip==13,"Pressure escaped the selected fill region");
 config.keys[60].zoneCount=1;config.keys[60].zones[0]={1,0,0};config.keys[60].intentWeights={{0,0,0,1}};config.keys[61]=config.keys[60];scheduler.reset();out.events.clear();
 Midi strike[]={{0,0x91,60,1},{0,0x92,61,127}};
 scheduler.process(config,{0,120,48000,true},128,strike,2,Capture::take,&out);
 check(scheduler.contacts()[1].clip==0&&scheduler.contacts()[2].clip==1,"Strike did not establish initial variation intent");
}
int main() {try {
 sustainTests();
 singleZoneTests();
 keyboardModeTests();
 for(auto mode:{KeyboardMode::normal,KeyboardMode::hold,KeyboardMode::toggle}){const auto events=keyboardModeRender(mode,64);for(int block:{1,257,2048})check(keyboardModeRender(mode,block)==events,"Keyboard mode timing depends on host buffer size");}
 intentTests();
 reconfigureTests();
 check(render(64)==render(257),"Timing depends on host buffer size");
 auto hits=render(64);check(hits.size()==4,"Expected isolated kick and hat events");
 check(std::get<0>(hits.back())==24312,"Human timing was quantized away");
 auto c=fixture();Scheduler s;Capture out;
 s.process(c,{0,120,48000,true},12000,nullptr,0,Capture::take,&out);
 for(auto&e:out.events)check(e.type!=Event::hit,"Unheld key sounded");
 check(s.evaluations==0,"Idle contacts evaluated unrelated clip timelines");
 // Active contacts share one bus. Pending note endings survive removing the
 // source clip from a prepared runtime and releasing the last physical key.
 {
  auto active=fixture();active.launchGrid=0;active.clips[0].notes={{0,2,36,1}};prepareClip(active.clips[0]);
  Scheduler scheduler;Capture capture;
  Midi start{0,0x91,60,100};scheduler.process(active,{0,120,48000,true},64,&start,1,Capture::take,&capture);
  check(scheduler.evaluations==1,"An active key evaluated unrelated candidate timelines");
  const auto onset=std::find_if(capture.events.begin(),capture.events.end(),[](const Event&e){return e.type==Event::hit;});
  check(onset!=capture.events.end(),"Long-note fixture did not trigger");const auto heldToken=onset->token;
  active.clips.clear();capture.events.clear();Midi up{0,0x81,60,80};
  scheduler.process(active,{64/24000.,120,48000,true},48000,&up,1,Capture::take,&capture);
  check(scheduler.evaluations==0,"Removed clips were still evaluated");
  const auto ending=std::find_if(capture.events.begin(),capture.events.end(),[&](const Event&e){return e.type==Event::endHit&&e.token==heldToken;});
  check(ending!=capture.events.end()&&ending->offset==48000-64,"Changing the prepared clip bus lost an outstanding note-off");
 }
 // Long durations must not overflow a sample-offset integer and end immediately.
 {
  auto longClip=fixture();longClip.launchGrid=0;longClip.clips[0].notes={{0,100000,36,1}};prepareClip(longClip.clips[0]);
  Scheduler scheduler;Capture capture;Midi start{0,0x91,60,100};
  scheduler.process(longClip,{0,120,48000,true},128,&start,1,Capture::take,&capture);
  check(std::none_of(capture.events.begin(),capture.events.end(),[](const Event&e){return e.type==Event::endHit;}),"A distant note-off overflowed into the current block");
 }
 s.reset();out.events.clear();
 Midi early[]={{100,0x91,60,127},{200,0x81,60,64}};
 s.process(c,{0,120,48000,true},30000,early,2,Capture::take,&out);
 for(auto&e:out.events)check(e.type!=Event::hit,"Released key launched later");
 s.reset();out.events.clear();
 Midi slide[]={{0,0x91,60,127},{0,0x92,61,127},{7000,0xb2,74,64}};
 s.process(c,{0,120,48000,true},48000,slide,3,Capture::take,&out);
 std::vector<std::pair<int,int>> expected{{0,60},{6000,61},{12000,61},{24000,60},{36000,61}},actual;
 for(auto&e:out.events)if(e.type==Event::hit)actual.emplace_back(e.offset,e.key);
 check(actual==expected,"Zone switch lost phase or affected another key");
 const auto first=s.contacts()[2].owner;out.events.clear();
 Midi reuse[]={{0,0x82,61,99},{1,0x92,61,127}};
 s.process(c,{2,120,48000,true},64,reuse,2,Capture::take,&out);
 check(s.contacts()[2].owner!=first,"MPE channel reuse retained owner");
 check(out.events[0].values.lift==99/127.f,"Release velocity discarded");
 s.reset();out.events.clear();Midi down{0,0x91,60,127};
 s.process(c,{-.25,120,48000,true},12000,&down,1,Capture::take,&out);
 bool atZero=false;for(auto&e:out.events)if(e.type==Event::hit&&e.offset==6000)atZero=true;
 check(atZero,"Negative PPQ did not wrap correctly");
 // Hits straddling host block edges retain their nearest-sample timestamps.
 auto edgeRender=[](int block){auto config=fixture();config.launchGrid=0;config.clips.resize(1);auto& clip=config.clips[0];clip.notes.clear();clip.length=.031;
  for(double sample:{0.,63.49,63.5,63.8,64.2,127.8,256.8,400.1})clip.notes.push_back({sample/24000.,.00001,36,1});prepareClip(clip);
  Scheduler engine;Capture capture;std::vector<int> times;
  for(int start=0;start<2000;start+=block){capture.events.clear();Midi note{0,0x91,60,100};engine.process(config,{start/24000.,120,48000,true},std::min(block,2000-start),&note,start==0?1:0,Capture::take,&capture);for(auto&e:capture.events)if(e.type==Event::hit)times.push_back(start+e.offset);}
  return times;
 };
 const auto edges=edgeRender(257);for(int block:{1,7,32,64,127,511})check(edgeRender(block)==edges,"Nearest-sample events move at host block boundaries");
 check(std::count(edges.begin(),edges.end(),64)==3,"Rounded events at sample 64 are missing or duplicated");
 // Even sub-sample MIDI durations produce an off after their own onset.
 auto shortNotes=fixture();shortNotes.clips.resize(1);shortNotes.clips[0].notes={{0,.000001,36,1},{63.8/24000.,.000001,36,1}};prepareClip(shortNotes.clips[0]);s.reset();out.events.clear();
 std::vector<Event> shortEvents;
 for(int start=0;start<128;start+=64){out.events.clear();s.process(shortNotes,{start/24000.,120,48000,true},64,&down,start==0?1:0,Capture::take,&out);for(auto event:out.events){event.offset+=start;shortEvents.push_back(event);}}
 for(const auto& on:shortEvents)if(on.type==Event::hit){const auto off=std::find_if(shortEvents.begin(),shortEvents.end(),[&](const Event&e){return e.type==Event::endHit&&e.token==on.token;});check(off!=shortEvents.end()&&off->offset==on.offset+1,"Sub-sample note left a held voice without its off");}
 // Seek, loop and stop flush old voices while keeping held-part gates in phase.
 s.reset();out.events.clear();s.process(c,{0,120,48000,true},64,&down,1,Capture::take,&out);
 out.events.clear();s.process(c,{8,120,48000,true},64,nullptr,0,Capture::take,&out);check(out.events.front().type==Event::panic,"Seek leaves old voices playing");check(std::any_of(out.events.begin(),out.events.end(),[](const Event&e){return e.type==Event::hit&&e.offset==0;}),"Seek did not retain the held part at the new song phase");
 out.events.clear();s.process(c,{0,120,48000,true},64,nullptr,0,Capture::take,&out);check(out.events.front().type==Event::panic,"Loop jump leaves old voices playing");
 out.events.clear();s.process(c,{0,120,48000,false},64,nullptr,0,Capture::take,&out);check(out.events.front().type==Event::panic,"Transport stop leaves old voices playing");for(auto&e:out.events)check(e.type!=Event::hit,"Stopped transport triggered a clip");
 out.events.clear();s.process(c,{0,120,48000,true},64,nullptr,0,Capture::take,&out);check(std::any_of(out.events.begin(),out.events.end(),[](const Event&e){return e.type==Event::hit;}),"Restart loses a held contact");
 // Member-channel resets and all five dimensions stay on their own contact.
 s.reset();out.events.clear();Midi mpe[]={{0,0x91,60,90},{0,0x92,61,110},{1,0xe1,0,64},{2,0xb1,74,100},{3,0xd1,80,0},{4,0xb1,123,0},{5,0xb0,107,70}};
 s.process(c,{0,120,48000,true},64,mpe,7,Capture::take,&out);
 check(!s.contacts()[1].held&&s.contacts()[2].held,"Member all-notes-off stopped another key");check(s.contacts()[1].values.glide==.5f,"Centered pitch bend is not neutral");check(s.contacts()[1].values.slide==100/127.f&&s.contacts()[1].values.press==80/127.f,"MPE dimensions were lost");check(s.contacts()[2].values.slide==0&&s.contacts()[2].values.press==0,"Expression leaked between channels");check(std::any_of(out.events.begin(),out.events.end(),[](const Event&e){return e.type==Event::globalCC&&e.cc==107;}),"Master controller was not forwarded");
 // CC74 belongs only to its receiving channel, including pre-note values.
 for(int channel=0;channel<16;++channel){
  auto lower=fixture();lower.switchGrid=0;lower.launchGrid=0;s.reset();out.events.clear();
  const int other=channel==1?2:1;
  Midi start[]={{0,(uint8_t)(0xb0|channel),74,127},{0,(uint8_t)(0x90|channel),60,100},{0,(uint8_t)(0x90|other),61,100}};
  s.process(lower,{0,120,48000,true},64,start,3,Capture::take,&out);
  check(s.contacts()[channel].zone==2,"Pre-note member CC74 lost");
  const auto owner=s.contacts()[channel].owner;
  Midi move{0,(uint8_t)(0xb0|channel),74,64};
  s.process(lower,{64/24000.,120,48000,true},64,&move,1,Capture::take,&out);
  check(s.contacts()[channel].held&&s.contacts()[channel].zone==1&&s.contacts()[channel].owner==owner,"Held member slide requires retrigger");
  check(s.contacts()[other].zone==0&&s.contacts()[other].values.slide==0,"Member slide affected another finger");
  move.b=0;s.process(lower,{128/24000.,120,48000,true},64,&move,1,Capture::take,&out);
  check(s.contacts()[channel].zone==0,"Downward slide failed to return to lower zone");
 }
 s.reset();out.events.clear();Midi masterSlide[]={{0,0xb0,74,127},{0,0x91,60,100},{0,0x9f,61,100}};
 s.process(c,{0,120,48000,true},64,masterSlide,3,Capture::take,&out);
 check(s.contacts()[1].zone==0&&s.contacts()[15].zone==0,"Master CC74 was broadcast into member contacts");
 check(std::none_of(out.events.begin(),out.events.end(),[](const Event&e){return e.type==Event::globalCC&&e.cc==74;}),"CC74 escaped into global controllers");

 Midi unassigned{0,0x93,99,100};s.process(c,{64/24000.,120,48000,true},64,&unassigned,1,Capture::take,&out);
 check(s.contacts()[3].down&&!s.contacts()[3].held,"Unassigned physical finger missing from telemetry");
 unassigned.status=0x83;s.process(c,{128/24000.,120,48000,true},64,&unassigned,1,Capture::take,&out);check(!s.contacts()[3].down,"Unassigned finger remains after note off");
 // Overflow is bounded and reports a panic instead of a partial, stuck pattern.
 auto dense=fixture();dense.clips.resize(1);dense.clips[0].notes.assign(Scheduler::capacity+1,{0,.1,36,1});prepareClip(dense.clips[0]);s.reset();out.events.clear();s.process(dense,{0,120,48000,true},64,&down,1,Capture::take,&out);check(s.overflow&&out.events.size()==1&&out.events.front().type==Event::panic,"Dense clip overflow did not fail coherently");
 std::cout<<"scheduler: buffer invariance, precision, silence, gating, independent zones, ownership, lift, preroll passed\n";
 return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
