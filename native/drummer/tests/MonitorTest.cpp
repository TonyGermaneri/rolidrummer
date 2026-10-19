#include "PluginProcessor.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Head:juce::AudioPlayHead {
 double beat=0,bpm=120;
 juce::Optional<PositionInfo> getPosition() const override {PositionInfo p;p.setBpm(bpm);p.setPpqPosition(beat);p.setIsPlaying(true);return p;}
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
 check(std::abs(snapshot.patterns[48].phase-128./24000)<1e-10,"Short tap did not freeze its last transport position");
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
 std::cout<<"monitor: bounded FIFO, filtered output, sample timing, note duration, short taps, last/current patterns, seek/tempo, refresh and restart passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
