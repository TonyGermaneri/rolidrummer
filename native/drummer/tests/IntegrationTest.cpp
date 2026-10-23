#include "PluginProcessor.h"
#include <ssb/Parameters.h>
#include <drummer/Assets.h>
#include <drummer/PatchBank.h>
#include <drummer/PatchResolver.h>
#include <drummer/PatternFamilies.h>
#include <iostream>
#include <stdexcept>
void check(bool x,const char* why){if(!x)throw std::runtime_error(why);}
struct Head:juce::AudioPlayHead{double beat=0,bpm=120;bool playing=true,reportTempo=true;juce::Optional<PositionInfo> getPosition()const override{PositionInfo p;if(reportTempo)p.setBpm(bpm);p.setPpqPosition(beat);p.setIsPlaying(playing);return p;}};
double render(DrummerProcessor&p,Head&head,bool hold,int channel=2,int note=48){double energy=0;p.setPlayHead(&head);juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;for(int block=0;block<100;++block){midi.clear();if(block==0&&hold)midi.addEvent(juce::MidiMessage::noteOn(channel,note,(juce::uint8)100),0);head.beat=block*256/24000.;p.processBlock(audio,midi);for(int i=0;i<256;++i)energy+=audio.getSample(0,i)*audio.getSample(0,i);}return energy;}
int main(int argc,char**argv){juce::ScopedJuceInitialiser_GUI gui;try{
 if(argc==4&&std::string(argv[1])=="write") {drummer::SharedStore store{juce::File(argv[2])};auto result=store.commit(juce::JSON::parse("[{\"key\":\""+juce::String(argv[3])+"\",\"value\":42,\"revision\":0}]"));return (bool)result.getProperty("ok",false)?0:2;}
 if(argc==3&&(std::string(argv[1])=="portable"||std::string(argv[1])=="restore")) {
  DrummerProcessor player;juce::File source(argv[2]);
  if(std::string(argv[1])=="portable")player.apply(drummer::compileDocument(drummer::loadPatch(source)));
  else {juce::MemoryBlock bytes;check(source.loadFileAsData(bytes),"State fixture unavailable");player.setStateInformation(bytes.getData(),(int)bytes.getSize());}
  auto saved=juce::JSON::parse(player.document());auto keys=saved.getProperty("keys",{});auto sound=(*keys.getArray())[0].getProperty("sound",{});
  check(sound.getProperty("asset",{}).toString().isNotEmpty(),"External sample was not restored");
  check(sound.getProperty("missing",{}).toString().isEmpty(),"Restored sound is missing");
  player.prepareToPlay(48000,256);Head head;check(render(player,head,true)>.1,"Restored external sample is silent");return 0;
 }
 auto doc=drummer::defaultDocument();auto runtime=drummer::compileDocument(doc);check(runtime->config.clips.size()==3,"Factory patterns missing");
 {
  check(runtime->config.latchGrid==4,"Default latch quantization is not one bar");
  auto quantized=doc.clone();quantized.getDynamicObject()->setProperty("latchBars",2);quantized.getDynamicObject()->setProperty("neural",juce::JSON::parse(R"({"version":1,"numerator":7,"denominator":8})"));
  check(drummer::compileDocument(quantized)->config.latchGrid==7,"Latch bars ignored the patch's 7/8 meter");
  quantized.getDynamicObject()->setProperty("latchQuantize",false);check(drummer::compileDocument(quantized)->config.latchGrid==0,"Latch quantization bypass was ignored");
 }
 // Master settings migrate dry and survive native patch/session compilation.
 {
  check(runtime->performance->fx.cutoff==20000 && runtime->performance->fx.sources[0]==-1,"Legacy master defaults changed the sound");
  auto bus=doc.clone();auto fx=juce::JSON::parse(R"({"cutoff":800,"resonance":3,"reverbMix":0.3,"delayMix":0.2,"chorusMix":0.1,"bindings":{"reverbMix":0,"delayMix":1,"chorusMix":2,"cutoff":3,"resonance":4}})");
  bus.getDynamicObject()->setProperty("fx",fx);auto compiled=drummer::compileDocument(bus);
  check(compiled->performance->fx.cutoff==800 && compiled->performance->fx.sources==std::array<int,5>{0,1,2,3,4},"Master FX/mappings were not compiled");
  auto recalled=drummer::compileDocument(juce::JSON::parse(compiled->json));check(recalled->performance->fx.sources==compiled->performance->fx.sources,"Master bindings lost on round trip");
 }
 // Hardware program changes load on the worker with no editor. Use an isolated patch bank.
 {
  DrummerProcessor player;auto temporary=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("rd-program-test",".sqlite");
  player.shared=std::make_unique<drummer::SharedStore>(temporary);
  juce::Array<juce::var> operations;
  for(const char* name:{"Zulu","Alpha"}){auto patch=doc.clone();patch.getDynamicObject()->setProperty("name",name);auto* op=new juce::DynamicObject;op->setProperty("key","patch/"+juce::String(name));op->setProperty("value",patch);op->setProperty("revision",0);operations.add(op);}
  player.shared->commit(operations);const auto bank=drummer::patchBank(player.shared->snapshot());
  check(bank.size()==128&&bank[1].getProperty("name",{}).toString()=="Alpha"&&bank[2].getProperty("name",{}).toString()=="Zulu","Initial MIDI program assignments changed");
  player.prepareToPlay(48000,256);juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
  auto pump=[&](const char* name){for(int i=0;i<300;++i){juce::Thread::sleep(5);juce::Timer::callPendingTimersSynchronously();player.processBlock(audio,midi);if(juce::JSON::parse(player.document()).getProperty("name",{}).toString()==name)return;}throw std::runtime_error("MIDI patch worker did not select requested patch");};
  midi.addEvent(juce::MidiMessage::programChange(2,1),0);player.processBlock(audio,midi);check(player.lastProgram.load()==-1,"Member-channel program message changed the global patch");
  midi.addEvent(juce::MidiMessage::programChange(1,1),0);player.processBlock(audio,midi);pump("Alpha");
  midi.addEvent(juce::MidiMessage::programChange(1,2),0);player.processBlock(audio,midi);pump("Zulu");
  midi.addEvent(juce::MidiMessage::programChange(1,1),0);midi.addEvent(juce::MidiMessage::programChange(1,0),2);player.processBlock(audio,midi);pump(doc.getProperty("name",{}).toString().toRawUTF8());
  auto updated=bank[1].clone();updated.getDynamicObject()->setProperty("name","Renamed Alpha");
  auto growth=doc.clone();growth.getDynamicObject()->setProperty("name","Aardvark");
  juce::Array<juce::var> changes;
  auto add=[&](const juce::String& key,const juce::var& value,int64_t revision){auto* op=new juce::DynamicObject;op->setProperty("key",key);op->setProperty("value",value);op->setProperty("revision",(juce::int64)revision);changes.add(op);};
  int64_t alphaRevision=0;const auto savedBank=player.shared->snapshot();for(const auto& field:*savedBank.getArray())if(field.getProperty("key",{}).toString()=="patch/Alpha")alphaRevision=(juce::int64)field.getProperty("revision",0);
  add("patch/Alpha",updated,alphaRevision);add("patch/Aardvark",growth,0);check((bool)player.shared->commit(changes).getProperty("ok",false),"Patch bank growth failed");
  const auto stable=drummer::patchBank(player.shared->snapshot());
  check(stable[1].getProperty("id",{})==bank[1].getProperty("id",{})&&stable[2].getProperty("id",{})==bank[2].getProperty("id",{})&&stable[3].getProperty("name",{}).toString()=="Aardvark","A renamed or alphabetically earlier patch shifted assigned programs");
  midi.addEvent(juce::MidiMessage::programChange(1,1),0);player.processBlock(audio,midi);pump("Renamed Alpha");
  midi.addEvent(juce::MidiMessage::programChange(1,127),0);player.processBlock(audio,midi);
  for(int i=0;i<300&&!player.programUnassigned.load();++i){juce::Thread::sleep(5);juce::Timer::callPendingTimersSynchronously();player.processBlock(audio,midi);}
  check(player.programUnassigned.load()&&juce::JSON::parse(player.document()).getProperty("name",{}).toString()=="Renamed Alpha","An unassigned program replaced the current patch");
  check(!player.programFailed.load(),"Valid program selection failed");
  player.shared.reset();temporary.deleteFile();temporary.getSiblingFile(temporary.getFileName()+"-wal").deleteFile();temporary.getSiblingFile(temporary.getFileName()+"-shm").deleteFile();
 }
 const std::array<int,16> factoryKeys={48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63};
 check(runtime->performance->contactEffectsOnly,"Drummer prepared unused legacy per-sound reverb convolvers");
 for(int key:factoryKeys)check(runtime->sounds[(size_t)key]!=nullptr,"Factory kit is not chromatically mapped");
 for(int note=0;note<128;++note)check(runtime->config.origins[(size_t)note]>=48&&runtime->config.origins[(size_t)note]<64,"Factory parallel key is unplayable");
 // Real two-bar backbeats, independently voiced parts and tuned sample reuse.
 for(const auto& clip:runtime->config.clips){
  check(clip.length==8,"Factory groove must have two bars");std::vector<double> backbeats,kicks;
  for(const auto&hit:clip.notes){if(hit.note==38&&hit.velocity>.7)backbeats.push_back(hit.beat);if(hit.note==36)kicks.push_back(hit.beat);}
  check(backbeats==std::vector<double>({1,3,5,7}),"Snare backbeat is not on beats 2 and 4");check(kicks!=backbeats,"Kick doubles the snare rhythm");
  for(int key:factoryKeys)check(std::any_of(clip.notes.begin(),clip.notes.end(),[&](const auto&hit){return runtime->config.keys[(size_t)key].parts[(size_t)hit.note];}),"Factory part lacks pattern events");
 }
 check(runtime->sounds[51]->sample==runtime->sounds[52]->sample&&runtime->sounds[52]->sample==runtime->sounds[53]->sample,"Tuned toms should reuse the waveform");
 check(runtime->sounds[51]->s.pitch>runtime->sounds[52]->s.pitch&&runtime->sounds[52]->s.pitch>runtime->sounds[53]->s.pitch,"Toms lack descending tuning");
 // Render every synthesized voice with neutral Glide: tuning must survive MPE.
 {
  ssb::Engine synth;synth.prepare(48000,256);synth.setPerformance(runtime->performance);std::array<float,256> l{},r{};
  std::array<int,3> crossings{};
  for(int key:factoryKeys){
   auto sound=runtime->sounds[(size_t)key];check(sound!=nullptr,"Missing full-kit voice");
   check(std::abs((sound->expressionRoutes[1].minimum+sound->expressionRoutes[1].maximum)*.5f-sound->s.pitch)<.001,"Glide overwrites factory tuning");
   synth.drumPanic();synth.process(l.data(),r.data(),1,nullptr,0,{120,0,true});synth.drumHit(sound,key+1,key+1,1,{1,.5f,.5f,0,0});double energy=0;float previous=0;
   for(int block=0;block<188;++block){synth.process(l.data(),r.data(),256,nullptr,0,{120,block*256/24000.,true});for(float v:l){check(std::isfinite(v),"Invalid synthesized voice audio");energy+=v*v;if(previous<=0&&v>0)for(size_t tom=0;tom<3;++tom)if(key==factoryKeys[tom+3])++crossings[tom];previous=v;}}
   if(energy<=.00001)std::cerr<<"Silent factory key "<<key<<" energy "<<energy<<"\n";check(energy>.00001,"Synth kit contains a silent voice");
  }
  check(crossings[0]>crossings[1]&&crossings[1]>crossings[2],"Rendered tom pitches do not descend");
 }
 auto mapped=juce::JSON::parse(runtime->json).getProperty("keys",{});
 for(auto& key:*mapped.getArray())check(key.getProperty("routes",{}).getArray()->size()==5,"Factory sound lacks all five MPE mappings");
 auto unmapped=doc.clone();auto unmappedKeys=unmapped.getProperty("keys",{});(*unmappedKeys.getArray())[0].getDynamicObject()->setProperty("routes",juce::Array<juce::var>{});
 auto kept=juce::JSON::parse(drummer::compileDocument(unmapped)->json).getProperty("keys",{});
 check((*kept.getArray())[0].getProperty("routes",{}).getArray()->isEmpty(),"Intentional unmapping was replaced");
 // Audition uses its own clock while Live is stopped, and leaves the kit untouched.
 {
  DrummerProcessor browser;Head stopped;stopped.playing=false;browser.prepareToPlay(48000,256);browser.setPlayHead(&stopped);
  const auto saved=browser.document();juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
  auto listen=[&](bool pattern,uint64_t ticket,double sr=48000){browser.publishPreview(std::make_unique<drummer::Preview>(drummer::compileDocument(doc),pattern,sr,256),ticket);};
  listen(true,++browser.previewRequest);double energy=0;
  for(int block=0;block<40;++block){browser.processBlock(audio,midi);for(int i=0;i<256;++i)energy+=audio.getSample(0,i)*audio.getSample(0,i);}
  check(energy>.1&&browser.previewPlaying.load(),"Pattern preview is silent when host transport is stopped");
  check(browser.document()==saved,"Audition changed the saved kit");for(auto& contact:browser.contacts)check(contact.load()==0,"Audition stole an MPE contact");
  browser.stopPreview();browser.processBlock(audio,midi);check(audio.getMagnitude(0,256)==0&&!browser.previewPlaying.load(),"Preview stop left audio running");
  const auto stale=++browser.previewRequest;browser.stopPreview();listen(true,stale);browser.processBlock(audio,midi);check(audio.getMagnitude(0,256)==0,"Cancelled worker restarted audition");
  listen(false,++browser.previewRequest);browser.processBlock(audio,midi);check(audio.getMagnitude(0,256)>0,"Sound preview is silent");
  browser.prepareToPlay(44100,256);browser.processBlock(audio,midi);check(!browser.previewPlaying.load(),"Old-rate preview survived host reprepare");
  listen(true,++browser.previewRequest,48000);browser.processBlock(audio,midi);check(!browser.previewPlaying.load(),"Late old-rate preview was accepted");
 }
 // Every mode gives a key one zone/owner, in both audible output and editor telemetry.
 {
  for(const char* mode:{"normal","hold","toggle"}){
   auto patch=doc.clone();patch.getDynamicObject()->setProperty("keyboardMode",mode);patch.getDynamicObject()->setProperty("latchQuantize",false);patch.getDynamicObject()->setProperty("launchGrid",0);patch.getDynamicObject()->setProperty("switchGrid",0);
   auto key=(*patch.getProperty("keys",{}).getArray())[0].clone();auto settings=key.getProperty("sound",{}).getProperty("s",{});
   settings.getDynamicObject()->setProperty("mode","hold");settings.getDynamicObject()->setProperty("delayMix",0);settings.getDynamicObject()->setProperty("reverbMix",0);
   key.getDynamicObject()->setProperty("routes",juce::JSON::parse(R"([{"source":1,"target":"pan","min":-1,"max":1,"curve":1,"smoothing":0}])"));
   key.getDynamicObject()->setProperty("zones",juce::JSON::parse(R"([{"upper":0.22,"clipId":"single"},{"upper":0.76,"clipId":"single"},{"upper":1,"clipId":"single"}])"));
   patch.getDynamicObject()->setProperty("keys",juce::Array<juce::var>{key});
   juce::Array<juce::var> notes;for(int n=0;n<8;++n){auto* hit=new juce::DynamicObject;hit->setProperty("beat",n*.125);hit->setProperty("duration",.1);hit->setProperty("note",36);hit->setProperty("velocity",1);notes.add(juce::var(hit));}
   auto* clip=new juce::DynamicObject;clip->setProperty("id","single");clip->setProperty("length",1);clip->setProperty("notes",notes);patch.getDynamicObject()->setProperty("clips",juce::Array<juce::var>{juce::var(clip)});
   DrummerProcessor player;player.apply(drummer::compileDocument(patch));player.prepareToPlay(48000,256);Head head;head.playing=false;player.setPlayHead(&head);
   juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;DrummerProcessor::FingerSnapshot fingers;
   midi.addEvent(juce::MidiMessage::pitchWheel(2,0),0);midi.addEvent(juce::MidiMessage::noteOn(2,48,(juce::uint8)100),0);
   double left=0;for(int b=0;b<32;++b){player.processBlock(audio,midi);for(int i=0;i<256;++i)left+=audio.getSample(0,i)*audio.getSample(0,i);}check(left>.01,"One-zone audio fixture was silent");
   midi.addEvent(juce::MidiMessage::pitchWheel(3,16383),0);midi.addEvent(juce::MidiMessage::controllerEvent(3,74,115),0);midi.addEvent(juce::MidiMessage::noteOn(3,48,(juce::uint8)100),0);player.processBlock(audio,midi);
   check(player.readFingers(fingers)&&fingers.count==1&&fingers.fingers[0].channel==2&&fingers.fingers[0].zone==2,"Processor retained multiple zones/markers for a key");
   midi.addEvent(juce::MidiMessage::controllerEvent(2,74,64),0);midi.addEvent(juce::MidiMessage::channelPressureChange(2,127),1);midi.addEvent(juce::MidiMessage::noteOff(2,48),2);
   left=0;double right=0;for(int b=0;b<96;++b){player.processBlock(audio,midi);if(b>48)for(int i=0;i<256;++i){left+=audio.getSample(0,i)*audio.getSample(0,i);right+=audio.getSample(1,i)*audio.getSample(1,i);}}
   check(left<1e-8&&right>.01,"Replaced zone remained audible or stale finger input stopped the new zone");
   check(player.readFingers(fingers)&&fingers.count==1&&fingers.fingers[0].zone==2,"Stale input changed the authoritative playing zone");
  }
 }
 // Marker snapshots retain the sustained owner while the same channel gets a new finger.
 {
  DrummerProcessor player;player.prepareToPlay(48000,256);Head head;head.playing=false;player.setPlayHead(&head);
  juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;DrummerProcessor::FingerSnapshot snapshot;
  auto read=[&](){check(player.readFingers(snapshot),"Finger snapshot unavailable");};
  midi.addEvent(juce::MidiMessage::controllerEvent(2,74,96),0);
  midi.addEvent(juce::MidiMessage::channelPressureChange(2,90),0);
  midi.addEvent(juce::MidiMessage::noteOn(2,48,(juce::uint8)100),0);
  player.processBlock(audio,midi);read();check(snapshot.count==1&&!snapshot.fingers[0].sustained,"Physical marker missing");
  const auto original=snapshot.fingers[0];
  midi.addEvent(juce::MidiMessage::controllerEvent(1,64,127),0);
  midi.addEvent(juce::MidiMessage::noteOff(2,48),1);
  midi.addEvent(juce::MidiMessage::controllerEvent(2,74,20),2);
  midi.addEvent(juce::MidiMessage::noteOn(2,50,(juce::uint8)100),3);
  player.processBlock(audio,midi);read();check(snapshot.count==2,"Reused channel lost a sustained marker");
  int latched=0;for(int i=0;i<snapshot.count;++i){const auto& f=snapshot.fingers[(size_t)i];
   if(f.sustained){++latched;check(f.owner==original.owner&&f.note==48&&f.slide==original.slide&&f.glide==original.glide&&f.pressure==original.pressure,"Sustained marker changed captured values");}
   else check(f.owner!=original.owner&&f.note==50&&std::abs(f.slide-4/99.f)<.0001f,"New finger inherited sustained identity");
  }check(latched==1,"Incorrect sustained marker count");
  midi.addEvent(juce::MidiMessage::controllerEvent(1,64,0),0);player.processBlock(audio,midi);read();
  check(snapshot.count==1&&!snapshot.fingers[0].sustained&&snapshot.fingers[0].note==50,"Pedal up retained latched marker or removed physical finger");
  midi.addEvent(juce::MidiMessage::controllerEvent(1,64,127),0);midi.addEvent(juce::MidiMessage::noteOff(2,50),1);player.processBlock(audio,midi);
  midi.addEvent(juce::MidiMessage::allNotesOff(1),0);player.processBlock(audio,midi);read();check(snapshot.count==0,"Panic left finger markers");
 }
 // Software modes reach the audio engine and telemetry without an editor, and recall as patch/session settings.
 {
  auto legacy=doc.clone();legacy.getDynamicObject()->removeProperty("keyboardMode");
  check(drummer::compileDocument(legacy)->config.keyboardMode==drummer::KeyboardMode::normal,"Legacy patch did not default to Normal");
  legacy.getDynamicObject()->setProperty("keyboardMode","invalid");
  check(juce::JSON::parse(drummer::compileDocument(legacy)->json).getProperty("keyboardMode",{}).toString()=="normal","Invalid keyboard mode was not canonicalized");
  for(const char* mode:{"hold","toggle"}){
   DrummerProcessor player;auto patch=doc.clone();patch.getDynamicObject()->setProperty("keyboardMode",mode);patch.getDynamicObject()->setProperty("latchQuantize",false);
   patch.getDynamicObject()->setProperty("launchGrid",0);patch.getDynamicObject()->setProperty("switchGrid",0);
   player.apply(drummer::compileDocument(patch));player.prepareToPlay(48000,256);Head head;head.playing=false;player.setPlayHead(&head);
   juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;DrummerProcessor::FingerSnapshot snapshot;
   midi.addEvent(juce::MidiMessage::controllerEvent(2,74,64),0);midi.addEvent(juce::MidiMessage::noteOn(2,50,(juce::uint8)100),0);
   midi.addEvent(juce::MidiMessage::noteOff(2,50,(juce::uint8)91),100);player.processBlock(audio,midi);
   check(player.readFingers(snapshot)&&snapshot.count==1&&snapshot.fingers[0].sustained&&snapshot.fingers[0].playing,"Software latch did not reach processor telemetry");
   const bool toggle=std::string(mode)=="toggle";
   check(snapshot.fingers[0].toggled==toggle,"Telemetry confused Toggle with Hold");
   double energy=0;for(int block=0;block<120;++block){player.processBlock(audio,midi);if(block>40)for(int i=0;i<256;++i)energy+=audio.getSample(0,i)*audio.getSample(0,i);}
   check(energy>.01&&player.freePlaying.load(),"Released software latch stopped producing audio with a stopped host");
   if(toggle){
    // A held replacement in a different zone owns all subsequent expression for this key.
    midi.addEvent(juce::MidiMessage::controllerEvent(3,74,115),0);midi.addEvent(juce::MidiMessage::noteOn(3,50,(juce::uint8)100),0);player.processBlock(audio,midi);
    check(player.readFingers(snapshot)&&snapshot.count==1&&snapshot.fingers[0].channel==2&&snapshot.fingers[0].zone==2&&!snapshot.fingers[0].sustained&&snapshot.fingers[0].toggled,"Zone handoff left duplicate/wrong processor owners");
    midi.addEvent(juce::MidiMessage::controllerEvent(4,74,115),0);midi.addEvent(juce::MidiMessage::noteOn(4,50,(juce::uint8)100),0);player.processBlock(audio,midi);
    check(player.readFingers(snapshot)&&snapshot.count==0&&!player.freePlaying.load(),"Same-zone toggle left physical-but-suppressed markers or a running gate");
    // Sound/routing edits must not revive either explicitly stopped physical contact.
    patch.getDynamicObject()->setProperty("switchGrid",.25);auto keys=patch.getProperty("keys",{});(*keys.getArray())[1].getDynamicObject()->setProperty("zoneSource",3);
    player.apply(drummer::compileDocument(patch));player.processBlock(audio,midi);
    check(player.readFingers(snapshot)&&snapshot.count==0&&!player.freePlaying.load(),"Runtime edit resurrected toggled-off physical keys");
    midi.addEvent(juce::MidiMessage::noteOff(3,50),0);midi.addEvent(juce::MidiMessage::noteOff(4,50),1);player.processBlock(audio,midi);
   }
   midi.addEvent(juce::MidiMessage::noteOn(5,48,(juce::uint8)100),0);midi.addEvent(juce::MidiMessage::controllerEvent(1,64,0),1);player.processBlock(audio,midi);
   check(player.readFingers(snapshot)&&snapshot.count==1&&snapshot.fingers[0].note==48&&!snapshot.fingers[0].sustained,"Pedal up did not clear lifted software latches while retaining the physical key");
   juce::MemoryBlock saved;player.getStateInformation(saved);DrummerProcessor restored;restored.setStateInformation(saved.getData(),(int)saved.getSize());
   const auto recalled=juce::JSON::parse(restored.document());
   check(recalled.getProperty("keyboardMode",{}).toString()==mode,"AU/VST state lost the keyboard mode");
   check(drummer::compileDocument(recalled)->config.keyboardMode==(toggle?drummer::KeyboardMode::toggle:drummer::KeyboardMode::hold),"Patch round trip lost keyboard mode");
   restored.prepareToPlay(48000,256);restored.processBlock(audio,midi);
   check(restored.readFingers(snapshot)&&snapshot.count==0&&!restored.freePlaying.load(),"Recall serialized live key latches");
  }
 }
 // Sustain renders the same pattern as a physically held key, even with a stopped host.
 {
  auto sustainRender=[&](bool pedal,bool playing,int block){
   DrummerProcessor player;auto patch=doc.clone();patch.getDynamicObject()->setProperty("sustainCC",66);
   player.apply(drummer::compileDocument(patch));player.prepareToPlay(48000,2048);Head head;head.playing=playing;player.setPlayHead(&head);
   juce::AudioBuffer<float> audio(2,2048);juce::MidiBuffer midi;std::vector<float> result;
   struct Input{int at;juce::MidiMessage message;};
   const Input events[]={{0,juce::MidiMessage::noteOn(2,50,(juce::uint8)100)},
    {1,juce::MidiMessage::controllerEvent(1,66,127)},
    {pedal?1000:48000,juce::MidiMessage::noteOff(2,50,(juce::uint8)99)},
    {48000,juce::MidiMessage::controllerEvent(1,66,0)}};
   size_t index=0;
   for(int at=0;at<96000;at+=block){const int n=std::min(block,96000-at);audio.setSize(2,n,false,false,true);midi.clear();
    while(index<std::size(events)&&events[index].at<at+n){midi.addEvent(events[index].message,events[index].at-at);++index;}
    head.beat=at/24000.;player.processBlock(audio,midi);
    if(at>2000&&at+n<48000){check(player.freePlaying.load()==!playing,"Sustained stopped-host gate missing");if(pedal)check(player.contacts[1].load()==0,"Sustained note appears as a physical finger");}
    if(at>48000)check(!player.freePlaying.load(),"Pedal up did not close the stopped-host gate");
    for(int j=0;j<n;++j)result.push_back(audio.getSample(0,j));
   }
   juce::MemoryBlock saved;player.getStateInformation(saved);DrummerProcessor restored;restored.setStateInformation(saved.getData(),(int)saved.getSize());
   check(drummer::compileDocument(juce::JSON::parse(restored.document()))->config.sustainCC==66,"AU state lost reassigned pedal");
   return result;
  };
  const auto held=sustainRender(false,false,256);
  for(bool playing:{false,true})for(int block:{64,257}){
   const auto pedalled=sustainRender(true,playing,block);double error=0,energy=0;
   for(size_t i=0;i<held.size();++i){error=std::max(error,double(std::abs(held[i]-pedalled[i])));if(i>24000&&i<48000)energy+=pedalled[i]*pedalled[i];}
   if(error>=.0001||energy<=.01)std::cerr<<"sustain: error="<<error<<" energy="<<energy<<" playing="<<playing<<" block="<<block<<"\n";
   check(error<.0001&&energy>.01,"Pedal-held audio differs from physical hold or loses clock sync");
  }
 }
 // A software Hold/Toggle latch produces the same samples as a physical hold until pedal-up.
 {
  auto modeRender=[&](const char* mode,int block){
   DrummerProcessor player;auto patch=doc.clone();patch.getDynamicObject()->setProperty("keyboardMode",mode);patch.getDynamicObject()->setProperty("latchQuantize",false);
   player.apply(drummer::compileDocument(patch));player.prepareToPlay(48000,2048);Head head;head.playing=false;player.setPlayHead(&head);
   juce::AudioBuffer<float> audio(2,2048);juce::MidiBuffer midi;std::vector<float> result;
   const bool normal=std::string(mode)=="normal";
   struct Input{int at;juce::MidiMessage message;};const Input events[]={{0,juce::MidiMessage::noteOn(2,50,(juce::uint8)100)},
    {normal?48000:1000,juce::MidiMessage::noteOff(2,50,(juce::uint8)99)},{48000,juce::MidiMessage::controllerEvent(1,64,0)}};
   size_t index=0;
   for(int at=0;at<96000;at+=block){const int n=std::min(block,96000-at);audio.setSize(2,n,false,false,true);midi.clear();
    while(index<std::size(events)&&events[index].at<at+n){midi.addEvent(events[index].message,events[index].at-at);++index;}
    player.processBlock(audio,midi);for(int i=0;i<n;++i)result.push_back(audio.getSample(0,i));
   }return result;
  };
  const auto held=modeRender("normal",256);
  for(const char* mode:{"hold","toggle"}){const auto latched=modeRender(mode,257);double error=0;
   for(size_t i=0;i<held.size();++i)error=std::max(error,double(std::abs(held[i]-latched[i])));
   check(error<.0001,"Software latch audio differs from physical hold across buffer sizes");
  }
 }
 // Stopped-host keys use the same shared musical clock, at the host's exact
 // tempo (including changes while stopped), retaining it if tempo is omitted.
 auto stoppedRender=[](int block,bool stopped){
  DrummerProcessor player;
  // Isolate clock/voice timing from freely ringing per-sound reverb tails.
  auto dry=drummer::defaultDocument();auto keys=dry.getProperty("keys",{});
  for(auto& key:*keys.getArray()){
   auto sound=key.getProperty("sound",{}),settings=sound.getProperty("s",{});settings.getDynamicObject()->setProperty("reverbMix",0.);settings.getDynamicObject()->setProperty("delayMix",0.);
   auto routes=key.getProperty("routes",{});for(auto& route:*routes.getArray())if(route.getProperty("target",{}).toString()=="reverbMix"||route.getProperty("target",{}).toString()=="delayMix"){route.getDynamicObject()->setProperty("min",0.);route.getDynamicObject()->setProperty("max",0.);}
  }
  player.apply(drummer::compileDocument(dry));player.prepareToPlay(48000,2048);Head head;player.setPlayHead(&head);
  juce::AudioBuffer<float> audio(2,2048);juce::MidiBuffer midi;std::vector<float> result;
  struct Input{int at;juce::MidiMessage message;};
  const Input events[]={
   {0,juce::MidiMessage::noteOn(2,48,(juce::uint8)110)},
   {5000,juce::MidiMessage::noteOn(3,50,(juce::uint8)110)},
   {17000,juce::MidiMessage::controllerEvent(3,74,115)},
   {60000,juce::MidiMessage::noteOff(2,48)},
   {65000,juce::MidiMessage::noteOff(3,50)},
   {85000,juce::MidiMessage::noteOn(2,48,(juce::uint8)110)},
   {100000,juce::MidiMessage::allNotesOff(1)}};
  size_t event=0;double beat=3;
  for(int start=0;start<144000;){
   int next=144000;for(int edge:{24000,36000,48000,72000,96000})if(edge>start){next=edge;break;}
   const int n=std::min(block,next-start);audio.setSize(2,n,false,false,true);midi.clear();
   const double tempo=start<24000?93.75:137.5;
   if(start==48000)beat=16.;
   head.beat=beat;head.bpm=tempo;head.reportTempo=start<36000;
   head.playing=!stopped||(start>=48000&&start<72000);
   // Hosts typically keep reporting a stationary song position while stopped.
   if(!head.playing)head.beat=3.;
   while(event<std::size(events)&&events[event].at<start+n){midi.addEvent(events[event].message,events[event].at-start);++event;}
   player.processBlock(audio,midi);
   check(player.hostBpm.load()==tempo,"Stopped host lost current/last known tempo");
   check(player.hostPlaying.load()==head.playing,"Free clock incorrectly starts host transport");
   check(!player.overflow.load(),"Free-running scheduler overflow");
   if(start>=65000&&start+n<=85000)check(!player.freePlaying.load(),"Released keys left free-play indicator active");
   if(start>=5100&&start<16000&&stopped)check(player.freePlaying.load(),"Stopped-host key playback is not indicated");
   for(int i=0;i<n;++i)result.push_back(audio.getSample(0,i));
   beat+=n*tempo/(60*48000.);start+=n;
  }
  double energy=0;for(float sample:result)energy+=sample*sample;check(energy>.1,"Held keys are silent with stopped transport");
  for(size_t i=125000;i<result.size();++i)check(std::abs(result[i])<.00001,"Panic left stopped-host patterns sounding");
  return result;
 };
 const auto stoppedAudio=stoppedRender(64,true);
 for(int block:{64,257,2048}){
  const auto other=stoppedRender(block,true);double error=0;
  for(size_t i=0;i<other.size();++i)error=std::max(error,double(std::abs(other[i]-stoppedAudio[i])));
  check(error<.0001,"Stopped-host MPE/transport audio depends on buffer partition");
 }
 // Before the host resumes, free-running output matches the live host exactly.
 const auto liveAudio=stoppedRender(64,false);
 for(size_t i=0;i<48000;++i)check(std::abs(liveAudio[i]-stoppedAudio[i])<.0001,"Stopped-host patterns differ from host-timed patterns");
 // Key-body CC74 stays per-note; only the dedicated ribbon bands act globally.
 {
  auto config=doc.clone();config.getDynamicObject()->setProperty("switchGrid",0.);auto cc=config.getProperty("globalCC",{});(*cc.getArray())[0]=74;
  DrummerProcessor isolated;isolated.apply(drummer::compileDocument(config));isolated.prepareToPlay(48000,256);Head host;isolated.setPlayHead(&host);
  juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(2,48,(juce::uint8)100),0);midi.addEvent(juce::MidiMessage::noteOn(3,50,(juce::uint8)100),0);isolated.processBlock(audio,midi);
  midi.addEvent(juce::MidiMessage::controllerEvent(2,74,115),0);midi.addEvent(juce::MidiMessage::controllerEvent(1,74,64),1);host.beat=256/24000.;isolated.processBlock(audio,midi);
  check(isolated.contactSlide[1].load()==1&&isolated.contactSlide[2].load()==0,"Processor broadcasts slide between channels");
  check((isolated.contacts[1].load()>>8)==2&&(isolated.contacts[2].load()>>8)==0,"Processor changes both held pattern zones");
  check(isolated.parameters.getParameter("macro0")->getValue()==0,"CC74 drives a global macro");
  midi.addEvent(juce::MidiMessage::controllerEvent(3,74,64),0);host.beat=512/24000.;isolated.processBlock(audio,midi);
  check(isolated.contactSlide[1].load()==1&&std::abs(isolated.contactSlide[2].load()-48/99.f)<.01f,"Second finger overwrites first finger expression");
 }
 // Browsing/import history must not add work to the audio callback until a
 // pattern is actually bound. Aliases of a bound pattern still share one bus.
 auto libraryDoc=doc.clone();auto libraryClips=libraryDoc.getProperty("clips",{});auto extra=(*libraryClips.getArray())[0].clone();extra.getDynamicObject()->setProperty("id","unused-choice");extra.getDynamicObject()->setProperty("length",7.5);libraryClips.getArray()->add(extra);
 check(drummer::compileDocument(libraryDoc)->config.clips.size()==3,"Unbound library pattern consumes a clip bus");
 auto libraryKeys=libraryDoc.getProperty("keys",{});auto keyZones=(*libraryKeys.getArray())[0].getProperty("zones",{});(*keyZones.getArray())[0].getDynamicObject()->setProperty("clipId","unused-choice");
 check(drummer::compileDocument(libraryDoc)->config.clips.size()==4,"Newly bound pattern lacks a clip bus");
 auto alias=extra.clone();alias.getDynamicObject()->setProperty("id","same-content");libraryClips.getArray()->add(alias);auto otherZones=(*libraryKeys.getArray())[1].getProperty("zones",{});(*otherZones.getArray())[0].getDynamicObject()->setProperty("clipId","same-content");
 check(drummer::compileDocument(libraryDoc)->config.clips.size()==4,"Pattern aliases created duplicate buses");
 DrummerProcessor p;Head head;p.prepareToPlay(48000,256);check(render(p,head,false)==0,"Idle instrument makes sound");p.prepareToPlay(48000,256);head.beat=0;double dry=render(p,head,true);check(dry>1,"Held kick makes no sound");
 juce::MemoryBlock state;p.getStateInformation(state);DrummerProcessor restored;restored.setStateInformation(state.getData(),(int)state.getSize());restored.prepareToPlay(48000,256);head.beat=0;double recalled=render(restored,head,true);check(std::abs(dry-recalled)<.001,"Editor-free restoration changes audio");
 // Full-processor offline rendering: fifteen contacts, expression, global CC,
 // tempo changes, a backwards seek, stop/resume and widely different blocks.
 auto stress=[](int block){
  DrummerProcessor player;auto doc=drummer::defaultDocument();doc.getDynamicObject()->setProperty("globalRoutes",juce::JSON::parse("[{\"source\":0,\"target\":\"volume\",\"min\":0.2,\"max\":0.9,\"smoothing\":8}]"));player.apply(drummer::compileDocument(doc));player.prepareToPlay(48000,2048);Head transport;player.setPlayHead(&transport);
  struct Input{int frame;juce::MidiMessage message;};std::vector<Input> events;
  for(int ch=2;ch<=16;++ch)events.push_back({0,juce::MidiMessage::noteOn(ch,std::array<int,6>{48,50,52,53,55,57}[(size_t)(ch%6)],(juce::uint8)(80+ch))});
  for(int frame=97;frame<85000;frame+=97){const int ch=2+(frame/97)%15;events.push_back({frame,juce::MidiMessage::channelPressureChange(ch,(frame/97)%128)});events.push_back({frame,juce::MidiMessage::controllerEvent(ch,74,(frame/193)%128)});if(frame%291==0)events.push_back({frame,juce::MidiMessage::pitchWheel(ch,7000+(frame%2300))});if(frame%485==0)events.push_back({frame,juce::MidiMessage::controllerEvent(1,107,(frame/485)%128)});}
  for(int ch=2;ch<=16;++ch)events.push_back({85000,juce::MidiMessage::noteOff(ch,std::array<int,6>{48,50,52,53,55,57}[(size_t)(ch%6)],(juce::uint8)90)});
  struct Segment{int frame;double ppq,bpm;bool playing;};const Segment segments[]={{0,0,120,true},{24000,1,137,true},{60000,-.5,137,true},{72000,0,137,false},{78000,0,137,true},{96000,0,137,false}};
  int segment=0;size_t event=0;std::vector<float> result;result.reserve(96000*2);juce::AudioBuffer<float> buffer(2,2048);juce::MidiBuffer midi;
  for(int start=0;start<96000;){while(segments[segment+1].frame<=start)++segment;const auto& current=segments[segment];const int n=std::min({block,96000-start,segments[segment+1].frame-start});buffer.setSize(2,n,false,false,true);midi.clear();
   while(event<events.size()&&events[event].frame<start+n){midi.addEvent(events[event].message,events[event].frame-start);++event;}
   transport.bpm=current.bpm;transport.playing=current.playing;transport.beat=current.ppq+(current.playing?(start-current.frame)*current.bpm/(60*48000.):0);player.processBlock(buffer,midi);
   check(!player.overflow.load(),"Dense MPE input overflowed unexpectedly");if(current.playing)check(player.clipEvaluations.load()<=3,"Clip evaluation count scales with held keys");
   for(int i=0;i<n;++i)for(int ch=0;ch<2;++ch){const float sample=buffer.getSample(ch,i);check(std::isfinite(sample)&&std::abs(sample)<100,"Dense offline audio is invalid");result.push_back(sample);}start+=n;
  }
  return result;
 };
 const auto dense=stress(64);for(int block:{257,2048}){const auto other=stress(block);double error=0;size_t first=dense.size(),worst=0;for(size_t i=0;i<dense.size();++i){auto d=double(std::abs(dense[i]-other[i]));if(d>.0001&&first==dense.size())first=i;if(d>error){error=d;worst=i;}}if(error>=.0001)std::cerr<<"block="<<block<<" first frame="<<first/2<<" worst frame="<<worst/2<<" error="<<error<<" samples="<<dense[worst]<<","<<other[worst]<<"\n";check(error<.0001,"Full-processor MPE audio depends on host buffer partition");}
 // Controller CC changes must survive the next block and session recall.
 auto controllerDoc=drummer::defaultDocument();controllerDoc.getDynamicObject()->setProperty("globalRoutes",juce::JSON::parse("[{\"source\":0,\"target\":\"volume\",\"min\":0,\"max\":1,\"smoothing\":0}]"));
 DrummerProcessor controlled;controlled.apply(drummer::compileDocument(controllerDoc));controlled.prepareToPlay(48000,256);controlled.setPlayHead(&head);
 juce::AudioBuffer<float> ccAudio(2,256);juce::MidiBuffer ccMidi;head.beat=0;
 ccMidi.addEvent(juce::MidiMessage::controllerEvent(1,107,127),0);controlled.processBlock(ccAudio,ccMidi);
 check(controlled.parameters.getParameter("macro0")->getValue()==1.f,"CC did not set controller value");
 controlled.parameters.getParameter("macro1")->setValueNotifyingHost(37.f/127.f);check(std::abs(controlled.parameters.getParameter("macro1")->getValue()-37.f/127.f)<1e-7f,"Controller parameter rounds away MIDI precision");
 head.beat=256/24000.;controlled.processBlock(ccAudio,ccMidi);
 check(render(controlled,head,true)>.5,"CC controller reverted on the next block");
 juce::MemoryBlock controllerState;controlled.getStateInformation(controllerState);
 controlled.parameters.getParameter("macro0")->setValueNotifyingHost(0.f);
 controlled.setStateInformation(controllerState.getData(),(int)controllerState.getSize());
 check(controlled.parameters.getParameter("macro0")->getValue()==1.f,"Session did not restore MIDI CC value");
 controlled.parameters.getParameter("macro0")->setValueNotifyingHost(0.f);controlled.prepareToPlay(48000,256);
 check(render(controlled,head,true)==0.,"Host automation did not override the last CC");
 // Restore a cached zero after another CC: APVTS itself may skip equal values.
 juce::MemoryBlock zeroState;controlled.getStateInformation(zeroState);
 ccMidi.addEvent(juce::MidiMessage::controllerEvent(1,107,127),0);controlled.processBlock(ccAudio,ccMidi);
 controlled.setStateInformation(zeroState.getData(),(int)zeroState.getSize());
 check(controlled.parameters.getParameter("macro0")->getValue()==0.f,"Recall used a stale parameter cache");
 auto path=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("rolidrummer-store-",".sqlite");
 {drummer::SharedStore a(path),b(path);auto ops=juce::JSON::parse("[{\"key\":\"camera\",\"value\":{\"x\":5},\"revision\":0}]");check((bool)a.commit(ops).getProperty("ok",false),"First settings write failed");check(!(bool)b.commit(ops).getProperty("ok",true),"Stale writer overwrote camera");check(b.snapshot().getArray()->size()==1,"Instance cannot see shared settings");
 juce::ChildProcess child;juce::StringArray args{juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),"write",path.getFullPathName(),"other-process"};check(child.start(args),"Could not start cross-process settings test");check(child.waitForProcessToFinish(15000),"Settings writer hung");check(child.getExitCode()==0,"Cross-process writer failed");check(a.snapshot().getArray()->size()==2,"Cross-process settings not observed");
 std::array<juce::ChildProcess,3> contenders;for(auto& writer:contenders)check(writer.start(juce::StringArray{juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),"write",path.getFullPathName(),"contended-layout"}),"Could not start competing settings writer");
 int accepted=0;for(auto& writer:contenders){check(writer.waitForProcessToFinish(15000),"Competing settings writer hung");accepted+=writer.getExitCode()==0?1:0;}check(accepted==1,"Concurrent stale writers were not rejected atomically");
 }
 juce::MidiMessageSequence track;track.addEvent(juce::MidiMessage::noteOn(10,42,(juce::uint8)97),12);track.addEvent(juce::MidiMessage::noteOff(10,42,(juce::uint8)64),120);track.addEvent(juce::MidiMessage::endOfTrack(),7680);juce::MidiFile file;file.setTicksPerQuarterNote(960);file.addTrack(track);auto midiPath=path.withFileExtension("mid");{juce::FileOutputStream out(midiPath);check(file.writeTo(out),"MIDI fixture write failed");}auto imported=drummer::importMidi(midiPath);auto notes=imported.getProperty("notes",{});check(std::abs((double)(*notes.getArray())[0].getProperty("beat",0)-.0125)<1e-10,"MIDI human timing lost");check((double)imported.getProperty("length",0)==8,"MIDI trailing silence lost");
 // Multitrack imports expose exact choices instead of silently mixing parts.
 juce::MidiMessageSequence melody,drums;
 melody.addEvent(juce::MidiMessage::textMetaEvent(3,"Melody"),0);
 melody.addEvent(juce::MidiMessage::noteOn(2,60,(juce::uint8)80),24);melody.addEvent(juce::MidiMessage::noteOff(2,60),240);melody.addEvent(juce::MidiMessage::endOfTrack(),7680);
 drums.addEvent(juce::MidiMessage::textMetaEvent(3,"Drums"),0);
 drums.addEvent(juce::MidiMessage::noteOn(10,36,(juce::uint8)110),48);drums.addEvent(juce::MidiMessage::noteOff(10,36),120);
 juce::MidiFile multitrack;multitrack.setTicksPerQuarterNote(960);multitrack.addTrack(melody);multitrack.addTrack(drums);
 auto multiPath=path.getSiblingFile(path.getFileNameWithoutExtension()+"-multi.mid");{juce::FileOutputStream out(multiPath);check(multitrack.writeTo(out),"Multitrack fixture write failed");}
 auto info=drummer::inspectMidi(multiPath);check(info.getProperty("channels",{}).getArray()->size()==2,"MIDI channel inspection lost channels");check((int)info.getProperty("autoChannel",0)==10,"Drum channel not suggested");
 auto tracks=info.getProperty("tracks",{});check((*tracks.getArray())[1].getProperty("name",{}).toString()=="Drums","MIDI track names lost");
 auto automatic=drummer::importMidi(multiPath);auto autoNotes=automatic.getProperty("notes",{});check(autoNotes.getArray()->size()==1&&(int)(*autoNotes.getArray())[0].getProperty("note",0)==36,"Automatic import did not isolate drums");
 auto picked=drummer::importMidi(multiPath,2,0);auto pickedNotes=picked.getProperty("notes",{});check(pickedNotes.getArray()->size()==1&&(int)(*pickedNotes.getArray())[0].getProperty("note",0)==60,"Explicit track/channel selection ignored");check(std::abs((double)(*pickedNotes.getArray())[0].getProperty("duration",0)-.225)<1e-10,"Selected note duration changed");
 check((double)picked.getProperty("length",0)==8,"Track selection lost the file's trailing silence");
 auto all=drummer::importMidi(multiPath,0,-1);check(all.getProperty("notes",{}).getArray()->size()==2,"All-channel import omitted notes");
 bool emptyRejected=false;try{drummer::importMidi(multiPath,2,1);}catch(const std::exception&){emptyRejected=true;}check(emptyRejected,"Empty channel/track selection accepted");
 // No channel 10: auto selection must fail rather than guess between parts.
 juce::MidiMessageSequence other;other.addEvent(juce::MidiMessage::noteOn(3,42,(juce::uint8)80),0);other.addEvent(juce::MidiMessage::noteOff(3,42),120);
 juce::MidiFile ambiguous;ambiguous.setTicksPerQuarterNote(960);ambiguous.addTrack(melody);ambiguous.addTrack(other);
 auto ambiguousPath=path.getSiblingFile(path.getFileNameWithoutExtension()+"-ambiguous.mid");{juce::FileOutputStream out(ambiguousPath);check(ambiguous.writeTo(out),"Ambiguous fixture write failed");}
 check((int)drummer::inspectMidi(ambiguousPath).getProperty("autoChannel",0)==-1,"Ambiguous import guessed a channel");
 bool ambiguousRejected=false;try{drummer::importMidi(ambiguousPath);}catch(const std::exception&){ambiguousRejected=true;}check(ambiguousRejected,"Ambiguous MIDI did not require selection");
 check((int)drummer::importMidi(ambiguousPath,-1,1).getProperty("channel",0)==3,"Automatic channel within selected track failed");
 // A real sample must survive both an absent original and a fresh process/cache.
 auto fixture=path.withFileExtension("wav");
 juce::AudioBuffer<float> tone(1,4800);for(int i=0;i<4800;++i)tone.setSample(0,i,(float)(std::sin(i*.07)*std::exp(-i/1300.)));
 {juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(new juce::FileOutputStream(fixture),48000,1,24,{},0));check(writer&&writer->writeFromAudioSampleBuffer(tone,0,tone.getNumSamples()),"Could not create sample fixture");}
 auto external=drummer::defaultDocument();auto externalKeys=external.getProperty("keys",{});auto externalSound=(*externalKeys.getArray())[0].getProperty("sound",{});externalSound.getDynamicObject()->setProperty("path",fixture.getFullPathName());
 auto prepared=drummer::compileDocument(external);auto canonical=juce::JSON::parse(prepared->json);auto canonicalKeys=canonical.getProperty("keys",{});auto canonicalSound=(*canonicalKeys.getArray())[0].getProperty("sound",{});auto assetId=canonicalSound.getProperty("asset",{}).toString();
 check(assetId.isNotEmpty(),"Assigned sound lacks content identity");
 auto second=drummer::compileDocument(canonical);check(second->sounds[48]->sample==prepared->sounds[48]->sample,"Sample is decoded again on a control edit");
 DrummerProcessor samplePlayer;samplePlayer.apply(std::move(prepared));
 juce::MemoryBlock portableState;samplePlayer.getStateInformation(portableState);
 auto statePath=path.withFileExtension("state");check(statePath.replaceWithData(portableState.getData(),portableState.getSize()),"Cannot write state fixture");
 auto stateTree=juce::ValueTree::readFromData(portableState.getData(),portableState.getSize());check(stateTree.getChildWithName("Assets").getNumChildren()==1,"Session does not deduplicate sound assets");
 auto portablePath=path.withFileExtension("rolidrummer");drummer::savePortablePatch(portablePath,canonical);
 check(fixture.deleteFile(),"Could not remove original test sound");
 check(drummer::compileDocument(canonical)->sounds[48]!=nullptr,"Missing original breaks cached kit");
 auto childCheck=[&](const juce::String&mode,const juce::File&source){
  check(drummer::assetFile(assetId).deleteFile(),"Could not remove test cache");
  juce::ChildProcess child;check(child.start(juce::StringArray{juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),mode,source.getFullPathName()}),"Could not start asset restore child");check(child.waitForProcessToFinish(15000),"Asset restore child hung");auto output=child.readAllProcessOutput();check(child.getExitCode()==0,("Asset restore child failed: "+output).toRawUTF8());check(drummer::assetFile(assetId).existsAsFile(),"Embedded asset was not reconstructed");
 };
 childCheck("restore",statePath);childCheck("portable",portablePath);
 auto missing=external.clone();auto missingKeys=missing.getProperty("keys",{});auto missingSound=(*missingKeys.getArray())[0].getProperty("sound",{});missingSound.getDynamicObject()->setProperty("path",fixture.getSiblingFile("never-created.wav").getFullPathName());auto incomplete=drummer::compileDocument(missing);check(!incomplete->sounds[48]&&incomplete->sounds[50],"One missing sound prevents the rest of the kit loading");check(incomplete->json.contains("missing"),"Missing sound is not reported for relinking");
 const auto revision=samplePlayer.stateRevision.load();check(!samplePlayer.apply(drummer::compileDocument(doc),revision-1),"Stale editor overwrote a restored document");check(samplePlayer.stateRevision.load()==revision,"Rejected document changed the revision");
 // Actual archive -> catalog -> cached clip -> processor -> audio, including
 // independent part gates and session recall after the source is removed.
 {
  auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("rd-library-audio","");root.createDirectory();
  auto source=root.getChildFile("patterns.zip");
  juce::MidiMessageSequence sequence;
  for(int note:{36,38,42}){sequence.addEvent(juce::MidiMessage::noteOn(10,note,(juce::uint8)100),0);sequence.addEvent(juce::MidiMessage::noteOff(10,note),96);}
  sequence.addEvent(juce::MidiMessage::endOfTrack(),3840);
  juce::MidiFile file;file.setTicksPerQuarterNote(960);file.addTrack(sequence);juce::MemoryOutputStream midi;check(file.writeTo(midi),"Library audio MIDI write failed");
  {juce::ZipFile::Builder archive;archive.addEntry(std::make_unique<juce::MemoryInputStream>(midi.getMemoryBlock(),true),6,"Hard Rock/Groove.mid",juce::Time::getCurrentTime());
   for(int variant=0;variant<14;++variant){
    juce::MidiMessageSequence events;
    for(int i=0;i<16;++i){const int note=i%4==0?36:i%4==2?38:42;const double tick=i*240.+variant*2.;
     events.addEvent(juce::MidiMessage::noteOn(10,note,(juce::uint8)(65+(variant*7+i*3)%55)),tick);events.addEvent(juce::MidiMessage::noteOff(10,note),tick+70+variant);}
    events.addEvent(juce::MidiMessage::endOfTrack(),3840);juce::MidiFile variation;variation.setTicksPerQuarterNote(960);variation.addTrack(events);juce::MemoryOutputStream data;check(variation.writeTo(data),"Variation MIDI write failed");
    archive.addEntry(std::make_unique<juce::MemoryInputStream>(data.getMemoryBlock(),true),6,"Hard Rock/Grooves/Variation "+juce::String(variant)+".mid",juce::Time::getCurrentTime());
   }
   juce::FileOutputStream output(source);check(archive.writeToStream(output,nullptr),"Library audio ZIP write failed");}
  drummer::PatternLibrary catalog(root.getChildFile("catalog.sqlite"));catalog.importSource(source);
  auto results=catalog.browse(juce::JSON::parse(R"({"query":"hard rock"})"));auto rows=results.getProperty("rows",{});
  check(rows.getArray()&&rows.getArray()->size()==15,"Real catalog query did not find its distinct pattern family");
  auto pattern=catalog.pattern((*rows.getArray())[0].getProperty("id",{}).toString());
  auto makePatch=[&](bool hatsOnly){auto patch=doc.clone();auto clip=pattern.clone();
   if(hatsOnly){auto notes=clip.getProperty("notes",{});juce::Array<juce::var> keep;for(const auto&note:*notes.getArray())if((int)note.getProperty("note",0)==42)keep.add(note);clip.getDynamicObject()->setProperty("notes",keep);}
   patch.getDynamicObject()->setProperty("clips",juce::Array<juce::var>{clip});auto keys=patch.getProperty("keys",{});
   for(auto&key:*keys.getArray()){auto*zone=new juce::DynamicObject;zone->setProperty("upper",1.);zone->setProperty("clipId",clip.getProperty("id",{}));key.getDynamicObject()->setProperty("zones",juce::Array<juce::var>{juce::var(zone)});}return patch;};
  auto full=makePatch(false),filtered=makePatch(true);
  if(drummer::neuralAssetsDirectory().getChildFile("manifest.json").existsAsFile()){
   drummer::NeuralIndex index(root.getChildFile("catalog.sqlite"));index.build();
   auto query=juce::JSON::parse(R"({"query":"Heavy Rock","variationCount":2,"seed":"audio-proof"})");
   auto retrieved=index.search(query);auto matches=retrieved.getProperty("rows",{});
   check(matches.getArray()&&matches.getArray()->size()==15,"Native pretrained query did not retrieve the archive family");
   full=drummer::resolveNeuralPatch(catalog,index,full,query);
   drummer::PatternFamilies families(catalog.file());families.registerFamily(full.getProperty("neural",{}));families.reconcile(index);
   full=drummer::resolveNeuralPatch(catalog,index,full,query);
   check(full.getProperty("neural",{}).getProperty("familyBuildId",{}).toString().isNotEmpty(),"Rendered family did not pin its membership snapshot");
   check(full.getProperty("neural",{}).getProperty("query",{}).toString()=="Heavy Rock","Named query resolution was not persisted");
   check((int)full.getProperty("neural",{}).getProperty("generatedCount",0)>0,"Archive query did not generate compatible variations");
   // Render a generated hat candidate explicitly, then verify exact session
   // recall; availability in a candidate list alone is not playback evidence.
   auto keys=full.getProperty("keys",{});juce::String generatedHat;
   for(auto& key:*keys.getArray())if((int)key.getProperty("note",0)==49){
    const auto zones=key.getProperty("zones",{});
    for(const auto& zone:*zones.getArray()){const auto choices=zone.getProperty("candidates",{});if(auto* candidates=choices.getArray())for(const auto& candidate:*candidates)if((bool)candidate.getProperty("generated",false))generatedHat=candidate.getProperty("clipId",{}).toString();}
    check(generatedHat.isNotEmpty(),"Resolved hat part lacks a generated performance");
    auto* zone=new juce::DynamicObject;zone->setProperty("upper",1.0);zone->setProperty("clipId",generatedHat);key.getDynamicObject()->setProperty("zones",juce::Array<juce::var>{juce::var(zone)});
   }
   const auto portable=root.getChildFile("generated.rolidrummer");drummer::savePortablePatch(portable,full);
   check(juce::JSON::toString(drummer::loadPatch(portable).getProperty("clips",{}),true)==juce::JSON::toString(full.getProperty("clips",{}),true),"Portable patch lost generated notes or provenance");
   filtered=full.clone();auto clips=filtered.getProperty("clips",{});
   for(auto& clip:*clips.getArray()){auto sourceNotes=clip.getProperty("notes",{});juce::Array<juce::var> hats;for(const auto&note:*sourceNotes.getArray())if((int)note.getProperty("note",0)==42)hats.add(note);clip.getDynamicObject()->setProperty("notes",hats);}
  }
  DrummerProcessor player,control;player.apply(drummer::compileDocument(full));control.apply(drummer::compileDocument(filtered));player.prepareToPlay(48000,256);control.prepareToPlay(48000,256);Head a,b;
  const auto actual=render(player,a,true,2,49),expected=render(control,b,true,2,49);
  check(actual>.001&&std::abs(actual-expected)<1e-8,"Catalog playback leaked kick/snare events into a held hat key");
  juce::MemoryBlock session;player.getStateInformation(session);check(source.deleteFile(),"Library audio source could not be removed");
  DrummerProcessor restored;restored.setStateInformation(session.getData(),(int)session.getSize());restored.prepareToPlay(48000,256);Head c;
  check(std::abs(render(restored,c,true,2,49)-expected)<1e-8,"Catalog session recall depends on the original archive");
  root.deleteRecursively();
 }
 std::cout<<"integration: audio, session recall, cross-process settings, MIDI precision, shared sample cache, embedded DAW assets, portable archives, missing-sound recovery, stale-document rejection and cached library playback passed\n";
 return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
