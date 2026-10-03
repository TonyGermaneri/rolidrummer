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
int main() {try {
 check(render(64)==render(257),"Timing depends on host buffer size");
 auto hits=render(64);check(hits.size()==4,"Expected isolated kick and hat events");
 check(std::get<0>(hits.back())==24312,"Human timing was quantized away");
 auto c=fixture();Scheduler s;Capture out;
 s.process(c,{0,120,48000,true},12000,nullptr,0,Capture::take,&out);
 for(auto&e:out.events)check(e.type!=Event::hit,"Unheld key sounded");
 check(s.evaluations==2,"Clip not evaluated exactly once");
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
 // Same note number on two channels still has two separate expression owners.
 {auto isolated=fixture();isolated.switchGrid=0;isolated.launchGrid=0;s.reset();out.events.clear();
  Midi start[]={{0,0xb1,74,0},{0,0x91,60,100},{0,0xb2,74,127},{0,0x92,60,100}};
  s.process(isolated,{0,120,48000,true},64,start,4,Capture::take,&out);
  const auto ownerA=s.contacts()[1].owner,ownerB=s.contacts()[2].owner;check(ownerA!=ownerB,"Overlapping note pitches share expression ownership");
  out.events.clear();Midi change[]={{0,0xb1,74,64},{1,0xb0,74,0}};
  s.process(isolated,{64/24000.,120,48000,true},64,change,2,Capture::take,&out);
  check(s.contacts()[1].zone==1&&s.contacts()[2].zone==2,"Slide crossed channel boundaries for the same note");
  for(const auto&e:out.events)if(e.type==Event::expression||e.type==Event::switchZone)check(e.owner==ownerA,"Slide modified another voice owner");
  Midi reuse[]={{0,0x81,60,64},{1,0xb1,74,0},{2,0x91,61,100}};
  s.process(isolated,{128/24000.,120,48000,true},64,reuse,3,Capture::take,&out);
  check(s.contacts()[1].key==61&&s.contacts()[1].zone==0&&s.contacts()[1].owner!=ownerA,"Channel reuse failed to bind expression to the new note");
  check(s.contacts()[2].key==60&&s.contacts()[2].zone==2&&s.contacts()[2].owner==ownerB,"Channel reuse changed the other held note");
 }

 Midi unassigned{0,0x93,99,100};s.process(c,{64/24000.,120,48000,true},64,&unassigned,1,Capture::take,&out);
 check(s.contacts()[3].down&&!s.contacts()[3].held,"Unassigned physical finger missing from telemetry");
 unassigned.status=0x83;s.process(c,{128/24000.,120,48000,true},64,&unassigned,1,Capture::take,&out);check(!s.contacts()[3].down,"Unassigned finger remains after note off");
 // Overflow is bounded and reports a panic instead of a partial, stuck pattern.
 auto dense=fixture();dense.clips.resize(1);dense.clips[0].notes.assign(Scheduler::capacity+1,{0,.1,36,1});prepareClip(dense.clips[0]);s.reset();out.events.clear();s.process(dense,{0,120,48000,true},64,&down,1,Capture::take,&out);check(s.overflow&&out.events.size()==1&&out.events.front().type==Event::panic,"Dense clip overflow did not fail coherently");
 std::cout<<"scheduler: buffer invariance, precision, silence, gating, independent zones, ownership, lift, preroll passed\n";
 return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
