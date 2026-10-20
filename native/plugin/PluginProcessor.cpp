#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <drummer/Assets.h>
#include <drummer/PatchBank.h>
#include <cmath>
namespace {
juce::AudioProcessorValueTreeState::ParameterLayout layout(){juce::AudioProcessorValueTreeState::ParameterLayout p;p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"volume",1},"Output",juce::NormalisableRange<float>{0.f,1.5f},.8f));for(int i=0;i<5;++i)p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"macro"+juce::String(i),1},"Controller "+juce::String(i+1),juce::NormalisableRange<float>{0.f,1.f},0.f));return p;}
std::array<float,5> values(const drummer::Expression&e){return{e.strike,e.glide,e.slide,e.press,e.lift};}
}
DrummerProcessor::DrummerProcessor():AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)),parameters(*this,nullptr,"Automation",layout()){
 for(int i=0;i<5;++i){auto id="macro"+juce::String(i);macroParameters[(size_t)i]=parameters.getParameter(id);}
 outputVolume=parameters.getRawParameterValue("volume");
 apply(drummer::compileDocument(drummer::defaultDocument()));
 try{shared=std::make_unique<drummer::SharedStore>(drummer::dataDirectory().getChildFile("shared.sqlite"));}catch(const std::exception&e){juce::Logger::writeToLog(e.what());}
 try{library=drummer::LibraryService::acquire(drummer::dataDirectory().getChildFile("library.sqlite"));}catch(const std::exception&e){juce::Logger::writeToLog("Pattern library: "+juce::String(e.what()));}
 startTimerHz(10);
}
DrummerProcessor::~DrummerProcessor(){stopTimer();life.reset();jobs.removeAllJobs(true,-1);neuralJobs.removeAllJobs(true,-1);delete incoming.exchange(nullptr);delete pendingRuntime;delete active;delete previewIncoming.exchange(nullptr);delete previewActive;for(auto r=previewRead.load();r!=previewWrite.load();++r)delete previewRetired[r%previewRetired.size()];for(auto r=retiredRead.load();r!=retiredWrite.load();++r)delete retired[r%retired.size()];}
bool DrummerProcessor::apply(std::unique_ptr<drummer::Runtime> r, std::optional<uint64_t> expected,double activationGrid,bool preserveActivation){
 std::lock_guard guard(stateMutex);
 if(expected && *expected!=stateRevision.load())return false;
 if(!std::isfinite(activationGrid)||activationGrid<0||activationGrid>256)throw std::runtime_error("Invalid performance activation grid");
 if(preserveActivation&&activationGrid==0&&activationPending())activationGrid=queuedActivationGrid;
 r->activationGrid=queuedActivationGrid=activationGrid;
 engine.preparePerformance(r->performance);
 ++kitPreviewRequest;kitPreviewRestore.reset();
 r->revision=stateRevision.load()+1;
 saved=r->json;
 const auto revision=r->revision;
 boundaryRevision.store(activationGrid>0?revision:0,std::memory_order_release);
 if(auto*old=incoming.exchange(r.release()))delete old;
 stateRevision.store(revision);
 return true;
}
juce::String DrummerProcessor::document(){std::lock_guard guard(stateMutex);return saved;}
uint64_t DrummerProcessor::beginKitPreview(){std::lock_guard guard(stateMutex);return ++kitPreviewRequest;}
bool DrummerProcessor::previewKit(const juce::var& kit,uint64_t expected,uint64_t ticket){
 juce::String committed;
 {
  std::lock_guard guard(stateMutex);
  if(expected!=stateRevision.load()||ticket!=kitPreviewRequest)return false;
  if(activationPending())throw std::runtime_error("Wait for the queued patch to start before previewing a kit.");
  committed=saved;
 }
 auto candidate=juce::JSON::parse(committed);
 const auto assignments=kit.getProperty("keys",{});
 if((int)kit.getProperty("version",0)!=1||!assignments.isArray()||assignments.getArray()->isEmpty())throw std::runtime_error("This is not a supported sound kit.");
 std::array<bool,128> assigned{};
 auto keys=candidate.getProperty("keys",{});
 for(const auto& assignment:*assignments.getArray()){
  const auto value=assignment.getProperty("note",{});const double number=(double)value;
  const auto sound=assignment.getProperty("sound",{});
  if(!value.isInt()&&!value.isInt64()&&!value.isDouble())throw std::runtime_error("Invalid preview keywave.");
  if(!std::isfinite(number)||number<0||number>127||std::floor(number)!=number)throw std::runtime_error("Invalid preview keywave.");
  const int note=(int)number;
  if(assigned[(size_t)note]||!sound.getProperty("path",{}).isString()||!sound.getProperty("s",{}).isObject())throw std::runtime_error("Invalid preview sound.");
  assigned[(size_t)note]=true;bool found=false;
  for(auto& key:*keys.getArray())if((int)key.getProperty("note",-1)==note){key.getDynamicObject()->setProperty("sound",sound.clone());found=true;break;}
  if(!found)throw std::runtime_error("The preview keywave is no longer assigned.");
 }
 // Prepare both directions off audio; Cancel never needs to load a sample.
 auto restore=drummer::compileDocument(juce::JSON::parse(committed));
 auto audition=drummer::compileDocument(candidate);
 std::lock_guard guard(stateMutex);
 if(expected!=stateRevision.load()||ticket!=kitPreviewRequest)return false;
 engine.preparePerformance(restore->performance);engine.preparePerformance(audition->performance);
 restore->revision=audition->revision=expected;
 kitPreviewRestore=std::move(restore);
 if(auto* old=incoming.exchange(audition.release()))delete old;
 return true;
}
void DrummerProcessor::cancelKitPreview(){
 std::lock_guard guard(stateMutex);++kitPreviewRequest;
 if(kitPreviewRestore)if(auto* old=incoming.exchange(kitPreviewRestore.release()))delete old;
}
void DrummerProcessor::prepareToPlay(double sr,int max){rate=sr;previewRate=sr;previewBlock=max;stopPreview();engine.prepare(sr,max);rightScratch.resize((size_t)std::max(max,1));scheduler.reset();standalonePpq=0;monitorBeat=monitorSegment=0;++monitorEpoch;lastPatterns={};publishFingers();wasHostPlaying=false;freePlaying=false;activationScheduled=activationClockKnown=false;}
bool DrummerProcessor::isBusesLayoutSupported(const BusesLayout&l)const{return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()||l.getMainOutputChannelSet()==juce::AudioChannelSet::mono();}
void DrummerProcessor::renderTo(int end){if(end<=rendered)return;engine.process(outputL+rendered,outputR+rendered,end-rendered,nullptr,0,{clock.bpm,clock.ppq+(rendered-eventBase)*clock.bpm/(60*rate),clock.playing});rendered=end;}
void DrummerProcessor::emit(void*ctx,const drummer::Event&e){auto&p=*static_cast<DrummerProcessor*>(ctx);p.renderTo(e.offset+p.eventBase);
 drummer::PlayedEvent view;view.epoch=p.monitorEpoch;view.beat=p.monitorSegment+e.offset*p.clock.bpm/(60*p.rate);view.owner=e.owner;view.token=e.token;view.key=e.key;view.inputNote=e.inputNote;view.note=e.drumNote;view.duration=e.duration;view.velocity=e.velocity;
 bool record=true;
 switch(e.type){
  case drummer::Event::hit:view.type=drummer::PlayedEvent::on;break;
  case drummer::Event::endHit:view.type=drummer::PlayedEvent::off;break;
  case drummer::Event::killContact:view.type=drummer::PlayedEvent::ownerOff;break;
  case drummer::Event::releaseContact:record=p.active&&p.active->config.hardRelease;view.type=drummer::PlayedEvent::ownerOff;break;
  case drummer::Event::panic:view.type=drummer::PlayedEvent::allOff;break;
  default:record=false;break;
 }
 if(record)p.performanceMonitor.push(view);
 if(e.type==drummer::Event::releaseContact||e.type==drummer::Event::killContact){
  auto remember=[&](const drummer::Contact& f){if(f.owner==e.owner&&f.held)p.rememberPattern(f,p.clock.ppq+e.offset*p.clock.bpm/(60*p.rate),false);};
  for(const auto& f:p.scheduler.contacts())remember(f);
  for(const auto& f:p.scheduler.sustainedContacts())remember(f);
 }
 switch(e.type){
 case drummer::Event::hit:if(p.active)p.engine.drumHit(p.active->sounds[(size_t)e.key],e.owner,e.token,e.velocity,values(e.values));break;
 case drummer::Event::endHit:p.engine.drumEnd(e.token);break;
 case drummer::Event::expression:p.engine.drumExpression(e.owner,values(e.values));break;
 case drummer::Event::releaseContact:p.engine.drumRelease(e.owner,values(e.values),p.active->config.hardRelease);break;
 case drummer::Event::killContact:p.engine.drumRelease(e.owner,values(e.values),true);break;
 case drummer::Event::switchZone:p.engine.drumRelease(e.owner,values(e.values),false);break;
 case drummer::Event::panic:p.engine.drumPanic();break;
 case drummer::Event::globalCC:for(int i=0;i<5;++i)if(p.active->cc[(size_t)i]==e.cc){p.engine.drumGlobal(i,e.value);p.macroParameters[(size_t)i]->setValue(e.value);}break;
}}
void DrummerProcessor::publishPreview(std::unique_ptr<drummer::Preview> next,uint64_t request){if(request!=previewRequest.load())return;if(auto*old=previewIncoming.exchange(next.release()))delete old;previewStop=false;}
void DrummerProcessor::stopPreview(){++previewRequest;previewStop=true;if(auto*old=previewIncoming.exchange(nullptr))delete old;}
void DrummerProcessor::processBlock(juce::AudioBuffer<float>&buffer,juce::MidiBuffer&midi){
 juce::ScopedNoDenormals denormals;const int n=buffer.getNumSamples();if(n<=0||buffer.getNumChannels()==0)return;
 if(buffer.getNumChannels()==1 && n>(int)rightScratch.size()){buffer.clear();return;}
 outputL=buffer.getWritePointer(0);outputR=buffer.getNumChannels()>1?buffer.getWritePointer(1):rightScratch.data();rendered=eventBase=0;monitorSegment=monitorBeat;
 engine.process(outputL,outputR,0,nullptr,0,{});
 unsigned write=retiredWrite.load(std::memory_order_relaxed);
 auto room=[&](unsigned needed){return write-retiredRead.load(std::memory_order_acquire)+needed<=retired.size();};
 auto retire=[&](drummer::Runtime* previous){if(previous){retired[write%retired.size()]=previous;retiredWrite.store(++write,std::memory_order_release);}};
 if(room(2))if(auto*next=incoming.exchange(nullptr)){retire(pendingRuntime);pendingRuntime=next;activationScheduled=false;}
 auto activate=[&](double beat){
  if(!pendingRuntime||!engine.canActivatePerformance()||!room(1))return false;
  auto* previous=active;active=pendingRuntime;pendingRuntime=nullptr;activationScheduled=false;
  engine.activatePerformance(active->performance);
  // Keep the previous runtime owned until contact remapping has finished.
  if(previous&&previous->patternHash!=active->patternHash){scheduler.reconfigure(previous->config,active->config,beat,emit,this);lastPatterns={};}
  retire(previous);activeRevision.store(active->revision,std::memory_order_release);return true;
 };
 const bool immediate=pendingRuntime&&(!active||pendingRuntime->activationGrid<=0||!scheduler.hasHeldNotes());
 const auto* timing=immediate?pendingRuntime:active;
 clock={standalonePpq,hostBpm.load(),rate,true};
 bool transportPlaying=false;
 if(wrapperType==wrapperType_Standalone){if(timing)clock.bpm=timing->performance->bpm;transportPlaying=standalonePlaying.load();}
 else if(auto*head=getPlayHead())if(auto pos=head->getPosition()){
  if(auto bpm=pos->getBpm();bpm&&std::isfinite(*bpm)&&*bpm>0)clock.bpm=*bpm;
  transportPlaying=pos->getIsPlaying();
  if(transportPlaying)if(auto ppq=pos->getPpqPosition();ppq&&std::isfinite(*ppq))clock.ppq=*ppq;
 }
 clock.discontinuity=transportPlaying&&!wasHostPlaying;wasHostPlaying=transportPlaying;
 const double initialStep=clock.bpm/(60*rate);
 if(clock.discontinuity||(activationClockKnown&&std::abs(clock.ppq-activationExpected)>initialStep*2))activationScheduled=false;
 hostPpq=clock.ppq;hostPlaying=transportPlaying;
 engine.process(outputL,outputR,0,nullptr,0,{clock.bpm,clock.ppq,clock.playing});
 if(immediate)activate(clock.ppq);
 if(!active){buffer.clear();midi.clear();return;}
 if(wrapperType==wrapperType_Standalone)clock.bpm=active->performance->bpm;
 int switchAt=n;
 if(pendingRuntime&&pendingRuntime->activationGrid>0){
  const double step=clock.bpm/(60*rate),grid=pendingRuntime->activationGrid;
  if(!activationScheduled){activationBeat=std::ceil((clock.ppq-1e-10)/grid)*grid;activationScheduled=true;}
  if(activationBeat<clock.ppq-step*.5){activationScheduled=false;activationBeat=std::ceil((clock.ppq+1e-10)/grid)*grid;activationScheduled=true;}
  const double position=(activationBeat-clock.ppq)/step;
  if(position<n-.5-1e-9){
   if(engine.canActivatePerformance()&&room(1))switchAt=position<=0?0:std::clamp((int)std::llround(position),0,n-1);
   else activationScheduled=false; // Wait for another musical boundary.
  }
 }
 if(switchAt==0){
  if(activate(clock.ppq)&&wrapperType==wrapperType_Standalone)clock.bpm=active->performance->bpm;
  else if(pendingRuntime)activationScheduled=false;
  switchAt=n;
 }
 int count=0;bool full=false;
 // Screen-only ribbon packets share the MIDI queue to preserve gesture order;
 // the private 0xf4 packet is never sent to a host or interpreted as real MIDI.
 ssb::MidiMessage screen;while(screenMidi.pop(screen)){if(count<(int)input.size())input[(size_t)count++]={0,screen.status,screen.data1,screen.data2,screen.status==0xf4?(screen.data1==1?drummer::Midi::ribbonTop:drummer::Midi::ribbonStop):drummer::Midi::message};else full=true;}
 for(auto m:midi){if(m.numBytes<1)continue;auto size=ssb::midiMessageSize(m.data[0]);if(!size||m.numBytes<size)continue;
  ssb::MidiMessage msg{m.data[0],size>1?m.data[1]:(uint8_t)0,size>2?m.data[2]:(uint8_t)0,size};midiToPage.push(msg);
  if(count<(int)input.size())input[(size_t)count++]={m.samplePosition,msg.status,msg.data1,msg.data2,drummer::Midi::message,true};else full=true;
 }midi.clear();
 for(int i=0;i<count;++i){const auto& m=input[(size_t)i];if((m.status&240)==0xc0&&(m.status&15)==active->config.masterChannel)requestProgram(m.a);}
 for(int i=0;i<count;++i)if((input[(size_t)i].status&240)==0xb0&&input[(size_t)i].a==74){lastSlideChannel=input[(size_t)i].status&15;lastSlideValue=input[(size_t)i].b;}
 for(int i=0;i<5;++i)engine.drumGlobal(i,macroParameters[(size_t)i]->getValue());
 uint32_t evaluations=0;bool scheduleOverflow=false;
 auto schedule=[&](int frames,const drummer::Midi* messages,int size){
  scheduler.process(active->config,clock,frames,messages,size,emit,this);
  monitorSegment+=frames*clock.bpm/(60*rate);evaluations+=scheduler.evaluations;scheduleOverflow=scheduleOverflow||scheduler.overflow;
 };
 if(full){engine.drumPanic();scheduler.reset();activationScheduled=false;
  drummer::PlayedEvent end;end.type=drummer::PlayedEvent::allOff;end.epoch=monitorEpoch;end.beat=monitorBeat;performanceMonitor.push(end);monitorSegment+=n*clock.bpm/(60*rate);
 }
 else if(switchAt<n){
  int before=0;while(before<count&&input[(size_t)before].offset<switchAt)++before;
  schedule(switchAt,input.data(),before);renderTo(switchAt);
  const double beat=clock.ppq+switchAt*clock.bpm/(60*rate);
  // Event offsets from the second scheduler segment remain relative to it.
  eventBase=switchAt;clock.ppq=beat;clock.discontinuity=false;
  if(!activate(beat))activationScheduled=false;
  if(wrapperType==wrapperType_Standalone)clock.bpm=active->performance->bpm;
  for(int i=before;i<count;++i)input[(size_t)i].offset-=switchAt;
  schedule(n-switchAt,input.data()+before,count-before);
 }else schedule(n,input.data(),count);
 renderTo(n);
 monitorBeat=monitorSegment;standalonePpq=clock.ppq+(n-eventBase)*clock.bpm/(60*rate);activationExpected=standalonePpq;activationClockKnown=true;hostBpm=clock.bpm;
 unsigned pw=previewWrite.load(std::memory_order_relaxed);
 if(pw-previewRead.load(std::memory_order_acquire)+2<=previewRetired.size()){
  auto retirePreview=[&](drummer::Preview* old){if(old){previewRetired[pw%previewRetired.size()]=old;previewWrite.store(++pw,std::memory_order_release);}};
  auto*next=previewIncoming.exchange(nullptr);const bool stop=previewStop.exchange(false);
  if(next&&(stop||next->sampleRate()!=rate)){retirePreview(next);next=nullptr;}
  if(next||stop||(previewActive&&previewActive->sampleRate()!=rate)){retirePreview(previewActive);previewActive=next;}
 }
 if(previewActive){const bool playing=previewActive->add(outputL,outputR,n,clock.bpm);previewPlaying=playing;if(!playing)previewStop=true;}else previewPlaying=false;
 freePlaying=!transportPlaying&&scheduler.hasHeldNotes();clipEvaluations=(int)evaluations;overflow=full||scheduleOverflow;
 if(buffer.getNumChannels()==1)for(int i=0;i<n;++i)outputL[i]=(outputL[i]+outputR[i])*.5f;
 buffer.applyGain(outputVolume->load());
 for(int ch=2;ch<buffer.getNumChannels();++ch)buffer.clear(ch,0,n);
 for(int i=0;i<16;++i){auto& f=scheduler.contacts()[(size_t)i];contactSlide[(size_t)i]=f.values.slide;contactGlide[(size_t)i]=f.values.glide;contactPressure[(size_t)i]=f.values.press;contacts[(size_t)i].store(f.down?1+f.key+(f.zone<<8):0,std::memory_order_relaxed);}
 publishFingers();
}
bool DrummerProcessor::readFingers(FingerSnapshot& out) noexcept {
 if(fingerBusy.test_and_set(std::memory_order_acquire))return false;
 out=fingerSnapshot;
 fingerBusy.clear(std::memory_order_release);return true;
}
void DrummerProcessor::rememberPattern(const drummer::Contact& f,double beat,bool playing) noexcept {
 if(!active||f.key<0||f.key>=128)return;
 auto& pattern=lastPatterns[(size_t)f.key];pattern={f.clip,f.zone,-1,0,true,playing,beat};
 const auto& key=active->config.keys[(size_t)f.key];const int group=key.zones[(size_t)std::clamp(f.zone,0,key.zoneCount-1)].group;
 if(key.intentEnabled&&group>=0&&group<(int)active->config.groups.size()){const auto& choices=active->config.groups[(size_t)group];for(int i=0;i<choices.count;++i)if(choices.candidates[(size_t)i].clip==f.clip){pattern.candidate=i;break;}}
 if(f.clip>=0&&f.clip<(int)active->config.clips.size()){const auto& clip=active->config.clips[(size_t)f.clip];pattern.phase=std::fmod(beat-clip.origin,clip.length);if(pattern.phase<0)pattern.phase+=clip.length;}
}
void DrummerProcessor::publishFingers() noexcept {
 for(auto& pattern:lastPatterns)pattern.playing=false;
 for(const auto& f:scheduler.contacts())if(f.held&&!f.suppressed)rememberPattern(f,standalonePpq,true);
 for(const auto& f:scheduler.sustainedContacts())if(f.held)rememberPattern(f,standalonePpq,true);
 if(fingerBusy.test_and_set(std::memory_order_acquire))return;
 fingerSnapshot.count=0;
 for(int key=0;key<128;++key)fingerSnapshot.latches[(size_t)key]={scheduler.pendingLatchAction(key),scheduler.pendingLatchNote(key)};
 // Ordinary launch quantization also schedules a future start, independently
 // of Hold/Toggle's latch grid. Give it the same immediate visual feedback.
 auto pendingStart=[&](const drummer::Contact& f){if(f.held&&f.opens>standalonePpq+1e-9&&f.key>=0&&!fingerSnapshot.latches[(size_t)f.key].action)fingerSnapshot.latches[(size_t)f.key]={1,f.inputNote>=0?f.inputNote:f.key};};
 for(const auto& f:scheduler.contacts())pendingStart(f);
 for(const auto& f:scheduler.sustainedContacts())pendingStart(f);
 fingerSnapshot.patterns=lastPatterns;
 fingerSnapshot.runtime=active;if(active)fingerSnapshot.patternHash=active->patternHash;
 fingerSnapshot.beat=monitorBeat;fingerSnapshot.bpm=hostBpm.load();fingerSnapshot.epoch=monitorEpoch;
 std::array<int,128> shown;shown.fill(-1);
 auto append=[&](const drummer::Contact& f,bool sustained){
  const bool playing=f.held&&standalonePpq+1e-9>=f.opens;
  int index=shown[(size_t)f.key];
  if(index>=0&&(fingerSnapshot.fingers[(size_t)index].playing||!playing))return;
  if(index<0){index=fingerSnapshot.count++;shown[(size_t)f.key]=index;}
  auto& view=fingerSnapshot.fingers[(size_t)index];view={f.owner,f.channel,f.inputNote>=0?f.inputNote:f.key,f.zone,f.values.slide,f.values.glide,f.values.press,sustained,f.toggled,playing};view.queued=scheduler.hasPendingLatch(f.key);view.origin=f.key;
  if(f.key>=0&&f.key<128)view.candidate=lastPatterns[(size_t)f.key].candidate;
 };
 for(const auto& f:scheduler.contacts())if(f.down&&!f.suppressed)append(f,false);
 for(const auto& f:scheduler.sustainedContacts())if(f.held)append(f,true);
 fingerBusy.clear(std::memory_order_release);
}
void DrummerProcessor::requestProgram(int program) noexcept {
 if(program<0||program>127)return;
 auto previous=programRequest.load(std::memory_order_relaxed);
 while(!programRequest.compare_exchange_weak(previous,((previous>>8)+1)*256+(uint64_t)program,std::memory_order_release,std::memory_order_relaxed)){}
 lastProgram=program;
}
void DrummerProcessor::timerCallback(){
 const auto ticket=programRequest.load(std::memory_order_acquire);
 if(ticket!=servicedProgram){
  servicedProgram=ticket;const auto expected=stateRevision.load();
  jobs.addJob([this,ticket,expected]{
   try{
    const auto current=stateRevision.load();
    // A preceding program job can finish after this request was queued. Follow
    // that job, but never overwrite a newer editor edit or DAW state recall.
    if(programRequest.load()!=ticket||(current!=expected&&current!=lastProgramRevision.load()))return;
    const auto bank=drummer::patchBank(shared?shared->snapshot():juce::var{});
    const auto& document=bank[(size_t)(ticket&127)];
    if(!document.isObject()){if(programRequest.load()==ticket){programUnassigned=true;programFailed=false;}return;}
    auto next=drummer::compileDocument(document);
    if(programRequest.load()==ticket){const bool ok=apply(std::move(next),current);if(ok)lastProgramRevision=current+1;programFailed=!ok;programUnassigned=false;}
   }catch(const std::exception& e){programFailed=true;juce::Logger::writeToLog("MIDI patch: "+juce::String(e.what()));}
  });
 }
 engine.collectGarbage();unsigned pr=previewRead.load();while(pr!=previewWrite.load(std::memory_order_acquire)){delete previewRetired[pr%previewRetired.size()];++pr;}previewRead.store(pr,std::memory_order_release);
 // Capture the retired cohort before reading its protected runtime. Audio can
 // publish a newer snapshot, but cannot publish a runtime already retired here.
 const auto end=retiredWrite.load(std::memory_order_acquire);FingerSnapshot protectedView;
 if(readFingers(protectedView)){unsigned r=retiredRead.load();while(r!=end){auto* old=retired[r%retired.size()];if(old==protectedView.runtime)break;delete old;++r;}retiredRead.store(r,std::memory_order_release);}
}
void DrummerProcessor::getStateInformation(juce::MemoryBlock&out){juce::ValueTree tree("RoliDrummer");const auto doc=document();tree.setProperty("document",doc,nullptr);drummer::appendSessionAssets(tree,juce::JSON::parse(doc));tree.setProperty("width",editorWidth,nullptr);tree.setProperty("height",editorHeight,nullptr);// MIDI CC writes the parameter's lock-free value directly. The APVTS listener
 // cache is only notified by host/editor automation, so serialize the actual
 // controller values explicitly without calling host listeners on audio.
 auto automation=parameters.copyState();
 for(auto child:automation){const auto id=child.getProperty("id").toString();for(int i=0;i<5;++i)if(id=="macro"+juce::String(i))child.setProperty("value",macroParameters[(size_t)i]->getValue(),nullptr);}
 tree.addChild(automation,-1,nullptr);juce::MemoryOutputStream stream(out,false);tree.writeToStream(stream);}
void DrummerProcessor::setStateInformation(const void*data,int size){auto state=juce::ValueTree::readFromData(data,(size_t)size);if(!state.hasType("RoliDrummer"))return;programRequest.fetch_add(256);servicedProgram=programRequest.load();try{drummer::restoreSessionAssets(state);apply(drummer::compileDocument(juce::JSON::parse(state.getProperty("document").toString())));editorWidth=(int)state.getProperty("width",1500);editorHeight=(int)state.getProperty("height",880);auto params=state.getChildWithName("Automation");if(params.isValid()){
 parameters.replaceState(params);
 // A recalled value may equal the APVTS cache while differing from the latest
 // MIDI CC. Always restore the controller's authoritative value as well.
 for(auto child:params){auto id=child.getProperty("id").toString();for(int i=0;i<5;++i)if(id=="macro"+juce::String(i))macroParameters[(size_t)i]->setValue(macroParameters[(size_t)i]->convertTo0to1((float)child.getProperty("value")));}
 }}catch(const std::exception&e){juce::Logger::writeToLog("Patch restore: "+juce::String(e.what()));}}
juce::AudioProcessorEditor* DrummerProcessor::createEditor(){return new DrummerEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new DrummerProcessor;}
