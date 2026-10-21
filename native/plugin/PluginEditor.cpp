#include "PluginEditor.h"
#include "PluginPaths.h"
#include <drummer/Assets.h>
#include <drummer/Json.h>
#include <drummer/NeuralIndex.h>
#include <drummer/PatchResolver.h>
#include <drummer/PatternFamilies.h>
#include <drummer/SoundMatcher.h>
namespace {
juce::var answer(juce::var result){auto*o=new juce::DynamicObject;o->setProperty("ok",true);o->setProperty("result",result);return o;}
juce::var failure(const juce::String& error){auto*o=new juce::DynamicObject;o->setProperty("ok",false);o->setProperty("error",error);return o;}
}
DrummerEditor::DrummerEditor(DrummerProcessor&p):AudioProcessorEditor(p),plugin(p),browser(juce::WebBrowserComponent::Options{}
 .withNativeIntegrationEnabled().withKeepPageLoadedWhenBrowserIsHidden()
 .withResourceProvider([this](const auto&path){return provide(path);})
 .withNativeFunction("rdRequest",[this](const auto&args,auto complete){request(args,complete);})){
 addAndMakeVisible(browser);setResizable(true,false);addAndMakeVisible(resizeBorder);addAndMakeVisible(resizeCorner);resizeBorder.setBorderThickness(juce::BorderSize<int>(12));resizeCorner.setColour(juce::ResizableWindow::backgroundColourId,background);setResizeLimits(900,600,2600,1600);setSize(plugin.editorWidth,plugin.editorHeight);browser.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());startTimerHz(30);
}
DrummerEditor::~DrummerEditor(){stopTimer();plugin.stopPreview();plugin.cancelKitPreview();}
void DrummerEditor::paint(juce::Graphics& g){g.fillAll(background);}
void DrummerEditor::resized(){
 // Reserve a native margin and an unobstructed, fully clickable resize grip.
 browser.setBounds(getLocalBounds().withTrimmedBottom(18).reduced(12));
 resizeBorder.setBounds(getLocalBounds());resizeBorder.toFront(false);
 resizeCorner.setBounds(getWidth()-30,getHeight()-30,30,30);resizeCorner.toFront(false);
 plugin.editorWidth=getWidth();plugin.editorHeight=getHeight();
}
std::optional<juce::WebBrowserComponent::Resource> DrummerEditor::provide(const juce::String&path){
 auto root=ssb::webRoot();auto relative=path.upToFirstOccurrenceOf("?",false,false).trimCharactersAtStart("/");if(relative.isEmpty())relative="index.html";
 auto file=root.getChildFile(relative);if(!file.isAChildOf(root)||!file.existsAsFile())return{};juce::MemoryBlock data;if(!file.loadFileAsData(data))return{};
 auto ext=file.getFileExtension();auto mime=ext==".html"?"text/html":ext==".js"?"text/javascript":ext==".css"?"text/css":ext==".svg"?"image/svg+xml":ext==".woff2"?"font/woff2":"application/octet-stream";
 std::vector<std::byte> bytes(data.getSize());std::memcpy(bytes.data(),data.getData(),data.getSize());return juce::WebBrowserComponent::Resource{std::move(bytes),mime};
}
void DrummerEditor::request(const juce::Array<juce::var>&args,Complete complete){
 if(args.isEmpty()){complete(failure("Missing request"));return;}auto method=args[0].toString();juce::var payload=args.size()>1?args[1]:juce::var();
 if(method=="theme"){
  const auto colour=payload.toString();
  if(colour.length()!=7||colour[0]!='#'||colour.substring(1).containsOnly("0123456789abcdefABCDEF")==false){complete(failure("Invalid theme colour"));return;}
  background=juce::Colour::fromString("ff"+colour.substring(1));resizeCorner.setColour(juce::ResizableWindow::backgroundColourId,background);repaint();resizeCorner.repaint();complete(answer(true));return;
 }
 if(method=="ready"){plugin.cancelKitPreview();pageReady=true;auto* o=new juce::DynamicObject;o->setProperty("document",juce::JSON::parse(plugin.document()));o->setProperty("standalone",plugin.wrapperType==juce::AudioProcessor::wrapperType_Standalone);o->setProperty("revision",(juce::int64)plugin.stateRevision.load());o->setProperty("activationPending",plugin.activationPending());complete(answer(o));return;}
 if(method=="monitorKey"){monitoredKey=std::clamp((int)payload,0,127);sentPattern.clear();complete(answer(true));return;}
 if(method=="ribbon"){
  const auto action=payload.getProperty("action",{}).toString();const auto note=payload.getProperty("note",{});const int pitch=(int)note,channel=(int)payload.getProperty("channel",1);
  if(!payload.isObject()||(!note.isInt()&&!note.isDouble())||(double)note!=pitch||pitch<0||pitch>127||(action!="top"&&action!="stop"&&action!="moveTop")||channel<1||channel>15){complete(failure("Choose an individual keywave ribbon lane"));return;}
  plugin.screenMidi.push({(uint8_t)(0xf0|channel),(uint8_t)pitch,(uint8_t)(action=="top"?1:action=="stop"?2:3),0});complete(answer(true));return;
 }
 if(method=="midi"){if(auto* bytes=payload.getArray();bytes&&bytes->size()>=3)plugin.screenMidi.push({(uint8_t)(int)(*bytes)[0],(uint8_t)(int)(*bytes)[1],(uint8_t)(int)(*bytes)[2],3});complete(answer(true));return;}
 if(method=="transport"){plugin.standalonePlaying=(bool)payload;complete(answer(true));return;}
 if(method=="parameter"){auto id=payload.getProperty("id",{}).toString();if(auto*param=plugin.parameters.getParameter(id)){param->beginChangeGesture();param->setValueNotifyingHost(param->convertTo0to1((float)(double)payload.getProperty("value",0)));param->endChangeGesture();}complete(answer(true));return;}
 if(method=="stopPreview"){plugin.stopPreview();complete(answer(true));return;}
 if(method=="cancelKitPreview"){plugin.cancelKitPreview();complete(answer(true));return;}
 if(method=="libraryImport"||method=="libraryCancel"||method=="libraryIndex"||method=="libraryFamilyRefresh"){
  try{
   if(!plugin.library)throw std::runtime_error("Pattern library is unavailable");
   if(method=="libraryCancel")plugin.library->cancel();
   else if(method=="libraryFamilyRefresh")plugin.library->refreshFamilies();
   else if(method=="libraryIndex")plugin.library->buildNeuralIndex();
   else {const auto path=payload.getProperty("path",{}).toString();if(!juce::File::isAbsolutePath(path))throw std::runtime_error("Choose a library source or enter its full path");plugin.library->start(juce::File(path),payload.getProperty("mapping",{}));}
   complete(answer(plugin.library->progress()));
  }catch(const std::exception&e){complete(failure(e.what()));}return;
 }
 if(method=="chooseFolder"||method=="chooseLibraryArchive"||method=="openPatch"||method=="savePatch"||method=="relinkSound"){
  if(chooser){complete(failure("A file dialog is already open"));return;}
  const bool folder=method=="chooseFolder",save=method=="savePatch",sound=method=="relinkSound",archive=method=="chooseLibraryArchive";
  chooser=std::make_unique<juce::FileChooser>(folder?"Choose a library folder":archive?"Choose a pattern ZIP archive":save?"Save instrument patch":"Open instrument patch",juce::File(),folder?"*":archive?"*.zip":sound?"*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg":"*.rolidrummer");
  juce::Component::SafePointer<DrummerEditor> safe(this);
  chooser->launchAsync((save?(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting):juce::FileBrowserComponent::openMode)|(folder?juce::FileBrowserComponent::canSelectDirectories:juce::FileBrowserComponent::canSelectFiles),[safe,complete,method,payload](const juce::FileChooser&fc){
   auto file=fc.getResult();if(!safe)return;
   if(file==juce::File()){complete(answer(juce::var()));safe->chooser.reset();return;}
   if(method=="savePatch" || method=="openPatch") {
    auto* p=&safe->plugin;auto life=std::weak_ptr<int>(p->life);
    p->jobs.addJob([p,life,safe,complete,method,payload,file]{
     juce::var result;
     try {if(method=="savePatch"){auto target=file.hasFileExtension("rolidrummer")?file:file.withFileExtension("rolidrummer");drummer::savePortablePatch(target,payload);result=answer(target.getFullPathName());}else result=answer(drummer::loadPatch(file));}
     catch(const std::exception&e){result=failure(e.what());}
     juce::MessageManager::callAsync([life,safe,result,complete]{if(!life.expired()&&safe)complete(result);});
    });
   } else complete(answer(file.getFullPathName()));
   safe->chooser.reset();
  });return;
 }
 const auto previewSerial=method=="preview"?++plugin.previewRequest:0;
 const auto kitSerial=method=="previewKit"?plugin.beginKitPreview():0;
 const auto previewDocument=method=="preview"?plugin.document():juce::String();
 auto* p=&plugin;auto life=std::weak_ptr<int>(plugin.life);juce::Component::SafePointer<DrummerEditor> safe(this);
 auto& queue=(method=="soundMatch"||method=="librarySearch"||method=="libraryResolve"||method=="libraryBrowse"||method.startsWith("libraryFamily"))?plugin.neuralJobs:plugin.jobs;
 queue.addJob([p,life,safe,method,payload,complete,previewSerial,previewDocument,kitSerial]{
  juce::var result;std::shared_ptr<std::unique_ptr<drummer::Runtime>> built;std::shared_ptr<std::unique_ptr<drummer::Preview>> audition;
  try{
   if(method=="preview"){
    const bool pattern=payload.getProperty("kind",{}).toString()=="pattern";
    auto doc=pattern?juce::JSON::parse(previewDocument):drummer::defaultDocument();
    if(pattern){auto clip=payload.getProperty("clip",{});if(!clip.isObject())throw std::runtime_error("Choose a MIDI pattern to preview");doc.getDynamicObject()->setProperty("clips",juce::Array<juce::var>{clip});
     auto keys=doc.getProperty("keys",{});for(auto&key:*keys.getArray()){auto*z=new juce::DynamicObject;z->setProperty("upper",1.);z->setProperty("clipId",clip.getProperty("id",{}));key.getDynamicObject()->setProperty("zones",juce::Array<juce::var>{juce::var(z)});}
    }else{auto keys=doc.getProperty("keys",{});auto key=(*keys.getArray())[0].clone();auto sound=payload.getProperty("sound",{}).clone();if(!sound.isObject()){sound=key.getProperty("sound",{});sound.getDynamicObject()->setProperty("path",payload.getProperty("path",{}));sound.getDynamicObject()->setProperty("s",juce::JSON::parse(R"({"mode":"stack","repeat":1,"volume":0.8,"attack":0.001,"decay":0.01,"sustain":1,"cutoff":20000})"));}key.getDynamicObject()->setProperty("sound",sound);key.getDynamicObject()->setProperty("routes",juce::Array<juce::var>());key.getDynamicObject()->setProperty("mpeDefaultsVersion",1);doc.getDynamicObject()->setProperty("keys",juce::Array<juce::var>{key});}
    audition=std::make_shared<std::unique_ptr<drummer::Preview>>(std::make_unique<drummer::Preview>(drummer::compileDocument(doc),pattern,p->previewRate.load(),p->previewBlock.load()));result=answer(true);
   }
   else if(method=="setDocument"){built=std::make_shared<std::unique_ptr<drummer::Runtime>>(drummer::compileDocument(payload.getProperty("document",payload)));result=answer(true);}
   else if(method=="previewKit")result=answer(p->previewKit(payload.getProperty("kit",{}),(uint64_t)(juce::int64)payload.getProperty("revision",-1),kitSerial));
   else if(method=="soundMatch")result=answer(drummer::matchSoundKit(drummer::dataDirectory().getChildFile("sound-vectors.sqlite"),payload));
   else if(method=="listDirectory")result=answer(drummer::listDirectory(juce::File(payload.getProperty("path",{}).toString()),(bool)payload.getProperty("patterns",false)));
   else if(method=="waveform")result=answer(drummer::waveform(payload));
   else if(method=="inspectMidi")result=answer(drummer::inspectMidi(juce::File(payload.toString())));
   else if(method=="importMidi")result=answer(drummer::importMidi(juce::File(payload.isObject()?payload.getProperty("path",{}).toString():payload.toString()),(int)payload.getProperty("channel",-1),(int)payload.getProperty("track",-1)));
   else if(method=="libraryStatus"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");auto*o=new juce::DynamicObject;o->setProperty("catalog",p->library->catalog.status());o->setProperty("progress",p->library->progress());o->setProperty("neural",p->library->neural->status());o->setProperty("families",p->library->families->status());result=answer(o);}
   else if(method=="libraryBrowse"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->catalog.browse(payload));}
   else if(method=="librarySearch"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->neural->search(payload));}
   else if(method=="libraryResolve"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(drummer::resolveNeuralPatch(p->library->catalog,*p->library->neural,payload.getProperty("document",{}),payload));}
   // Saving a searched groove registers its family; a search alone never does,
   // since every registration rematches the catalog.
   else if(method=="libraryFamilyRegister"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->families->registerFamily(payload));p->library->refreshFamilies();}
   else if(method=="libraryFamilyRemove"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->families->remove(payload));p->library->refreshFamilies();}
   else if(method=="libraryFamilyMembers"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->families->members(payload));}
   else if(method=="libraryFamilyDecisions"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->families->decisions(payload));}
   else if(method=="libraryFamilySuggestions"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->families->suggestions((int)payload.getProperty("offset",0),(int)payload.getProperty("limit",25)));}
   else if(method=="libraryFamilyUpdate"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->families->update(payload));p->library->refreshFamilies();}
   else if(method=="libraryFamilyDecision"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->families->decideSuggestion(payload));p->library->refreshFamilies();}
   else if(method=="libraryPattern"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->catalog.pattern(payload.toString()));}
   else if(method=="libraryErrors"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->catalog.errors(payload.getProperty("runId",{}).toString(),(int)payload.getProperty("offset",0),(int)payload.getProperty("limit",100)));}
   else if(method=="systemStatus"){
    const auto directory=drummer::dataDirectory();
    auto bytes=[](const juce::File& file){return file.getSize()+file.getSiblingFile(file.getFileName()+"-wal").getSize()+file.getSiblingFile(file.getFileName()+"-shm").getSize();};
    juce::Array<juce::var> databases;
    for(const auto& name:{"shared.sqlite","library.sqlite","sound-vectors.sqlite"}){const auto file=directory.getChildFile(name);databases.add(drummer::json::object({{"name",name},{"path",file.getFullPathName()},{"bytes",(juce::int64)bytes(file)}}));}
    const auto sounds=directory.getChildFile("assets").findChildFiles(juce::File::findFiles,false);
    juce::int64 soundBytes=0;for(const auto& file:sounds)soundBytes+=file.getSize();
    result=answer(drummer::json::object({{"directory",directory.getFullPathName()},{"databases",databases},{"sounds",(int)sounds.size()},{"soundBytes",soundBytes},
     {"shared",p->shared?p->shared->statistics():juce::var()},{"patterns",p->library?p->library->catalog.status().getProperty("patterns",0):juce::var(0)}}));
   }
   else if(method=="sharedSnapshot")result=p->shared?answer(p->shared->snapshot((juce::int64)payload)):failure("Shared settings unavailable");
   else if(method=="lease")result=p->shared?answer(p->shared->lease(payload.getProperty("resource",{}).toString(),payload.getProperty("owner",{}).toString(),(bool)payload.getProperty("release",false))):failure("Shared settings unavailable");
   else if(method=="sharedCommit")result=p->shared?answer(p->shared->commit(payload)):failure("Shared settings unavailable");
   else result=failure("Unknown request: "+method);
  }catch(const std::exception&e){result=failure(e.what());}
  juce::MessageManager::callAsync([p,life,safe,result,built,audition,previewSerial,complete,payload]() mutable {
   if(life.expired())return;
   if(audition)p->publishPreview(std::move(*audition),previewSerial);
   if(built){
    try{
    const auto expected=(uint64_t)(juce::int64)payload.getProperty("revision",(juce::int64)p->stateRevision.load());
    const bool conflict=!p->apply(std::move(*built),expected,(double)payload.getProperty("activationGrid",0),(bool)payload.getProperty("preserveActivation",false));
    auto* ack=new juce::DynamicObject;ack->setProperty("conflict",conflict);ack->setProperty("revision",(juce::int64)p->stateRevision.load());ack->setProperty("document",juce::JSON::parse(p->document()));ack->setProperty("activationPending",p->activationPending());result=answer(ack);
    }catch(const std::exception&e){result=failure(e.what());}
   }
   if(safe)complete(result);
  });
 });
}
void DrummerEditor::timerCallback(){
 if(!pageReady)return;
 auto* o=new juce::DynamicObject;o->setProperty("bpm",plugin.hostBpm.load());o->setProperty("ppq",plugin.hostPpq.load());o->setProperty("playing",plugin.hostPlaying.load());o->setProperty("freePlaying",plugin.freePlaying.load());o->setProperty("revision",(juce::int64)plugin.stateRevision.load());o->setProperty("voices",plugin.engine.activeVoices());o->setProperty("peak",std::max(plugin.engine.peak(0),plugin.engine.peak(1)));o->setProperty("clips",plugin.clipEvaluations.load());o->setProperty("overflow",plugin.overflow.load());
 juce::Array<juce::var> macros;for(int i=0;i<5;++i)macros.add(plugin.parameters.getParameter("macro"+juce::String(i))->getValue());o->setProperty("macros",macros);
 o->setProperty("preview",plugin.previewPlaying.load());
 o->setProperty("program",plugin.lastProgram.load());o->setProperty("programFailed",plugin.programFailed.load());o->setProperty("programUnassigned",plugin.programUnassigned.load());o->setProperty("activationPending",plugin.activationPending());
 o->setProperty("output",plugin.parameters.getRawParameterValue("volume")->load());
 juce::Array<juce::var> contacts;for(auto&v:plugin.contacts)contacts.add(v.load());o->setProperty("contacts",contacts);
 DrummerProcessor::FingerSnapshot snapshot;
 if(plugin.readFingers(snapshot)){
  o->setProperty("touchSerial",juce::String((juce::int64)snapshot.touchSerial));o->setProperty("touchNote",snapshot.touchNote);
  juce::Array<juce::var> touches;
  for(int i=0;i<snapshot.touchCount;++i){const auto& touch=snapshot.touches[(size_t)i];auto* item=new juce::DynamicObject;
   item->setProperty("id",juce::String((juce::int64)touch.id));item->setProperty("channel",touch.channel);item->setProperty("note",touch.note);
   item->setProperty("pitch",touch.pitch);item->setProperty("slide",touch.slide);item->setProperty("pressure",touch.pressure);item->setProperty("surface",touch.surface);touches.add(item);
  }
  o->setProperty("touches",touches);
  juce::Array<juce::var> fingers;
  for(int i=0;i<snapshot.count;++i){const auto& view=snapshot.fingers[(size_t)i];auto*f=new juce::DynamicObject;
   f->setProperty("owner",juce::String((juce::int64)view.owner));f->setProperty("sustained",view.sustained);f->setProperty("toggled",view.toggled);f->setProperty("playing",view.playing);
   f->setProperty("touch",juce::String((juce::int64)view.touch));
   f->setProperty("channel",view.channel);f->setProperty("note",view.note);f->setProperty("zone",view.zone);
   f->setProperty("candidate",view.candidate);
   f->setProperty("queued",view.queued);f->setProperty("origin",view.origin);
   f->setProperty("slide",view.slide);f->setProperty("glide",view.glide);f->setProperty("pressure",view.pressure);fingers.add(juce::var(f));
  }
  o->setProperty("fingers",fingers);
  juce::Array<juce::var> latches;
  for(int origin=0;origin<128;++origin){const auto& queued=snapshot.latches[(size_t)origin];if(!queued.action)continue;
   auto* item=new juce::DynamicObject;item->setProperty("origin",origin);item->setProperty("note",queued.note>=0?queued.note:origin);item->setProperty("action",queued.action==1?"start":"stop");item->setProperty("touch",juce::String((juce::int64)queued.touch));latches.add(item);
  }
  o->setProperty("pendingLatches",latches);
  auto* performance=new juce::DynamicObject;performance->setProperty("beat",snapshot.beat);performance->setProperty("bpm",snapshot.bpm);performance->setProperty("epoch",(double)snapshot.epoch);
  performance->setProperty("dropped",(double)plugin.performanceMonitor.dropped.load());
  juce::Array<juce::var> events;drummer::PlayedEvent event;
  while(plugin.performanceMonitor.pop(event)){
   auto* item=new juce::DynamicObject;item->setProperty("type",(int)event.type);item->setProperty("epoch",(double)event.epoch);item->setProperty("beat",event.beat);item->setProperty("duration",event.duration);
   item->setProperty("owner",juce::String((juce::int64)event.owner));item->setProperty("token",juce::String((juce::int64)event.token));item->setProperty("key",event.key);item->setProperty("inputNote",event.inputNote);item->setProperty("note",event.note);item->setProperty("velocity",event.velocity);events.add(item);
  }
  performance->setProperty("events",events);o->setProperty("performance",performance);
  const auto& pattern=snapshot.patterns[(size_t)monitoredKey];
  const auto* runtime=snapshot.runtime;
  auto version=juce::String::toHexString(snapshot.patternHash.data(),(int)snapshot.patternHash.size(),0);
  auto identity=version+":"+juce::String((juce::int64)snapshot.epoch)+":"+juce::String(monitoredKey)+":"+juce::String(pattern.clip)+":"+juce::String(pattern.known?1:0);
  auto* position=new juce::DynamicObject;position->setProperty("key",monitoredKey);position->setProperty("phase",pattern.phase);position->setProperty("beat",pattern.beat);position->setProperty("playing",pattern.playing);position->setProperty("zone",pattern.zone);position->setProperty("candidate",pattern.candidate);position->setProperty("identity",identity);
  o->setProperty("patternPosition",position);
  if(identity!=sentPattern){
   sentPattern=identity;auto* data=new juce::DynamicObject;data->setProperty("identity",identity);data->setProperty("key",monitoredKey);data->setProperty("known",pattern.known);data->setProperty("name","Silent zone");data->setProperty("length",4.0);
   juce::Array<juce::var> notes;
   if(runtime&&pattern.known&&pattern.clip>=0&&pattern.clip<(int)runtime->config.clips.size()){
    const auto& clip=runtime->config.clips[(size_t)pattern.clip];data->setProperty("name",runtime->clipNames[(size_t)pattern.clip]);data->setProperty("length",clip.length);data->setProperty("origin",clip.origin);
    int count=0;for(const auto& note:clip.notes)if(runtime->config.keys[(size_t)monitoredKey].parts[(size_t)note.note]){
     ++count;if(notes.size()>=8192)continue;
     auto* n=new juce::DynamicObject;n->setProperty("beat",note.beat);n->setProperty("duration",note.duration);n->setProperty("note",note.note);n->setProperty("velocity",note.velocity);notes.add(n);
    }
    data->setProperty("totalNotes",count);
   }
   data->setProperty("notes",notes);o->setProperty("pattern",data);
  }
 }
 o->setProperty("slideChannel",plugin.lastSlideChannel.load());o->setProperty("slideValue",plugin.lastSlideValue.load());
 std::vector<ssb::VoiceView> voices;double time=0;
 if(plugin.engine.readVoices(voices,time)){juce::Array<juce::var> list;for(const auto&v:voices){int note=-1;for(int n=0;n<128;++n)if(v.sound==ssb::idHash("key-"+std::to_string(n))){note=n;break;}if(note<0)continue;auto*voice=new juce::DynamicObject;voice->setProperty("id",(juce::int64)v.id);voice->setProperty("note",note);voice->setProperty("pos",v.position);voice->setProperty("dur",v.duration);voice->setProperty("velocity",v.velocity);voice->setProperty("grains",v.grains);list.add(juce::var(voice));}o->setProperty("playheads",list);o->setProperty("audioTime",time);}
 std::vector<ssb::GrainView> grains;plugin.engine.readGrains(grains,400);juce::Array<juce::var> marks;for(const auto&g:grains){auto*m=new juce::DynamicObject;m->setProperty("voice",(juce::int64)g.voice);m->setProperty("when",g.when);m->setProperty("pos",g.pos);m->setProperty("len",g.len);m->setProperty("dur",g.dur);m->setProperty("pan",g.pan);m->setProperty("gain",g.gain);m->setProperty("reverse",g.reverse);marks.add(juce::var(m));}o->setProperty("grainMarks",marks);
 juce::Array<juce::var> messages;ssb::MidiMessage m;while(plugin.midiToPage.pop(m))messages.add(juce::Array<juce::var>{m.status,m.data1,m.data2});o->setProperty("midi",messages);
 browser.emitEventIfBrowserIsVisible("rdTelemetry",juce::var(o));
}
