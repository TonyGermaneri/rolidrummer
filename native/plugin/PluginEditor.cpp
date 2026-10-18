#include "PluginEditor.h"
#include "PluginPaths.h"
#include <drummer/Assets.h>
#include <drummer/NeuralIndex.h>
#include <drummer/PatchResolver.h>
#include <drummer/PatternFamilies.h>
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
DrummerEditor::~DrummerEditor(){stopTimer();plugin.stopPreview();}
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
 if(method=="ready"){pageReady=true;auto* o=new juce::DynamicObject;o->setProperty("document",juce::JSON::parse(plugin.document()));o->setProperty("standalone",plugin.wrapperType==juce::AudioProcessor::wrapperType_Standalone);o->setProperty("revision",(juce::int64)plugin.stateRevision.load());o->setProperty("activationPending",plugin.activationPending());complete(answer(o));return;}
 if(method=="midi"){if(auto* bytes=payload.getArray();bytes&&bytes->size()>=3)plugin.screenMidi.push({(uint8_t)(int)(*bytes)[0],(uint8_t)(int)(*bytes)[1],(uint8_t)(int)(*bytes)[2],3});complete(answer(true));return;}
 if(method=="transport"){plugin.standalonePlaying=(bool)payload;complete(answer(true));return;}
 if(method=="parameter"){auto id=payload.getProperty("id",{}).toString();if(auto*param=plugin.parameters.getParameter(id)){param->beginChangeGesture();param->setValueNotifyingHost(param->convertTo0to1((float)(double)payload.getProperty("value",0)));param->endChangeGesture();}complete(answer(true));return;}
 if(method=="stopPreview"){plugin.stopPreview();complete(answer(true));return;}
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
 const auto previewDocument=method=="preview"?plugin.document():juce::String();
 auto* p=&plugin;auto life=std::weak_ptr<int>(plugin.life);juce::Component::SafePointer<DrummerEditor> safe(this);
 auto& queue=(method=="librarySearch"||method=="libraryResolve"||method=="libraryBrowse"||method.startsWith("libraryFamily"))?plugin.neuralJobs:plugin.jobs;
 queue.addJob([p,life,safe,method,payload,complete,previewSerial,previewDocument]{
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
   else if(method=="listDirectory")result=answer(drummer::listDirectory(juce::File(payload.getProperty("path",{}).toString()),(bool)payload.getProperty("patterns",false)));
   else if(method=="waveform")result=answer(drummer::waveform(payload));
   else if(method=="inspectMidi")result=answer(drummer::inspectMidi(juce::File(payload.toString())));
   else if(method=="importMidi")result=answer(drummer::importMidi(juce::File(payload.isObject()?payload.getProperty("path",{}).toString():payload.toString()),(int)payload.getProperty("channel",-1),(int)payload.getProperty("track",-1)));
   else if(method=="libraryStatus"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");auto*o=new juce::DynamicObject;o->setProperty("catalog",p->library->catalog.status());o->setProperty("progress",p->library->progress());o->setProperty("neural",p->library->neural->status());o->setProperty("families",p->library->families->status());result=answer(o);}
   else if(method=="libraryBrowse"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->catalog.browse(payload));}
   else if(method=="librarySearch"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->neural->search(payload));}
   else if(method=="libraryResolve"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");auto document=drummer::resolveNeuralPatch(p->library->catalog,*p->library->neural,payload.getProperty("document",{}),payload);if(document.getProperty("neural",{}).getProperty("familyBuildId",{}).toString().isEmpty())p->library->families->registerFamily(document.getProperty("neural",{}));p->library->refreshFamilies();result=answer(document);}
   else if(method=="libraryFamilyMembers"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->families->members(payload));}
   else if(method=="libraryFamilyDecisions"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->families->decisions(payload));}
   else if(method=="libraryFamilySuggestions"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->families->suggestions((int)payload.getProperty("offset",0),(int)payload.getProperty("limit",25)));}
   else if(method=="libraryFamilyUpdate"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->families->update(payload));p->library->refreshFamilies();}
   else if(method=="libraryFamilyDecision"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->families->decideSuggestion(payload));p->library->refreshFamilies();}
   else if(method=="libraryPattern"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->catalog.pattern(payload.toString()));}
   else if(method=="libraryErrors"){if(!p->library)throw std::runtime_error("Pattern library is unavailable");result=answer(p->library->catalog.errors(payload.getProperty("runId",{}).toString(),(int)payload.getProperty("offset",0),(int)payload.getProperty("limit",100)));}
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
  juce::Array<juce::var> fingers;
  for(int i=0;i<snapshot.count;++i){const auto& view=snapshot.fingers[(size_t)i];auto*f=new juce::DynamicObject;
   f->setProperty("owner",juce::String((juce::int64)view.owner));f->setProperty("sustained",view.sustained);f->setProperty("toggled",view.toggled);f->setProperty("playing",view.playing);
   f->setProperty("channel",view.channel);f->setProperty("note",view.note);f->setProperty("zone",view.zone);
   f->setProperty("candidate",view.candidate);
   f->setProperty("slide",view.slide);f->setProperty("glide",view.glide);f->setProperty("pressure",view.pressure);fingers.add(juce::var(f));
  }
  o->setProperty("fingers",fingers);
 }
 o->setProperty("slideChannel",plugin.lastSlideChannel.load());o->setProperty("slideValue",plugin.lastSlideValue.load());
 std::vector<ssb::VoiceView> voices;double time=0;
 if(plugin.engine.readVoices(voices,time)){juce::Array<juce::var> list;for(const auto&v:voices){int note=-1;for(int n=0;n<128;++n)if(v.sound==ssb::idHash("key-"+std::to_string(n))){note=n;break;}if(note<0)continue;auto*voice=new juce::DynamicObject;voice->setProperty("id",(juce::int64)v.id);voice->setProperty("note",note);voice->setProperty("pos",v.position);voice->setProperty("dur",v.duration);voice->setProperty("velocity",v.velocity);voice->setProperty("grains",v.grains);list.add(juce::var(voice));}o->setProperty("playheads",list);o->setProperty("audioTime",time);}
 std::vector<ssb::GrainView> grains;plugin.engine.readGrains(grains,400);juce::Array<juce::var> marks;for(const auto&g:grains){auto*m=new juce::DynamicObject;m->setProperty("voice",(juce::int64)g.voice);m->setProperty("when",g.when);m->setProperty("pos",g.pos);m->setProperty("len",g.len);m->setProperty("dur",g.dur);m->setProperty("pan",g.pan);m->setProperty("gain",g.gain);m->setProperty("reverse",g.reverse);marks.add(juce::var(m));}o->setProperty("grainMarks",marks);
 juce::Array<juce::var> messages;ssb::MidiMessage m;while(plugin.midiToPage.pop(m))messages.add(juce::Array<juce::var>{m.status,m.data1,m.data2});o->setProperty("midi",messages);
 browser.emitEventIfBrowserIsVisible("rdTelemetry",juce::var(o));
}
