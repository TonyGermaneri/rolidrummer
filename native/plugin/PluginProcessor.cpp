#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <drummer/Assets.h>
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
 startTimerHz(10);
}
DrummerProcessor::~DrummerProcessor(){stopTimer();life.reset();jobs.removeAllJobs(true,-1);delete incoming.exchange(nullptr);delete active;delete previewIncoming.exchange(nullptr);delete previewActive;for(auto r=previewRead.load();r!=previewWrite.load();++r)delete previewRetired[r%previewRetired.size()];for(auto r=retiredRead.load();r!=retiredWrite.load();++r)delete retired[r%retired.size()];}
bool DrummerProcessor::apply(std::unique_ptr<drummer::Runtime> r, std::optional<uint64_t> expected){
 std::lock_guard guard(stateMutex);
 if(expected && *expected!=stateRevision.load())return false;
 engine.preparePerformance(r->performance);
 r->revision=stateRevision.load()+1;
 saved=r->json;
 const auto revision=r->revision;
 if(auto*old=incoming.exchange(r.release()))delete old;
 stateRevision.store(revision);
 return true;
}
juce::String DrummerProcessor::document(){std::lock_guard guard(stateMutex);return saved;}
void DrummerProcessor::prepareToPlay(double sr,int max){rate=sr;previewRate=sr;previewBlock=max;stopPreview();engine.prepare(sr,max);rightScratch.resize((size_t)std::max(max,1));scheduler.reset();standalonePpq=0;wasHostPlaying=false;freePlaying=false;}
bool DrummerProcessor::isBusesLayoutSupported(const BusesLayout&l)const{return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()||l.getMainOutputChannelSet()==juce::AudioChannelSet::mono();}
void DrummerProcessor::renderTo(int end){if(end<=rendered)return;engine.process(outputL+rendered,outputR+rendered,end-rendered,nullptr,0,{clock.bpm,clock.ppq+rendered*clock.bpm/(60*rate),clock.playing});rendered=end;}
void DrummerProcessor::emit(void*ctx,const drummer::Event&e){auto&p=*static_cast<DrummerProcessor*>(ctx);p.renderTo(e.offset);switch(e.type){
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
 outputL=buffer.getWritePointer(0);outputR=buffer.getNumChannels()>1?buffer.getWritePointer(1):rightScratch.data();rendered=0;
 engine.process(outputL,outputR,0,nullptr,0,{});
 const unsigned w=retiredWrite.load(std::memory_order_relaxed);
 if(engine.canActivatePerformance() && w-retiredRead.load(std::memory_order_acquire)<retired.size())if(auto*next=incoming.exchange(nullptr)){if(active){retired[w%retired.size()]=active;retiredWrite.store(w+1,std::memory_order_release);}active=next;engine.activatePerformance(active->performance);}
 if(!active){buffer.clear();midi.clear();return;}
 // The clip bus always advances. Contacts gate audible parts, including when
 // the host is stopped. Keeping one clock also lets later fingers join in phase.
 clock={standalonePpq,hostBpm.load(),rate,true};
 bool transportPlaying=false;
 if(wrapperType==wrapperType_Standalone){clock.bpm=active->performance->bpm;transportPlaying=standalonePlaying.load();}
 else if(auto*head=getPlayHead())if(auto pos=head->getPosition()){
  if(auto bpm=pos->getBpm();bpm && std::isfinite(*bpm) && *bpm>0)clock.bpm=*bpm;
  transportPlaying=pos->getIsPlaying();
  if(transportPlaying)if(auto ppq=pos->getPpqPosition();ppq && std::isfinite(*ppq))clock.ppq=*ppq;
 }
 // Re-enter host time even if its PPQ happens to equal the free-running phase.
 clock.discontinuity=transportPlaying&&!wasHostPlaying;wasHostPlaying=transportPlaying;
 standalonePpq=clock.ppq+n*clock.bpm/(60*rate);
 hostBpm=clock.bpm;hostPpq=clock.ppq;hostPlaying=transportPlaying;
 int count=0;bool full=false;
 ssb::MidiMessage screen;while(screenMidi.pop(screen)){if(count<(int)input.size())input[(size_t)count++]={0,screen.status,screen.data1,screen.data2};else full=true;}
 for(auto m:midi){if(m.numBytes<1)continue;auto size=ssb::midiMessageSize(m.data[0]);if(!size||m.numBytes<size)continue;
 ssb::MidiMessage msg{m.data[0],size>1?m.data[1]:(uint8_t)0,size>2?m.data[2]:(uint8_t)0,size};midiToPage.push(msg);
 if(count<(int)input.size())input[(size_t)count++]={m.samplePosition,msg.status,msg.data1,msg.data2};else full=true;
 }midi.clear();
 for(int i=0;i<count;++i)if((input[(size_t)i].status&240)==0xb0&&input[(size_t)i].a==74){lastSlideChannel=input[(size_t)i].status&15;lastSlideValue=input[(size_t)i].b;}
 outputL=buffer.getWritePointer(0);outputR=buffer.getNumChannels()>1?buffer.getWritePointer(1):rightScratch.data();rendered=0;
 engine.process(outputL,outputR,0,nullptr,0,{clock.bpm,clock.ppq,clock.playing});
 for(int i=0;i<5;++i)engine.drumGlobal(i,macroParameters[(size_t)i]->getValue());
 if(full){engine.drumPanic();scheduler.reset();}else scheduler.process(active->config,clock,n,input.data(),count,emit,this);
 renderTo(n);
 unsigned pw=previewWrite.load(std::memory_order_relaxed);
 if(pw-previewRead.load(std::memory_order_acquire)+2<=previewRetired.size()){
  auto retire=[&](drummer::Preview* old){if(old){previewRetired[pw%previewRetired.size()]=old;previewWrite.store(++pw,std::memory_order_release);}};
  auto*next=previewIncoming.exchange(nullptr);
  const bool stop=previewStop.exchange(false);
  if(next&&(stop||next->sampleRate()!=rate)){retire(next);next=nullptr;}
  if(next||stop||(previewActive&&previewActive->sampleRate()!=rate)){retire(previewActive);previewActive=next;}
 }
 if(previewActive){const bool playing=previewActive->add(outputL,outputR,n,clock.bpm);previewPlaying=playing;if(!playing)previewStop=true;}else previewPlaying=false;
 freePlaying=!transportPlaying&&std::any_of(scheduler.contacts().begin(),scheduler.contacts().end(),[](const auto& f){return f.held;});
 clipEvaluations=scheduler.evaluations;overflow=full||scheduler.overflow;
 if(buffer.getNumChannels()==1)for(int i=0;i<n;++i)outputL[i]=(outputL[i]+outputR[i])*.5f;
 // Sound performance supplies patch volume; automation is a separate output trim.
 buffer.applyGain(outputVolume->load());
 for(int ch=2;ch<buffer.getNumChannels();++ch)buffer.clear(ch,0,n);
 for(int i=0;i<16;++i){auto& f=scheduler.contacts()[(size_t)i];contactSlide[(size_t)i]=f.values.slide;contactGlide[(size_t)i]=f.values.glide;contactPressure[(size_t)i]=f.values.press;contacts[(size_t)i].store(f.down?1+f.key+(f.zone<<8):0,std::memory_order_relaxed);}
}
void DrummerProcessor::timerCallback(){engine.collectGarbage();unsigned pr=previewRead.load();while(pr!=previewWrite.load(std::memory_order_acquire)){delete previewRetired[pr%previewRetired.size()];++pr;}previewRead.store(pr,std::memory_order_release);unsigned r=retiredRead.load();while(r!=retiredWrite.load(std::memory_order_acquire)){delete retired[r%retired.size()];++r;}retiredRead.store(r,std::memory_order_release);}
void DrummerProcessor::getStateInformation(juce::MemoryBlock&out){juce::ValueTree tree("RoliDrummer");const auto doc=document();tree.setProperty("document",doc,nullptr);drummer::appendSessionAssets(tree,juce::JSON::parse(doc));tree.setProperty("width",editorWidth,nullptr);tree.setProperty("height",editorHeight,nullptr);// MIDI CC writes the parameter's lock-free value directly. The APVTS listener
 // cache is only notified by host/editor automation, so serialize the actual
 // controller values explicitly without calling host listeners on audio.
 auto automation=parameters.copyState();
 for(auto child:automation){const auto id=child.getProperty("id").toString();for(int i=0;i<5;++i)if(id=="macro"+juce::String(i))child.setProperty("value",macroParameters[(size_t)i]->getValue(),nullptr);}
 tree.addChild(automation,-1,nullptr);juce::MemoryOutputStream stream(out,false);tree.writeToStream(stream);}
void DrummerProcessor::setStateInformation(const void*data,int size){auto state=juce::ValueTree::readFromData(data,(size_t)size);if(!state.hasType("RoliDrummer"))return;try{drummer::restoreSessionAssets(state);apply(drummer::compileDocument(juce::JSON::parse(state.getProperty("document").toString())));editorWidth=(int)state.getProperty("width",1500);editorHeight=(int)state.getProperty("height",880);auto params=state.getChildWithName("Automation");if(params.isValid()){
 parameters.replaceState(params);
 // A recalled value may equal the APVTS cache while differing from the latest
 // MIDI CC. Always restore the controller's authoritative value as well.
 for(auto child:params){auto id=child.getProperty("id").toString();for(int i=0;i<5;++i)if(id=="macro"+juce::String(i))macroParameters[(size_t)i]->setValue(macroParameters[(size_t)i]->convertTo0to1((float)child.getProperty("value")));}
 }}catch(const std::exception&e){juce::Logger::writeToLog("Patch restore: "+juce::String(e.what()));}}
juce::AudioProcessorEditor* DrummerProcessor::createEditor(){return new DrummerEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new DrummerProcessor;}
