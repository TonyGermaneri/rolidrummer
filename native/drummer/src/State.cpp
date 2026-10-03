#include <drummer/State.h>
#include <FactoryData.h>
#include <drummer/Assets.h>
#include <mutex>
#include <ssb/ModelJson.h>
#include <ssb/Parameters.h>
#include <cmath>
#include <map>
#include <set>
#include <limits>
#include <stdexcept>
namespace drummer {
namespace {
juce::var object(std::initializer_list<std::pair<const char*,juce::var>> items) {auto* o=new juce::DynamicObject;for(auto&[k,v]:items)o->setProperty(k,v);return o;}
juce::var get(const juce::var&o,const char*k,juce::var fallback={}){return o.getProperty(k,fallback);}
double number(const juce::var&o,const char*k,double fallback,double lo,double hi){double v=(double)get(o,k,fallback);return std::isfinite(v)?std::clamp(v,lo,hi):fallback;}
ssb::SamplePtr sample(const juce::String& path) {
 static std::mutex cacheMutex;
 static std::map<juce::String,std::weak_ptr<const ssb::Sample>> cache;
 std::lock_guard guard(cacheMutex);
 if(auto cached=cache[path].lock())return cached;
 if(cache.size()>1024){for(auto it=cache.begin();it!=cache.end();){if(it->second.expired())it=cache.erase(it);else ++it;}}
 auto s=std::make_shared<ssb::Sample>();
 if(path.startsWith("factory:")) {
  s->rate=48000;int frames=24000;s->channels.resize(1);s->channels[0].resize((size_t)frames);
  uint32_t rng=234972;double phase=0;
  for(int i=0;i<frames;++i){double t=i/s->rate;rng=rng*1664525+1013904223;double noise=double(rng)/4294967295.*2-1;double v=0;
   if(path=="factory:kick") {phase+=2*3.141592653589793*(48+150*std::exp(-t*45))/s->rate;v=std::sin(phase)*std::exp(-t*14);}
   else if(path=="factory:hat")v=noise*std::exp(-t*55)*.5;
   else if(path=="factory:snare")v=(noise*.65+std::sin(t*2*3.141592653589793*180)*.35)*std::exp(-t*19);
   else v=std::sin(t*2*3.141592653589793*110)*std::exp(-t*12);
   s->channels[0][(size_t)i]=(float)(v*std::min(1.,t*1000));
  }
  cache[path]=s;return s;
 }
 juce::AudioFormatManager formats;formats.registerBasicFormats();
 std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(path)));
 if(!reader)throw std::runtime_error("Cannot read sound: "+path.toStdString());
 if(reader->lengthInSamples<2 || reader->lengthInSamples>48000LL*600)throw std::runtime_error("Sound must be between two samples and ten minutes: "+path.toStdString());
 juce::AudioBuffer<float> buffer(std::min(2,(int)reader->numChannels),(int)reader->lengthInSamples);
 if(!reader->read(&buffer,0,buffer.getNumSamples(),0,true,true))throw std::runtime_error("Sound decode failed: "+path.toStdString());
 s->rate=reader->sampleRate;
 for(int ch=0;ch<buffer.getNumChannels();++ch)s->channels.emplace_back(buffer.getReadPointer(ch),buffer.getReadPointer(ch)+buffer.getNumSamples());
 cache[path]=s;return s;
}
juce::var mpeRoutes(){juce::Array<juce::var> routes;
 const char* target[]={"volume","pitch","cutoff","reverbMix","release"};
 const double lo[]={.45,-2,900,.02,.06},hi[]={1,2,18000,.3,1.2};
 for(int i=0;i<5;++i)routes.add(object({{"source",i},{"target",target[i]},{"min",lo[i]},{"max",hi[i]},{"curve",i==2?.55:1.},{"smoothing",8.}}));return routes;
}
juce::var legacyGrooveNotes(int p,bool legacy=false){juce::Array<juce::var> notes;
 auto add=[&](int i,int note,double velocity){notes.add(object({{"beat",i*.25},{"duration",note==42?.07:.1},{"note",note},{"velocity",velocity}}));};
 for(int i=0;i<16;++i){if(legacy){if(i%4==0)add(i,36,.9);if(i%4==2)add(i,38,.8);if(i%(p==0?4:p==1?2:1)==0)add(i,42,i%2==0?.7:.4);}
 else if(i%(p==0?4:p==1?2:1)==0)for(int n:{36,42,38,50,47,43})add(i,n,i%4==0?.85:i%2==0?.65:.45);}
 return notes;
}
bool sameNotes(const juce::var&a,const juce::var&b){auto*x=a.getArray();auto*y=b.getArray();if(!x||!y||x->size()!=y->size())return false;for(int i=0;i<x->size();++i)for(const char*key:{"beat","duration","note","velocity"})if(std::abs((double)get((*x)[i],key)-(double)get((*y)[i],key))>1e-7)return false;return true;}
}
juce::File dataDirectory(){auto override=juce::SystemStats::getEnvironmentVariable("RD_DATA_DIR",{});return override.isNotEmpty()?juce::File(override):juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("WaveContour/RoliDrummer");}
juce::var defaultDocument(){
 auto data=juce::JSON::parse(juce::String::fromUTF8(drummerfactory::factoryKit_json,drummerfactory::factoryKit_jsonSize));
 juce::Array<juce::var> zones;zones.add(object({{"upper",.22},{"clipId","factory-0"}}));zones.add(object({{"upper",.76},{"clipId","factory-1"}}));zones.add(object({{"upper",1.},{"clipId","factory-2"}}));
 juce::Array<juce::var> keys;auto voices=get(data,"voices");
 for(const auto&v:*voices.getArray()){
  auto settings=get(v,"settings").clone();settings.getDynamicObject()->setProperty("name",get(v,"name"));
  keys.add(object({{"note",get(v,"note")},{"name",get(v,"name")},{"parts",get(v,"parts")},{"zoneSource",2},{"zones",zones},{"routes",get(v,"routes")},{"mpeDefaultsVersion",1},{"sound",object({{"path",get(v,"source")},{"s",settings}})}}));
 }
 juce::Array<juce::var> cc;for(int n:{107,109,111,113,114})cc.add(n);
 return object({{"version",1},{"name",get(data,"name")},{"keys",keys},{"clips",get(data,"clips")},{"templateZones",zones},{"launchGrid",.25},{"switchGrid",.25},{"hardRelease",false},{"volume",.8},{"bpm",120.},{"globalCC",cc},{"globalRoutes",juce::Array<juce::var>()}});
}
std::unique_ptr<Runtime> compileDocument(const juce::var& input){
 auto doc=input.clone();
 if((int)get(doc,"version")!=1)throw std::runtime_error("Unsupported patch version");
 if(auto*clips=get(doc,"clips").getArray())for(auto&clip:*clips)for(int p=0;p<3;++p)if(get(clip,"id").toString()=="factory-"+juce::String(p)&&(double)get(clip,"length")==4.&&(sameNotes(get(clip,"notes"),legacyGrooveNotes(p,true))||sameNotes(get(clip,"notes"),legacyGrooveNotes(p,false)))){
  auto factoryClips=get(defaultDocument(),"clips");const auto&replacement=(*factoryClips.getArray())[p];for(const char*field:{"notes","length","name"})clip.getDynamicObject()->setProperty(field,get(replacement,field));
 }
 if(auto*keys=get(doc,"keys").getArray())for(auto&key:*keys)if((int)get(key,"mpeDefaultsVersion")!=1){auto routes=get(key,"routes");if(!routes.isArray()||routes.getArray()->isEmpty())key.getDynamicObject()->setProperty("routes",mpeRoutes());key.getDynamicObject()->setProperty("mpeDefaultsVersion",1);}
 auto r=std::make_unique<Runtime>();auto& c=r->config;
 c.launchGrid=number(doc,"launchGrid",.25,0,16);c.switchGrid=number(doc,"switchGrid",.25,0,16);c.hardRelease=(bool)get(doc,"hardRelease",false);
 c.hysteresis=(float)number(doc,"hysteresis",.015,0,.1);c.masterChannel=(int)number(doc,"masterChannel",0,0,15);
 std::map<juce::String,int> clipIndex;std::map<juce::String,int> contentIndex;
 std::set<juce::String> boundClips,allClipIds;
 if(auto keys=get(doc,"keys");auto* list=keys.getArray())for(const auto& key:*list)
  if(auto zones=get(key,"zones");auto* items=zones.getArray())for(const auto& zone:*items)boundClips.insert(get(zone,"clipId").toString());
 if(auto value=get(doc,"clips");auto* list=value.getArray())for(const auto& item:*list){
  auto id=get(item,"id").toString();if(id.isEmpty() || !allClipIds.insert(id).second)throw std::runtime_error("Pattern IDs must be unique");
  if(!boundClips.count(id))continue; // Library choices are retained in the patch, but only bound patterns need a bus.
  auto identity=juce::JSON::toString(get(item,"notes"))+":"+get(item,"length").toString()+":"+get(item,"origin",0).toString();
  if(contentIndex.count(identity)){clipIndex[id]=contentIndex[identity];continue;}
  Clip clip;clip.id=(uint32_t)c.clips.size()+1;clip.length=number(item,"length",4,.001,100000);clip.origin=number(item,"origin",0,-100000,100000);
  if(auto value=get(item,"notes");auto* notes=value.getArray())for(const auto& n:*notes){
   if(clip.notes.size()>=100000)throw std::runtime_error("Pattern exceeds 100000 notes");
   clip.notes.push_back({number(n,"beat",0,0,clip.length),number(n,"duration",.1,.00001,100000),(int)number(n,"note",36,0,127),(float)number(n,"velocity",1,0,1)});
  }
  clipIndex[id]=(int)c.clips.size();contentIndex[identity]=(int)c.clips.size();prepareClip(clip);c.clips.push_back(std::move(clip));
 }
 std::map<std::string,std::shared_ptr<ssb::Sound>> sounds;std::map<juce::String,ssb::SamplePtr> samples;
 if(auto value=get(doc,"keys");auto* list=value.getArray())for(const auto& item:*list){
  int note=(int)number(item,"note",48,0,127);auto& key=c.keys[(size_t)note];if(key.enabled)throw std::runtime_error("Duplicate keyboard key");key.enabled=true;
  key.zoneSource=(int)number(item,"zoneSource",2,0,4);
  // Lift arrives only after the gate closes; it can shape sound tails but
  // cannot choose a running zone. Migrate an old Lift zone to Slide.
  if(key.zoneSource==4){key.zoneSource=2;if(auto* o=item.getDynamicObject())o->setProperty("zoneSource",2);}
  if(auto value=get(item,"parts");auto* parts=value.getArray())for(auto& p:*parts)key.parts[(size_t)std::clamp((int)p,0,127)]=true;
  auto zones=get(item,"zones");auto* zs=zones.getArray();if(!zs || zs->isEmpty() || zs->size()>maxZones)throw std::runtime_error("A key needs 1–8 zones");
  key.zoneCount=zs->size();float previous=0;
  for(int i=0;i<zs->size();++i){const auto& z=(*zs)[i];float upper=(float)number(z,"upper",1,0,1);if(upper<=previous || (i==zs->size()-1 && upper!=1))throw std::runtime_error("Zone boundaries must increase and finish at 1");previous=upper;
   auto id=get(z,"clipId").toString();key.zones[(size_t)i]={upper,clipIndex.count(id)?clipIndex[id]:-1};
  }
  auto snd=get(item,"sound");auto path=get(snd,"path").toString();if(path.isEmpty())continue;
  ssb::SamplePtr loaded;
  try {
   const auto resolved=path.startsWith("factory:")?path:cacheSound(snd).getFullPathName();
   if(!samples.count(resolved))samples[resolved]=sample(resolved);
   loaded=samples[resolved];
   if(auto* obj=snd.getDynamicObject())obj->removeProperty("missing");
  } catch(const std::exception& e) {
   if(auto* obj=snd.getDynamicObject())obj->setProperty("missing",juce::String(e.what()));
   continue;
  }
  auto sound=std::make_shared<ssb::Sound>();sound->id="key-"+std::to_string(note);sound->sample=loaded;sound->s=ssb::settingsFromJson(get(snd,"s"));sound->s.reverse=(bool)get(get(snd,"s"),"reverse",false);
  auto addRoutes=[&](juce::var routes,bool global){if(auto* rs=routes.getArray())for(auto& route:*rs){if(sound->expressionRouteCount>=16)throw std::runtime_error("At most 16 expression routes per key");
   int target=ssb::parameterIndex(get(route,"target").toString().toStdString());if(target==-1)continue;
   int source=(int)number(route,"source",0,0,global?4:9)+(global?5:0);
   sound->expressionRoutes[(size_t)sound->expressionRouteCount++]={source,target,(float)number(route,"min",0,-20000,20000),(float)number(route,"max",1,-20000,20000),(float)number(route,"curve",1,.1,10),(float)number(route,"smoothing",8,0,1000)};
  }};
  addRoutes(get(item,"routes"),false);addRoutes(get(doc,"globalRoutes"),true);
  r->sounds[(size_t)note]=sound;sounds[sound->id]=sound;
 }
 juce::Array<juce::var> pads;for(auto&[id,sound]:sounds)pads.add(juce::String(id));
 auto meta=object({{"volume",number(doc,"volume",.8,0,1.5)},{"play",false},{"pads",pads},{"bpm",number(doc,"bpm",120,20,400)},{"fx",get(doc,"fx")}});
 r->performance=ssb::buildPerformance(meta,sounds);
 if(auto value=get(doc,"globalCC");auto* cc=value.getArray())for(int i=0;i<std::min(5,cc->size());++i)r->cc[(size_t)i]=std::clamp((int)(*cc)[i],0,127);
 r->json=juce::JSON::toString(doc,true);return r;
}
juce::var waveform(const juce::var& input){
 auto sound=input.isObject()?input.clone():object({{"path",input.toString()}});
 auto path=get(sound,"path").toString();
 auto s=sample(path.startsWith("factory:")?path:cacheSound(sound).getFullPathName());
 juce::Array<juce::var> peaks;for(size_t i=0;i<256;++i){float peak=0;for(size_t j=i*s->frames()/256;j<(i+1)*s->frames()/256;++j)peak=std::max(peak,std::abs(s->channels[0][j]));peaks.add(peak);}return object({{"peaks",peaks},{"duration",s->duration()}});
}
namespace {
juce::MidiFile readMidi(const juce::File& file) {
 if(file.getSize()>32*1024*1024)throw std::runtime_error("MIDI file exceeds 32 MB");
 juce::FileInputStream in(file);juce::MidiFile midi;
 if(!in.openedOk() || !midi.readFrom(in))throw std::runtime_error("Invalid MIDI file");
 if(midi.getTimeFormat()<=0)throw std::runtime_error("SMPTE MIDI timing is not supported");
 return midi;
}
juce::var midiInfo(const juce::MidiFile& midi) {
 std::array<int,17> channels{};juce::Array<juce::var> tracks;
 for(int t=0;t<midi.getNumTracks();++t) {
  std::array<int,17> counts{};int total=0;juce::String name;
  for(int i=0;i<midi.getTrack(t)->getNumEvents();++i) {
   const auto& m=midi.getTrack(t)->getEventPointer(i)->message;
   if(m.isTrackNameEvent())name=m.getTextFromTextMetaEvent();
   if(m.isNoteOn()){++counts[(size_t)m.getChannel()];++channels[(size_t)m.getChannel()];++total;}
  }
  juce::Array<juce::var> active;for(int ch=1;ch<=16;++ch)if(counts[(size_t)ch])active.add(ch);
  tracks.add(object({{"index",t},{"name",name.isEmpty()?"Track "+juce::String(t+1):name},{"notes",total},{"channels",active}}));
 }
 juce::Array<juce::var> active;for(int ch=1;ch<=16;++ch)if(channels[(size_t)ch])active.add(ch);
 int automatic=channels[10]?10:active.size()==1?(int)active[0]:-1;
 return object({{"tracks",tracks},{"channels",active},{"autoChannel",automatic},{"ppq",midi.getTimeFormat()}});
}
}
juce::var inspectMidi(const juce::File& file){return midiInfo(readMidi(file));}
juce::var importMidi(const juce::File& file,int channel,int track){
 const auto midi=readMidi(file);const int ppq=midi.getTimeFormat();
 if(channel< -1||channel>16||track< -1||track>=midi.getNumTracks())throw std::runtime_error("Invalid MIDI track or channel selection");
 if(channel==-1) {
  std::array<bool,17> active{};
  for(int t=0;t<midi.getNumTracks();++t)if(track==-1||track==t)for(int i=0;i<midi.getTrack(t)->getNumEvents();++i){const auto& m=midi.getTrack(t)->getEventPointer(i)->message;if(m.isNoteOn())active[(size_t)m.getChannel()]=true;}
  if(active[10])channel=10;
  else if(std::count(active.begin(),active.end(),true)==1)channel=(int)std::distance(active.begin(),std::find(active.begin(),active.end(),true));
  else throw std::runtime_error("Choose a MIDI channel to import");
 }
 juce::MidiMessageSequence selected;double end=0,signatureAt=std::numeric_limits<double>::infinity();int numerator=4,denominator=4;
 for(int t=0;t<midi.getNumTracks();++t)for(int i=0;i<midi.getTrack(t)->getNumEvents();++i){const auto& m=midi.getTrack(t)->getEventPointer(i)->message;
  end=std::max(end,m.getTimeStamp()/ppq);
  if(m.isTimeSignatureMetaEvent()&&m.getTimeStamp()<signatureAt){m.getTimeSignatureInfo(numerator,denominator);signatureAt=m.getTimeStamp();}
  if((track==-1||t==track)&&(channel==0||m.getChannel()==channel)&&(m.isNoteOn()||m.isNoteOff()))selected.addEvent(m);
 }
 selected.updateMatchedPairs();juce::Array<juce::var> notes;
 for(int i=0;i<selected.getNumEvents();++i){auto* e=selected.getEventPointer(i);const auto& m=e->message;
  if(m.isNoteOn()){
   if(notes.size()>=100000)throw std::runtime_error("Pattern exceeds 100000 notes");
   notes.add(object({{"beat",m.getTimeStamp()/ppq},{"duration",e->noteOffObject?std::max(.00001,(e->noteOffObject->message.getTimeStamp()-m.getTimeStamp())/ppq):.1},{"note",m.getNoteNumber()},{"velocity",m.getFloatVelocity()}}));
  }
 }
 if(notes.isEmpty())throw std::runtime_error("The selected track and channel have no notes");
 const double bar=numerator*4./denominator;double length=std::ceil((end-1e-8)/bar)*bar;if(length<=0)length=bar;
 auto name=file.getFileName();if(track>=0)name+=" / track "+juce::String(track+1);if(channel>0)name+=" / ch "+juce::String(channel);
 return object({{"id",juce::Uuid().toString()},{"name",name},{"path",file.getFullPathName()},{"channel",channel},{"track",track},{"length",length},{"ppq",ppq},{"notes",notes}});
}
juce::var listDirectory(const juce::File& dir,bool patterns){
 if(!dir.isDirectory())throw std::runtime_error("Directory is unavailable: "+dir.getFullPathName().toStdString());
 juce::Array<juce::File> files;dir.findChildFiles(files,juce::File::findFilesAndDirectories,false);
 juce::Array<juce::var> result;
 for(auto& f:files){const bool folder=f.isDirectory();auto ext=f.getFileExtension().toLowerCase();if(!folder && !(patterns?(ext==".mid"||ext==".midi"):(ext==".wav"||ext==".aif"||ext==".aiff"||ext==".flac"||ext==".mp3"||ext==".ogg")))continue;
 result.add(object({{"id",f.getFullPathName()},{"path",f.getFullPathName()},{"name",f.getFileName()},{"folder",folder},{"size",(juce::int64)f.getSize()},{"modified",f.getLastModificationTime().toISO8601(true)}}));}
 return result;
}
}
