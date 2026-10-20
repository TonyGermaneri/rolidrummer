#include "PluginProcessor.h"
#include <drummer/Json.h>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Capture {std::vector<drummer::Event> events;static void take(void* p,const drummer::Event& e){static_cast<Capture*>(p)->events.push_back(e);}};
struct Head:juce::AudioPlayHead {double beat=.1;juce::Optional<PositionInfo> getPosition()const override{PositionInfo p;p.setBpm(120);p.setPpqPosition(beat);p.setIsPlaying(true);return p;}};
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 using namespace drummer;
 auto doc=defaultDocument();doc.getDynamicObject()->setProperty("launchGrid",0);doc.getDynamicObject()->setProperty("switchGrid",0);doc.getDynamicObject()->setProperty("hardRelease",true);
 doc.getDynamicObject()->setProperty("clips",juce::JSON::parse(R"([{"id":"test","name":"Every pitch","length":1,"notes":[{"beat":0,"duration":0.1,"note":36,"velocity":1}]}])"));
 auto keys=doc.getProperty("keys",{});for(auto& key:*keys.getArray()){
  key.getDynamicObject()->setProperty("parts",juce::Array<juce::var>{36});key.getDynamicObject()->setProperty("routes",juce::Array<juce::var>{});
  key.getDynamicObject()->setProperty("zones",juce::JSON::parse(R"([{"upper":0.5,"clipId":"test"},{"upper":1,"clipId":"test"}])"));
 }
 auto runtime=compileDocument(doc);auto& cfg=runtime->config;Scheduler scheduler;Capture capture;
 for(int note=0;note<128;++note){
  const int expected=48+((note-48)%16+16)%16;check(originFor(cfg,note)==expected,"Parallel bank did not wrap in chromatic pitch order");
  scheduler.reset();capture.events.clear();Midi on{0,0x91,(uint8_t)note,100};scheduler.process(cfg,{0,120,48000,true},64,&on,1,Capture::take,&capture);
  check(scheduler.contacts()[1].held&&scheduler.contacts()[1].key==expected&&scheduler.contacts()[1].inputNote==note,"A MIDI pitch is unplayable or lost its physical identity");
  check(std::any_of(capture.events.begin(),capture.events.end(),[&](const Event& e){return e.type==Event::hit&&e.key==expected&&e.inputNote==note&&e.drumNote==36;}),"Parallel input did not emit its origin's filtered pattern");
  Midi off{0,0x81,(uint8_t)note,90};scheduler.process(cfg,{64/24000.,120,48000,true},64,&off,1,Capture::take,&capture);check(!scheduler.hasHeldNotes(),"Parallel note-off stranded a contact");
 }
 // Adjacent half steps are distinct keys, regardless of finger arrival order.
 for(const auto pair:{std::pair{52,53},std::pair{53,52},std::pair{59,60},std::pair{60,59},std::pair{48,49},std::pair{49,48}}){
  scheduler.reset();Midi input[]={{0,0x91,(uint8_t)pair.first,100},{1,0x92,(uint8_t)pair.second,100}};
  scheduler.process(cfg,{0,120,48000,true},64,input,2,Capture::take,&capture);
  check(scheduler.contacts()[1].held&&scheduler.contacts()[2].held&&scheduler.contacts()[1].key!=scheduler.contacts()[2].key,"Half-step neighbors depend on play order");
 }
 // Reusing a channel for another parallel pitch must not let the old pitch's
 // delayed note-off or poly pressure affect its replacement.
 scheduler.reset();Midi reuse[]={{0,0x91,64,100},{1,0x91,80,100},{2,0x81,64,64},{3,0xa1,64,127}};
 scheduler.process(cfg,{0,120,48000,true},64,reuse,4,Capture::take,&capture);
 check(scheduler.contacts()[1].held&&scheduler.contacts()[1].inputNote==80&&scheduler.contacts()[1].values.press==0,"Old parallel pitch messages stopped or modulated its replacement");
 // Aliases share the origin's one-owner and toggle rules, including the grid.
 for(const auto mode:{KeyboardMode::normal,KeyboardMode::hold,KeyboardMode::toggle}){
  cfg.keyboardMode=mode;cfg.latchGrid=0;scheduler.reset();Midi on[]={{0,0x91,48,100},{1,0x81,48,64},{2,0x92,64,100}};
  scheduler.process(cfg,{0,120,48000,true},64,on,3,Capture::take,&capture);
  int held=0;for(const auto& f:scheduler.contacts())held+=f.held;for(const auto& f:scheduler.sustainedContacts())held+=f.held;
  check(held==(mode==KeyboardMode::toggle?0:1),"Parallel keys escaped per-origin ownership or toggle behavior");
 }
 cfg.keyboardMode=KeyboardMode::toggle;cfg.latchGrid=4;scheduler.reset();Midi start{0,0x91,64,100};
 scheduler.process(cfg,{.1,120,48000,true},64,&start,1,Capture::take,&capture);check(scheduler.hasPendingLatch(48)&&!scheduler.contacts()[1].held,"Parallel latch did not use the origin's quantized grid");
 scheduler.process(cfg,{4,120,48000,true},64,nullptr,0,Capture::take,&capture);check(scheduler.contacts()[1].held&&scheduler.contacts()[1].key==48,"Parallel latch did not activate at the bar");
 Midi stop{0,0x92,80,100};scheduler.process(cfg,{4.1,120,48000,true},64,&stop,1,Capture::take,&capture);
 scheduler.process(cfg,{8,120,48000,true},64,nullptr,0,Capture::take,&capture);check(!scheduler.hasHeldNotes(),"Parallel unlatch did not stop the origin at its next bar");
 // Old patches migrate their editable bindings, without changing drum notes.
 auto legacy=doc.clone();legacy.getDynamicObject()->removeProperty("keyLayoutVersion");auto oldKeys=legacy.getProperty("keys",{});const int natural[]={48,50,52,53,55,57,59,60,62,64,65,67,69,71,72,74};
 for(int i=0;i<16;++i)oldKeys.getArray()->getReference(i).getDynamicObject()->setProperty("note",natural[i]);
 auto migrated=compileDocument(legacy);const auto migratedDoc=juce::JSON::parse(migrated->json);const auto migratedKeys=migratedDoc.getProperty("keys",{});
 check((int)migratedDoc.getProperty("keyLayoutVersion",0)==1&&migrated->patternHash==runtime->patternHash,"Legacy migration changed the pattern configuration");
 for(int i=0;i<16;++i){const auto& key=migratedKeys.getArray()->getReference(i);check((int)key.getProperty("note",0)==48+i,"Legacy bank is not in chromatic order");
  for(const char* field:{"sound","routes","parts","zones"})check(juce::JSON::toString(key.getProperty(field,{}))==juce::JSON::toString(keys.getArray()->getReference(i).getProperty(field,{})),"Legacy migration changed a keywave's musical settings");
 }
 // Through the real processor, telemetry keeps physical and origin identities
 // and the sound engine receives an audible hit from an otherwise unbound key.
 auto player=std::make_unique<DrummerProcessor>();player->apply(compileDocument(doc));player->prepareToPlay(48000,256);
 juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(2,64,(juce::uint8)110),0);player->processBlock(audio,midi);
 DrummerProcessor::FingerSnapshot view;check(player->readFingers(view)&&view.count==1&&view.fingers[0].note==64&&view.fingers[0].origin==48,"Physical parallel key or origin is absent from telemetry");
 check(audio.getMagnitude(0,256)>.0001f,"Parallel key did not reach the sound engine");PlayedEvent hit;bool found=false;while(player->performanceMonitor.pop(hit))if(hit.type==PlayedEvent::on){found=true;check(hit.key==48&&hit.inputNote==64,"Keywave flash did not retain both parallel and origin pitches");}check(found,"Parallel hit was not monitored");
 // Raw fingers are not deduplicated by the origin that owns their commands.
 auto physical=std::make_unique<DrummerProcessor>();physical->apply(compileDocument(doc));physical->prepareToPlay(48000,256);
 midi.addEvent(juce::MidiMessage::controllerEvent(2,74,100),0);midi.addEvent(juce::MidiMessage::noteOn(2,64,(juce::uint8)100),1);
 midi.addEvent(juce::MidiMessage::controllerEvent(3,74,100),2);midi.addEvent(juce::MidiMessage::noteOn(3,80,(juce::uint8)100),3);physical->processBlock(audio,midi);
 check(physical->readFingers(view)&&view.touchCount==2&&view.count==1,"Two physical fingers were merged with their shared-origin command");
 const auto firstTouch=view.touches[0].id;check(view.touches[0].note==64&&view.touches[1].note==80&&view.fingers[0].touch==view.touches[1].id,"Command did not retain the physical finger that created it");
 midi.addEvent(juce::MidiMessage::noteOff(3,80),0);physical->processBlock(audio,midi);
 check(physical->readFingers(view)&&view.touchCount==1&&view.touches[0].id==firstTouch,"Stopping a command hid the older finger still resting on its parallel key");
 midi.addEvent(juce::MidiMessage::channelPressureChange(2,96),0);physical->processBlock(audio,midi);
 check(physical->readFingers(view)&&view.touchCount==1&&view.touches[0].pressure==96/127.f&&view.count==0,"A suppressed finger disappeared or restarted the replaced command");
 midi.addEvent(juce::MidiMessage::noteOff(2,64),0);physical->processBlock(audio,midi);check(physical->readFingers(view)&&view.touchCount==0,"Physical marker survived its genuine note-off");
 // The fourth adjacent member must reach the real audio engine and telemetry.
 auto nearbyDoc=doc.clone();nearbyDoc.getDynamicObject()->setProperty("clips",juce::JSON::parse(R"([{"id":"four","name":"Four nearby keys","length":1,"notes":[{"beat":0,"duration":0.25,"note":36,"velocity":0.9}]}])"));
 auto nearbyKeys=nearbyDoc.getProperty("keys",{});for(auto& key:*nearbyKeys.getArray()){
  key.getDynamicObject()->setProperty("parts",juce::Array<juce::var>{36});key.getDynamicObject()->setProperty("zones",juce::JSON::parse(R"([{"upper":1,"clipId":"four"}])"));
  key.getProperty("sound",{}).getProperty("s",{}).getDynamicObject()->setProperty("choke",0);
 }
 auto nearbyPlayer=std::make_unique<DrummerProcessor>();nearbyPlayer->apply(compileDocument(nearbyDoc));nearbyPlayer->prepareToPlay(48000,256);
 for(int i=0;i<4;++i){midi.addEvent(juce::MidiMessage::controllerEvent(i+2,74,100),0);midi.addEvent(juce::MidiMessage::noteOn(i+2,48+i,(juce::uint8)100),0);}nearbyPlayer->processBlock(audio,midi);
 check(nearbyPlayer->readFingers(view)&&view.touchCount==4&&view.count==4,"Fourth adjacent key lost its touch or playing command in the real processor");
 check(nearbyPlayer->engine.activeVoices()>=4&&audio.getMagnitude(0,256)>.0001f,"Four adjacent keys did not reach concurrent audible voices");
 for(int i=0;i<4;++i)check(view.touches[(size_t)i].note==48+i&&view.fingers[(size_t)i].origin==48+i,"Adjacent keys were retargeted to their neighbors");
 // A queued tap has no current finger after release. Publish its direction
 // independently, so the UI can keep a green/red indicator until its boundary.
 auto quantized=doc.clone();quantized.getDynamicObject()->setProperty("keyboardMode","toggle");
 auto quant=std::make_unique<DrummerProcessor>();quant->apply(compileDocument(quantized));quant->prepareToPlay(48000,256);Head head;quant->setPlayHead(&head);
 midi.addEvent(juce::MidiMessage::noteOn(2,64,(juce::uint8)100),0);midi.addEvent(juce::MidiMessage::noteOff(2,64),1);quant->processBlock(audio,midi);
 check(quant->readFingers(view)&&view.count==0&&view.latches[48].action==1&&view.latches[48].note==64,"Queued start indicator disappeared when its tapped finger lifted");
 head.beat=4;quant->processBlock(audio,midi);check(quant->readFingers(view)&&view.count==1&&view.latches[48].action==0,"Queued start indicator did not clear at activation");
 head.beat=4.1;midi.addEvent(juce::MidiMessage::noteOn(3,80,(juce::uint8)100),0);midi.addEvent(juce::MidiMessage::noteOff(3,80),1);quant->processBlock(audio,midi);
 check(quant->readFingers(view)&&view.latches[48].action==2&&view.latches[48].note==80,"Queued stop indicator did not distinguish its direction or parallel pitch");
 head.beat=8;quant->processBlock(audio,midi);check(quant->readFingers(view)&&view.count==0&&view.latches[48].action==0,"Queued stop indicator survived the stop boundary");
 head.beat=8.1;midi.addEvent(juce::MidiMessage::noteOn(2,64,(juce::uint8)100),0);midi.addEvent(juce::MidiMessage::noteOff(2,64),1);quant->processBlock(audio,midi);
 head.beat=8.2;midi.addEvent(juce::MidiMessage::noteOn(3,80,(juce::uint8)100),0);midi.addEvent(juce::MidiMessage::noteOff(3,80),1);quant->processBlock(audio,midi);
 check(quant->readFingers(view)&&view.latches[48].action==0,"Cancelling a queued toggle retained its indicator");
 head.beat=8.3;midi.addEvent(juce::MidiMessage::noteOn(2,64,(juce::uint8)100),0);quant->processBlock(audio,midi);
 midi.addEvent(juce::MidiMessage::allNotesOff(1),0);quant->processBlock(audio,midi);check(quant->readFingers(view)&&view.count==0&&view.latches[48].action==0,"Panic retained a queued indicator");
 auto launched=doc.clone();launched.getDynamicObject()->setProperty("launchGrid",1.0);quant->apply(compileDocument(launched));
 head.beat=12.1;midi.addEvent(juce::MidiMessage::noteOn(2,64,(juce::uint8)100),0);quant->processBlock(audio,midi);
 check(quant->readFingers(view)&&view.count==1&&!view.fingers[0].playing&&view.latches[48].action==1,"Normal-mode launch quantization is missing its queued-start indicator");
 head.beat=13;quant->processBlock(audio,midi);check(quant->readFingers(view)&&view.fingers[0].playing&&view.latches[48].action==0,"Launch indicator did not clear when playback started");
 // Ribbon packets target one origin, and real hardware glides encounter
 // individual chromatic lanes while note-off retains its original anchor.
 auto ribbonDoc=doc.clone();ribbonDoc.getDynamicObject()->setProperty("keyboardMode","toggle");
 auto ribbonPlayer=std::make_unique<DrummerProcessor>();ribbonPlayer->apply(compileDocument(ribbonDoc));ribbonPlayer->prepareToPlay(48000,256);Head ribbonHead;ribbonPlayer->setPlayHead(&ribbonHead);
 const auto unchanged=ribbonPlayer->document();
 auto screenTop=[&](int note,int ch){ribbonPlayer->screenMidi.push({(uint8_t)(0xf0|ch),(uint8_t)note,1,0});ribbonPlayer->screenMidi.push({(uint8_t)(0x80|ch),(uint8_t)note,64,3});};
 screenTop(48,1);screenTop(49,2);ribbonPlayer->processBlock(audio,midi);
 check(ribbonPlayer->readFingers(view)&&view.count==0&&view.latches[48].action==1&&view.latches[49].action==1&&view.latches[50].action==0,"Screen ribbon did not queue only the two touched keywaves");
 ribbonHead.beat=4;ribbonPlayer->processBlock(audio,midi);check(ribbonPlayer->readFingers(view)&&view.count==2,"Independent ribbon toggles did not play concurrently");
 for(int i=0;i<view.count;++i)check(view.fingers[(size_t)i].zone==1,"Ribbon touch did not select that key's highest zone");
 check(ribbonPlayer->engine.activeVoices()>=2&&audio.getMagnitude(0,256)>.0001f,"Concurrent ribbon keywaves did not reach the audible sound engine");
 ribbonHead.beat=4.1;screenTop(48,1);ribbonPlayer->processBlock(audio,midi);
 check(ribbonPlayer->readFingers(view)&&view.count==2&&view.latches[48].action==2&&view.latches[49].action==0,"Same-zone toggle queued a stop for another key");
 ribbonHead.beat=8;ribbonPlayer->processBlock(audio,midi);check(ribbonPlayer->readFingers(view)&&view.count==1&&view.fingers[0].origin==49,"Toggling one key stopped its peer");
 ribbonHead.beat=8.1;ribbonPlayer->screenMidi.push({0xf1,49,2,0});ribbonPlayer->processBlock(audio,midi);
 check(ribbonPlayer->readFingers(view)&&view.latches[49].action==2&&view.latches[48].action==0,"Bottom ribbon stop was not scoped to its lane");
 ribbonHead.beat=12;ribbonPlayer->processBlock(audio,midi);check(ribbonPlayer->readFingers(view)&&view.count==0,"Per-key ribbon stop failed");
 ribbonHead.beat=12.1;midi.addEvent(juce::MidiMessage::controllerEvent(2,74,127),0);midi.addEvent(juce::MidiMessage::noteOn(2,64,(juce::uint8)100),1);midi.addEvent(juce::MidiMessage::pitchWheel(2,8192+int(std::lround(2*8191./48))),2);midi.addEvent(juce::MidiMessage::noteOff(2,64),3);ribbonPlayer->processBlock(audio,midi);
 check(ribbonPlayer->readFingers(view)&&view.latches[48].action==1&&view.latches[49].action==1&&view.latches[50].action==1&&view.latches[51].action==0,"Hardware sweep did not visit exactly three lanes");
 ribbonHead.beat=16;ribbonPlayer->processBlock(audio,midi);check(ribbonPlayer->readFingers(view)&&view.count==3,"Hardware ribbon sweep did not keep its three independent toggles");
 ribbonHead.beat=16.1;midi.addEvent(juce::MidiMessage::pitchWheel(2,8192),0);midi.addEvent(juce::MidiMessage::controllerEvent(2,74,0),1);midi.addEvent(juce::MidiMessage::noteOn(2,65,(juce::uint8)100),2);midi.addEvent(juce::MidiMessage::noteOff(2,65),3);ribbonPlayer->processBlock(audio,midi);
 check(ribbonPlayer->readFingers(view)&&view.latches[49].action==2&&view.latches[48].action==0&&view.latches[50].action==0,"Hardware stop touched more than its parallel origin");
 ribbonHead.beat=20;ribbonPlayer->processBlock(audio,midi);check(ribbonPlayer->readFingers(view)&&view.count==2&&ribbonPlayer->document()==unchanged,"Per-key ribbon performance changed the patch or stopped unrelated parts");
 std::cout<<"parallel-keys: all 128 pitches, half-step orders, MPE reuse, owner/latch rules, legacy migration and real audio/telemetry passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
