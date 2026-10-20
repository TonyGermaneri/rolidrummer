#include "PluginProcessor.h"
#include <drummer/Json.h>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 auto player=std::make_unique<DrummerProcessor>();auto doc=drummer::defaultDocument();
 doc.getDynamicObject()->setProperty("launchGrid",0);
 doc.getDynamicObject()->setProperty("clips",juce::JSON::parse(R"([{"id":"test","name":"Repeated kick","length":0.25,"notes":[{"beat":0,"duration":0.05,"note":36,"velocity":1}]}])"));
 auto keys=doc.getProperty("keys",{});for(auto& key:*keys.getArray()){
  key.getDynamicObject()->setProperty("routes",juce::Array<juce::var>{});
  key.getDynamicObject()->setProperty("zones",juce::JSON::parse(R"([{"upper":1,"clipId":"test"}])"));
 }
 player->apply(drummer::compileDocument(doc));player->prepareToPlay(48000,256);
 juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
 auto block=[&]{player->processBlock(audio,midi);juce::Timer::callPendingTimersSynchronously();};
 midi.addEvent(juce::MidiMessage::noteOn(2,48,(juce::uint8)110),0);block();
 DrummerProcessor::FingerSnapshot before,after;check(player->readFingers(before)&&before.count==1,"Preview fixture has no held keywave");
 const auto otherVolume=before.runtime->sounds[50]->s.volume;
 const auto committed=player->document();const auto revision=player->stateRevision.load();
 auto sound=keys.getArray()->getReference(0).getProperty("sound",{}).clone();
 sound.getProperty("s",{}).getDynamicObject()->setProperty("volume",0.0);
 const auto assignment=drummer::json::object({{"note",48},{"sound",sound}});
 const auto kit=drummer::json::object({{"version",1},{"keys",juce::Array<juce::var>{assignment}}});
 check(player->previewKit(kit,revision,player->beginKitPreview()),"Kit preview was rejected");block();
 check(player->readFingers(after)&&after.count==1&&after.fingers[0].owner==before.fingers[0].owner,"Kit preview interrupted the held contact");
 check(after.runtime->sounds[48]->s.volume==0&&after.patternHash==before.patternHash,"Preview did not replace the live sound independently of patterns");
 check(after.runtime->sounds[50]->s.volume==otherVolume,"Single-key preview changed another keywave");
 check(player->document()==committed&&player->stateRevision.load()==revision,"Preview changed the committed document or revision");
 juce::MemoryBlock saved;player->getStateInformation(saved);auto session=juce::ValueTree::readFromData(saved.getData(),saved.getSize());
 check(session.getProperty("document").toString()==committed,"Host session saved uncommitted preview sounds");
 // Let the old hit ring out, then confirm repeated hits use the preview sound.
 double silent=0;for(int i=0;i<500;++i){block();if(i>300)for(int n=0;n<audio.getNumSamples();++n)silent+=audio.getSample(0,n)*audio.getSample(0,n);}
 check(silent<1e-10,"Live scheduler hits did not audition the muted preview sound");
 player->cancelKitPreview();block();
 check(player->readFingers(after)&&after.runtime->sounds[48]->s.volume>0&&after.fingers[0].owner==before.fingers[0].owner,"Cancel failed to restore sounds without releasing the keywave");
 double audible=0;for(int i=0;i<800;++i){block();for(int n=0;n<audio.getNumSamples();++n)audible+=audio.getSample(0,n)*audio.getSample(0,n);}
 check(audible>.001,"Restored kit is not audible");
 check(player->document()==committed&&player->stateRevision.load()==revision,"Cancel created a document edit");
 auto ticket=player->beginKitPreview();player->cancelKitPreview();check(!player->previewKit(kit,revision,ticket),"Cancelled worker published a late preview");
 ticket=player->beginKitPreview();const auto newer=player->beginKitPreview();check(!player->previewKit(kit,revision,ticket),"Older search replaced a newer preview request");
 check(player->previewKit(kit,revision,newer),"New search preview failed");block();
 // Applying a kit is the ordinary, revision-checked document commit.
 auto applied=juce::JSON::parse(committed);applied.getProperty("keys",{}).getArray()->getReference(0).getDynamicObject()->setProperty("sound",sound.clone());
 check(player->apply(drummer::compileDocument(applied),revision),"Applying the preview failed");
 const auto appliedDocument=player->document();player->cancelKitPreview();block();
 check(player->readFingers(after)&&after.runtime->sounds[48]->s.volume==0&&player->document()==appliedDocument&&player->stateRevision.load()==revision+1,"Cancel reverted an applied kit");
 // A recall/edit invalidates both active and queued previews.
 ticket=player->beginKitPreview();player->apply(drummer::compileDocument(doc));
 check(!player->previewKit(kit,revision+1,ticket),"Late preview replaced a host recall");player->cancelKitPreview();block();
 check(player->readFingers(after)&&after.runtime->sounds[48]->s.volume>0,"Host recall left preview sounds active");
 player->apply(drummer::compileDocument(doc),{},4);
 bool rejected=false;try{player->previewKit(kit,player->stateRevision.load(),player->beginKitPreview());}catch(const std::exception&){rejected=true;}
 check(rejected&&player->activationPending(),"Preview displaced a bar-queued patch activation");
 std::cout<<"kit-preview: live audition, contact continuity, untouched session/revision, Cancel, Apply and stale-worker guards passed\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
