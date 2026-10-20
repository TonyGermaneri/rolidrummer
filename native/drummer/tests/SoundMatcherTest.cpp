#include <drummer/SoundMatcher.h>
#include <drummer/Json.h>
#include <iostream>
#include <stdexcept>
using namespace drummer;using namespace drummer::json;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
 const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("rd-sound-match","");root.createDirectory();
 struct Cleanup{juce::File root;~Cleanup(){root.deleteRecursively();}}cleanup{root};
 auto asset=[](const char* id,const char* name,int note){return object({{"id",id},{"path","factory:"+juce::String(id)},{"name",name},{"preset",object({{"parts",juce::Array<juce::var>{note}},{"sound",object({{"path","factory:"+juce::String(id)},{"s",object({{"name",name}})}})}})}});};
 const auto options=object({{"query","heavy rock"},{"assets",juce::Array<juce::var>{asset("kick","Aggressive heavy rock kick",36),asset("soft-kick","Quiet soft jazz kick",36),asset("snare","Powerful rock snare",38),asset("hat","Bright open hi hat",46)}},
  {"keys",juce::Array<juce::var>{object({{"note",48},{"part","Kick"},{"parts",juce::Array<juce::var>{36}}}),object({{"note",50},{"part","Open hat"},{"parts",juce::Array<juce::var>{46}}}),object({{"note",52},{"part","Snare"},{"parts",juce::Array<juce::var>{38}}})}}});
 const auto cache=root.getChildFile("vectors.sqlite");const auto first=matchSoundKit(cache,options);
 check(get(first,"assignments").size()==3,"Sound matching missed compatible keywave roles");
 for(const auto& assignment:*get(first,"assignments").getArray()){
  const auto note=(int)get(assignment,"note");const auto id=get(get(assignment,"asset"),"id").toString();
  check((note==48&&id=="kick")||(note==50&&id=="hat")||(note==52&&id=="snare"),"Semantic sound matching changed a drum role or ignored the style");
 }
 auto cpu=options.clone();set(cpu,"cpu",true);
 check(juce::JSON::toString(get(first,"assignments"),true)==juce::JSON::toString(get(matchSoundKit(cache,cpu),"assignments"),true),"Cached CPU/Metal kit matching changed assignments");
 auto directory=root.getChildFile("samples");directory.createDirectory();directory.getChildFile("Analog 909 BD_01.wav").replaceWithText("sample fixture");directory.getChildFile("Analog 909 SD_01.wav").replaceWithText("sample fixture");
 auto files=options.clone();set(files,"assets",juce::Array<juce::var>());set(files,"roots",juce::Array<juce::var>{directory.getFullPathName()});
 const auto matched=matchSoundKit(cache,files);check((int)get(matched,"sounds")==2,"Sound folders were not indexed");
 for(const auto& assignment:*get(matched,"assignments").getArray()){
  const auto note=(int)get(assignment,"note");const auto name=get(get(assignment,"asset"),"name").toString();check((note==48&&name.contains("BD_01"))||(note==52&&name.contains("SD_01")),"Filename matching routed an incompatible instrument into a keywave");
 }
 check(cache.existsAsFile(),"Sound vector cache was not persisted");
 std::cout<<"sound matching: pretrained style retrieval, role gates, folder samples, persistent cache and CPU/Metal agreement passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
