#include "PluginProcessor.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Head:juce::AudioPlayHead {
 double beat=0,bpm=120;bool playing=true;
 juce::Optional<PositionInfo> getPosition() const override {PositionInfo p;p.setBpm(bpm);p.setPpqPosition(beat);p.setIsPlaying(playing);return p;}
};
juce::var document(){
 auto doc=drummer::defaultDocument();auto* root=doc.getDynamicObject();root->setProperty("launchGrid",0);root->setProperty("switchGrid",0);root->setProperty("hardRelease",true);
 auto clip=juce::JSON::parse(R"({"id":"monitor","name":"Monitor groove","length":4,"notes":[{"beat":0.0026666666666666666,"duration":0.125,"note":36,"velocity":0.8},{"beat":0.0026666666666666666,"duration":0.25,"note":38,"velocity":0.9}]})");
 root->setProperty("clips",juce::Array<juce::var>{clip});
 auto keys=doc.getProperty("keys",{});for(auto& key:*keys.getArray()){
  const int note=(int)key.getProperty("note",0);auto* object=key.getDynamicObject();
  object->setProperty("zones",juce::JSON::parse(R"([{"upper":1,"clipId":"monitor"}])"));
  object->setProperty("parts",juce::Array<juce::var>{note==50?38:36});
 }
 return doc;
}
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 // Saturated telemetry is lossy, FIFO, and immediately usable after draining.
 auto queue=std::make_unique<drummer::PerformanceMonitor>();drummer::PlayedEvent event;
 for(unsigned i=0;i<queue->capacity;++i){event.token=i;check(queue->push(event),"Monitor filled prematurely");}
 check(!queue->push(event)&&queue->dropped==1,"Full monitor did not drop without waiting");
 for(unsigned i=0;i<queue->capacity;++i){check(queue->pop(event)&&event.token==i,"Monitor lost FIFO order");}
 check(!queue->pop(event)&&queue->push(event),"Drained monitor could not resume");

 auto player=std::make_unique<DrummerProcessor>();auto doc=document();player->apply(drummer::compileDocument(doc));player->prepareToPlay(48000,256);Head head;player->setPlayHead(&head);
 juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
 midi.addEvent(juce::MidiMessage::noteOn(2,48,(juce::uint8)100),32);
 midi.addEvent(juce::MidiMessage::noteOn(3,50,(juce::uint8)100),32);
 // A complete touch within one callback must still leave its last pattern.
 midi.addEvent(juce::MidiMessage::noteOff(2,48),128);
 player->processBlock(audio,midi);
 std::vector<drummer::PlayedEvent> events;while(player->performanceMonitor.pop(event))events.push_back(event);
 std::vector<drummer::PlayedEvent> hits;for(const auto& e:events)if(e.type==drummer::PlayedEvent::on)hits.push_back(e);
 check(hits.size()==2,"Monitor missed hits or leaked another drum part");
 check(hits[0].key==48&&hits[0].note==36&&hits[1].key==50&&hits[1].note==38,"Monitor lost keywave/drum-pitch pairing");
 check(std::abs(hits[0].beat-64./24000)<1e-10&&hits[0].duration==.125,"Monitor lost sample timing or note duration");
 check(std::any_of(events.begin(),events.end(),[](const auto& e){return e.type==drummer::PlayedEvent::ownerOff&&std::abs(e.beat-128./24000)<1e-10;}),"Hard release missing from live roll");
 DrummerProcessor::FingerSnapshot snapshot;check(player->readFingers(snapshot),"Pattern snapshot unavailable");
 check(snapshot.patterns[48].known&&!snapshot.patterns[48].playing&&snapshot.patterns[50].playing,"Last/current pattern lifecycle is incorrect");
 check(std::abs(snapshot.patterns[48].phase-256./24000)<1e-10,"Muted pattern stopped following the shared song position");
 check(snapshot.runtime&&snapshot.runtime->clipNames[0]=="Monitor groove","Snapshot did not retain its immutable pattern catalog");
 const auto epoch=snapshot.epoch;double previousBeat=snapshot.beat;
 // Host seeks affect pattern phase, but never move the rolling history backwards.
 head.beat=-12;head.bpm=90;player->processBlock(audio,midi);check(player->readFingers(snapshot)&&snapshot.beat>previousBeat,"Host seek reversed live monitoring time");
 check(std::abs(snapshot.beat-(256./24000+256.*90/(60*48000)))<1e-10,"Tempo change broke monotonic monitor timing");
 // A sound-only update keeps last patterns; geometry changes invalidate them.
 auto edit=doc.clone();edit.getDynamicObject()->setProperty("volume",.6);player->apply(drummer::compileDocument(edit));player->processBlock(audio,midi);juce::Timer::callPendingTimersSynchronously();
 check(player->readFingers(snapshot)&&snapshot.patterns[48].known,"Sound-only edit discarded the last pattern");
 auto notes=edit.getProperty("clips",{}).getArray()->getReference(0).getProperty("notes",{});notes.getArray()->getReference(0).getDynamicObject()->setProperty("velocity",.5);
 player->apply(drummer::compileDocument(edit));player->processBlock(audio,midi);juce::Timer::callPendingTimersSynchronously();
 check(player->readFingers(snapshot)&&!snapshot.patterns[48].known&&snapshot.patterns[50].known,"Active pattern edit left stale last-pattern indices");
 midi.addEvent(juce::MidiMessage::allNotesOff(1),40);player->processBlock(audio,midi);
 bool panic=false;while(player->performanceMonitor.pop(event))panic|=event.type==drummer::PlayedEvent::allOff;check(panic,"Panic missing from live monitor");
 player->prepareToPlay(48000,256);player->processBlock(audio,midi);check(player->readFingers(snapshot)&&snapshot.epoch!=epoch&&snapshot.beat<.02,"Audio restart did not reset monitor epoch");
 // A bar-queued activation splits one callback into two scheduler segments.
 // Display timestamps must include the first segment exactly once.
 auto split=std::make_unique<DrummerProcessor>();split->apply(drummer::compileDocument(document()));split->prepareToPlay(48000,256);Head splitHead;split->setPlayHead(&splitHead);
 midi.addEvent(juce::MidiMessage::noteOn(2,48,(juce::uint8)100),32);split->processBlock(audio,midi);while(split->performanceMonitor.pop(event)){}
 auto next=document();auto nextNotes=next.getProperty("clips",{}).getArray()->getReference(0).getProperty("notes",{});
 nextNotes.getArray()->getReference(0).getDynamicObject()->setProperty("beat",488./24000);
 split->apply(drummer::compileDocument(next),{},.02);splitHead.beat=256./24000;split->processBlock(audio,midi);
 bool afterBoundary=false;while(split->performanceMonitor.pop(event))if(event.type==drummer::PlayedEvent::on){check(std::abs(event.beat-488./24000)<1e-10,"Split callback lost its monitor segment offset");afterBoundary=true;}
 check(afterBoundary&&!split->activationPending(),"Queued pattern did not activate and publish its new hit");
 // Different pattern lengths repeat independently, and telemetry keeps the
 // phrase position as well as the short pattern's wrapped local position.
 auto mixed=std::make_unique<DrummerProcessor>();auto mixedDoc=document();
 auto shortClip=juce::JSON::parse(R"({"id":"short","name":"One bar","length":8,"loopLength":4,"notes":[{"beat":0,"duration":0.125,"note":36,"velocity":0.8}]})");
 auto longClip=juce::JSON::parse(R"({"id":"long","name":"Two bars","length":8,"notes":[{"beat":0,"duration":0.125,"note":38,"velocity":0.8}]})");
 mixedDoc.getDynamicObject()->setProperty("clips",juce::Array<juce::var>{shortClip,longClip});
 auto mixedKeys=mixedDoc.getProperty("keys",{});for(auto& key:*mixedKeys.getArray()){
  auto* zone=new juce::DynamicObject;zone->setProperty("upper",1.0);zone->setProperty("clipId",(int)key.getProperty("note",0)==50?"long":"short");key.getDynamicObject()->setProperty("zones",juce::Array<juce::var>{zone});
 }
 mixed->apply(drummer::compileDocument(mixedDoc));mixed->prepareToPlay(48000,2400);Head mixedHead;mixed->setPlayHead(&mixedHead);
 juce::AudioBuffer<float> mixedAudio(2,2400);std::vector<int> shortBeats,longBeats;
 for(int block=0;block<160;++block){
  mixedHead.beat=block*.1;midi.clear();if(block==0){midi.addEvent(juce::MidiMessage::noteOn(2,48,(juce::uint8)100),0);midi.addEvent(juce::MidiMessage::noteOn(3,50,(juce::uint8)100),0);}
  mixed->processBlock(mixedAudio,midi);while(mixed->performanceMonitor.pop(event))if(event.type==drummer::PlayedEvent::on)(event.key==48?shortBeats:longBeats).push_back((int)std::llround(event.beat));
  if(block==49){check(mixed->readFingers(snapshot),"Mixed-loop telemetry unavailable");check(std::abs(snapshot.patterns[48].phase-1)<1e-8&&std::abs(snapshot.patterns[48].beat-5)<1e-8,"Short-loop marker lost its second pass through the phrase");}
 }
 check(shortBeats==std::vector<int>({0,4,8,12})&&longBeats==std::vector<int>({0,8}),"Processor made a short pattern wait for the longer pattern");
 // Keys unmute a continuously phased pattern; they never create a clip origin.
 // Cover an already-running song, a stopped edit cursor, and AU hosts that
 // advance PPQ but omit the transport-state callback. All must agree exactly.
 auto songRender=[](int block,const juce::String& mode,int hostKind){
  auto player=std::make_unique<DrummerProcessor>();auto doc=document();auto* o=doc.getDynamicObject();
  o->setProperty("launchGrid",1.);o->setProperty("switchGrid",1.);o->setProperty("keyboardMode",mode);o->setProperty("latchBars",1);
  o->setProperty("clips",juce::JSON::parse(R"([{"id":"song","name":"Song phase","length":4,"notes":[{"beat":0.125,"duration":0.1,"note":36,"velocity":0.8},{"beat":1.25,"duration":0.1,"note":36,"velocity":0.8},{"beat":2.5,"duration":0.1,"note":36,"velocity":0.8},{"beat":3.75,"duration":0.1,"note":36,"velocity":0.8}]}])"));
  auto keys=doc.getProperty("keys",{});for(auto& key:*keys.getArray())key.getDynamicObject()->setProperty("zones",juce::JSON::parse(R"([{"upper":1,"clipId":"song"}])"));
  player->apply(drummer::compileDocument(doc));player->prepareToPlay(48000,2048);Head head;head.playing=hostKind==0;player->setPlayHead(&head);
  std::vector<int> hits;juce::MidiBuffer midi;juce::AudioBuffer<float> audio(2,2048);
  for(int at=0;at<96000;at+=block){
   const int size=std::min(block,96000-at);audio.setSize(2,size,false,false,true);midi.clear();
   head.beat=10.2+(hostKind==2?0:at/24000.);
   if(at<=137&&137<at+size)midi.addEvent(juce::MidiMessage::noteOn(2,48,(juce::uint8)100),137-at);
   if(mode!="normal"&&at<=138&&138<at+size)midi.addEvent(juce::MidiMessage::noteOff(2,48),138-at);
   player->processBlock(audio,midi);
   check(std::abs(player->hostPpq.load()-(10.2+(at+size)/24000.))<1e-8,"Host song position was replaced by an instance-relative clock");
   drummer::PlayedEvent event;while(player->performanceMonitor.pop(event))if(event.type==drummer::PlayedEvent::on)hits.push_back((int)std::llround(event.beat*24000));
   DrummerProcessor::FingerSnapshot view;check(player->readFingers(view),"Song-phase snapshot unavailable");
   if(view.patterns[48].known){
    double phase=std::fmod(10.2+(at+size)/24000.,4.);
    check(std::abs(std::remainder(view.patterns[48].phase-phase,4.))<1e-8,"Pattern phase restarted at a key press or latch");
    const double opens=mode=="normal"?11.:12.;
    if(10.2+(at+size)/24000.<opens)check(!view.patterns[48].playing,"Queued pattern was displayed as already unmuted");
   }
  }
  const std::vector<int> expected=mode=="normal"?std::vector<int>{37200,46200,73200}:std::vector<int>{46200,73200};
  check(hits==expected,"Quantized unmute restarted the clip or played the wrong song beats");return hits;
 };
 for(const auto* mode:{"normal","hold","toggle"}){
  const auto expected=songRender(257,mode,0);
  for(int hostKind:{0,1,2})for(int block:{64,2048})check(songRender(block,mode,hostKind)==expected,"Song-position unmute depends on host callback or buffer size");
 }
 std::cout<<"monitor: bounded FIFO, filtered output, sample timing, note duration, short taps, last/current patterns, seek/tempo, refresh and restart passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
