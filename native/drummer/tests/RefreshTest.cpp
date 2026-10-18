#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace realtime {
thread_local bool watching=false;
thread_local size_t allocations=0,deletions=0;
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

void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
struct Head:juce::AudioPlayHead{
 double beat=0,bpm=120;bool playing=true;
 juce::Optional<PositionInfo> getPosition()const override{PositionInfo p;p.setBpm(bpm);p.setPpqPosition(beat);p.setIsPlaying(playing);return p;}
};
juce::var fixture(bool refreshed){
 auto document=drummer::defaultDocument();
 auto keys=document.getProperty("keys",{});auto key=(*keys.getArray())[0].clone();
 key.getDynamicObject()->setProperty("routes",juce::Array<juce::var>{});
 key.getDynamicObject()->setProperty("zones",juce::JSON::parse(R"([{"upper":1,"clipId":"pattern"}])"));
 document.getDynamicObject()->setProperty("keys",juce::Array<juce::var>{key});
 document.getDynamicObject()->setProperty("launchGrid",0.);document.getDynamicObject()->setProperty("switchGrid",0.);
 auto clip=juce::JSON::parse(R"({"id":"pattern","name":"Pattern","length":4,"origin":0,"notes":[]})");
 juce::Array<juce::var> notes;
 for(int i=0;i<(refreshed?8:4);++i){auto* note=new juce::DynamicObject;note->setProperty("beat",i*(refreshed?.5:1));note->setProperty("duration",1.25);note->setProperty("note",36);note->setProperty("velocity",refreshed?.85:.3);notes.add(note);}
 clip.getDynamicObject()->setProperty("notes",notes);document.getDynamicObject()->setProperty("clips",juce::Array<juce::var>{clip});return document;
}
void process(DrummerProcessor& player,juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi){
 realtime::watching=true;player.processBlock(audio,midi);realtime::watching=false;
 check(realtime::allocations==0&&realtime::deletions==0,"Performance refresh allocated or freed memory on audio");
 check(!player.overflow.load(),"Performance refresh overflowed the scheduler");
}
std::vector<float> render(int block,double grid,bool quantized,bool pedal,bool seek,bool playing=true,double bpm=120,bool boundaryMidi=false,bool backwards=false){
 constexpr int request=23456,total=128000;
 const double samplesPerBeat=60*48000/bpm;
 const int boundary=seek?(backwards?118000:76000):(int)std::llround(grid*samplesPerBeat);
 DrummerProcessor player;player.apply(drummer::compileDocument(fixture(false)));player.prepareToPlay(48000,2048);
 Head head;head.playing=playing;head.bpm=bpm;player.setPlayHead(&head);juce::AudioBuffer<float> audio(2,2048);juce::MidiBuffer midi;
 std::vector<float> result;result.reserve(total*2);
 const int points[]={1000,2000,request,request+113,40000,total};
 for(int start=0;start<total;){
  if(start==(quantized?request:boundary)){
   player.apply(drummer::compileDocument(fixture(true)),{},quantized?grid:0);
   check(player.activationPending()==quantized,"Refresh acknowledgement lost its boundary policy");
  }
  if(quantized&&start==request+113){auto edit=fixture(true);edit.getDynamicObject()->setProperty("name","Edited while waiting");player.apply(drummer::compileDocument(edit),{},0,true);check(player.activationPending(),"Ordinary edit cancelled a prepared boundary");}
  int n=std::min(block,total-start);for(int point:points)if(point>start)n=std::min(n,point-start);
  if(!quantized&&boundary>start)n=std::min(n,boundary-start);
  audio.setSize(2,n,false,false,true);midi.clear();
  if(start==0)midi.addEvent(juce::MidiMessage::noteOn(2,48,(juce::uint8)100),0);
  if(pedal&&start==1000)midi.addEvent(juce::MidiMessage::controllerEvent(1,64,127),0);
  if(pedal&&start==2000)midi.addEvent(juce::MidiMessage::noteOff(2,48,(juce::uint8)95),0);
  if(boundaryMidi&&start<=boundary&&start+n>boundary){midi.addEvent(juce::MidiMessage::noteOff(2,48),boundary-start);midi.addEvent(juce::MidiMessage::noteOn(3,48,(juce::uint8)100),boundary-start);}
  head.beat=seek&&start>=40000?(backwards?.75:6.5)+(start-40000)/samplesPerBeat:start/samplesPerBeat;
  process(player,audio,midi);
  if(quantized&&start>=request)check(player.activationPending()==(start+n<=boundary),"Refresh did not activate at the exact musical boundary");
  for(int i=0;i<n;++i)for(int channel=0;channel<2;++channel){const float sample=audio.getSample(channel,i);check(std::isfinite(sample),"Refreshed audio is not finite");result.push_back(sample);}
  start+=n;
 }
 return result;
}
void compare(const std::vector<float>& expected,const std::vector<float>& actual){
 check(expected.size()==actual.size(),"Refresh render lengths differ");double maximum=0,energy=0;
 for(size_t i=0;i<expected.size();++i){maximum=std::max(maximum,double(std::abs(expected[i]-actual[i])));energy+=actual[i]*actual[i];}
 if(maximum>=.0001)std::cerr<<"refresh maximum audio difference: "<<maximum<<'\n';
 check(maximum<.0001&&energy>.1,"Quantized refresh changes with buffer partition or differs from exact-boundary publication");
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 for(double grid:{4.,3.5}){
  const auto reference=render(64,grid,false,false,false);
  for(int block:{64,257,2048})compare(reference,render(block,grid,true,false,false));
  compare(reference,render(257,grid,true,true,false));
  compare(reference,render(257,grid,true,true,false,false));
 }
 const auto seekReference=render(64,4,false,false,true);compare(seekReference,render(2048,4,true,false,true));
 compare(render(64,4,false,false,true,true,120,false,true),render(2048,4,true,false,true,true,120,false,true));
 compare(render(64,4,false,false,false,true,137),render(2048,4,true,false,false,true,137));
 compare(render(64,4,false,false,false,true,120,true),render(2048,4,true,false,false,true,120,true));
 {
  DrummerProcessor player;Head head;player.setPlayHead(&head);player.prepareToPlay(48000,256);juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
  head.beat=.5;process(player,audio,midi);player.apply(drummer::compileDocument(fixture(true)),{},4);process(player,audio,midi);
  check(!player.activationPending(),"Idle instrument waits for a bar before adopting patterns");
  midi.addEvent(juce::MidiMessage::noteOn(2,48,(juce::uint8)100),0);head.beat+=256/24000.;process(player,audio,midi);
  juce::MemoryBlock saved;player.getStateInformation(saved);
  player.apply(drummer::compileDocument(fixture(false)),{},4);head.beat+=256/24000.;process(player,audio,midi);check(player.activationPending(),"Held refresh was not queued");
  player.setStateInformation(saved.getData(),(int)saved.getSize());head.beat+=256/24000.;process(player,audio,midi);check(!player.activationPending(),"Host recall did not cancel a waiting refresh");
  player.apply(drummer::compileDocument(fixture(false)),{},4);head.beat+=256/24000.;process(player,audio,midi);
  midi.addEvent(juce::MidiMessage::noteOff(2,48),0);head.beat+=256/24000.;process(player,audio,midi);
  head.beat+=256/24000.;process(player,audio,midi);check(!player.activationPending(),"Releasing all contacts leaves a prepared refresh stranded");
  bool rejected=false;try{player.apply(drummer::compileDocument(fixture(false)),{},std::nan(""));}catch(const std::exception&){rejected=true;}check(rejected,"Invalid activation grid was accepted");
 }
 auto first=drummer::compileDocument(fixture(false));auto edited=fixture(false);
 edited.getDynamicObject()->setProperty("name","Same patterns, new name");edited.getDynamicObject()->setProperty("volume",.6);
 check(first->patternHash==drummer::compileDocument(edited)->patternHash,"Sound edits reset held pattern selection");
 check(first->patternHash!=drummer::compileDocument(fixture(true))->patternHash,"Changed notes were omitted from the runtime identity");
 {
  // Delay the off-thread collector until the fixed retirement queue fills.
  // Audio must keep playing and later adopt the newest coalesced document.
  DrummerProcessor player;player.apply(drummer::compileDocument(fixture(false)));player.prepareToPlay(48000,256);Head head;player.setPlayHead(&head);
  juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;midi.ensureSize(128);midi.addEvent(juce::MidiMessage::noteOn(2,48,(juce::uint8)100),0);process(player,audio,midi);
  int frame=256;
  for(int edit=0;edit<320;++edit){auto document=fixture(true);document.getDynamicObject()->setProperty("name","Prepared "+juce::String(edit));player.apply(drummer::compileDocument(document),{},4);head.beat=frame/24000.;process(player,audio,midi);frame+=256;}
  while(frame<100000){head.beat=frame/24000.;process(player,audio,midi);frame+=256;}
  check(player.activationPending(),"Collector backpressure dropped the newest queued refresh");
  juce::Thread::sleep(110);juce::Timer::callPendingTimersSynchronously();
  while(frame<192256){head.beat=frame/24000.;process(player,audio,midi);if(frame+256<=192000)check(player.activationPending(),"Recovered refresh activated between musical boundaries");frame+=256;}
  check(!player.activationPending(),"Prepared refresh did not recover after off-thread collection");
  check(juce::JSON::parse(player.document()).getProperty("name",{}).toString()=="Prepared 319","Refresh coalescing lost the latest document");
 }
 std::cout<<"refresh: exact bar/odd-meter adoption, sustain, stopped host, seek, idle, recall, coalescing, buffer invariance and allocation checks passed\n";return 0;
 }catch(const std::exception& e){realtime::watching=false;std::cerr<<e.what()<<'\n';return 1;}}
