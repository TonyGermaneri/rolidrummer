#pragma once
#include "State.h"
#include "Json.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace drummer {
inline constexpr const char* programBankKey = "midi/program-bank";
inline constexpr const char* factoryPatchId = "builtin:factory";

// SharedStore commits these canonical changes atomically with the patch edit.
// Existing slots, including temporarily missing identities, remain reserved.
inline juce::Array<juce::var> reconcilePatchBank(const juce::var& snapshot) {
 using namespace json;
 std::map<juce::String,juce::var> patches;juce::var bank;
 if(auto* entries=snapshot.getArray())for(const auto& entry:*entries){
  const auto key=get(entry,"key").toString();const auto value=get(entry,"value");
  if(key.startsWith("patch/")&&value.isObject())patches.emplace(key,value);
  if(key==programBankKey)bank=value;
 }
 juce::Array<juce::var> changes;
 if(patches.empty()&&bank.isVoid())return changes;
 std::set<juce::String> identities;
 for(auto& [key,patch]:patches){
  auto id=get(patch,"id").toString();
  if(auto raw=get(patch,"id");!raw.isVoid()&&!raw.isString())throw std::runtime_error("Invalid saved patch identity");
  if(id.isEmpty()||id==factoryPatchId){
   id="patch-"+juce::SHA256(key.toUTF8()).toHexString();patch=patch.clone();set(patch,"id",id);
   changes.add(object({{"key",key},{"value",patch}}));
  }
  if(id.length()>128||!identities.insert(id).second)throw std::runtime_error("Saved patches require distinct stable identities");
 }
 juce::Array<juce::var> slots;
 if(!bank.isVoid()){
  if(!bank.isObject()||(int)get(bank,"version")!=1||!get(bank,"slots").isArray()||get(bank,"slots").getArray()->size()!=128)throw std::runtime_error("Unsupported MIDI program bank");
  slots=*get(bank,"slots").getArray();std::set<juce::String> assigned;
  for(const auto& slot:slots)if(!slot.isVoid()&&(!slot.isString()||slot.toString().isEmpty()||!assigned.insert(slot.toString()).second))throw std::runtime_error("Invalid MIDI program assignment");
 }else{
  // Preserve the former factory-plus-sorted-key order once on migration.
  const auto factoryName=get(defaultDocument(),"name");
  if(std::none_of(patches.begin(),patches.end(),[&](const auto& p){return get(p.second,"name")==factoryName;}))slots.add(factoryPatchId);
  for(const auto& [key,patch]:patches)if(slots.size()<128)slots.add(get(patch,"id"));
  while(slots.size()<128)slots.add(juce::var());
 }
 for(const auto& [key,patch]:patches){
  const auto id=get(patch,"id");if(slots.contains(id))continue;
  for(int slot=0;slot<128;++slot)if(slots[slot].isVoid()){slots.set(slot,id);break;}
 }
 const auto updated=object({{"version",1},{"slots",slots}});
 if(juce::JSON::toString(updated,true)!=juce::JSON::toString(bank,true))changes.add(object({{"key",programBankKey},{"value",updated}}));
 return changes;
}

// Fixed MIDI addresses. Unassigned/missing positions are void and do not wrap.
// This helper runs only on a worker, never in audio rendering.
inline std::vector<juce::var> patchBank(const juce::var& input) {
 using namespace json;
 auto snapshot=input.clone();if(!snapshot.isArray())snapshot=juce::Array<juce::var>();
 for(const auto& change:reconcilePatchBank(snapshot)){
  auto* entries=snapshot.getArray();entries->removeIf([&](const auto& entry){return get(entry,"key")==get(change,"key");});entries->add(change);
 }
 std::map<juce::String,juce::var> patches;auto factory=defaultDocument();set(factory,"id",factoryPatchId);patches.emplace(factoryPatchId,factory);
 juce::var bank;
 for(const auto& entry:*snapshot.getArray()){
  const auto key=get(entry,"key").toString();const auto value=get(entry,"value");
  if(key.startsWith("patch/")&&value.isObject())patches.emplace(get(value,"id").toString(),value);
  if(key==programBankKey)bank=value;
 }
 std::vector<juce::var> result(128);
 if(auto slots=get(bank,"slots");slots.isArray())for(int i=0;i<128;++i){auto found=patches.find((*slots.getArray())[i].toString());if(found!=patches.end())result[(size_t)i]=found->second;}
 else result[0]=factory;
 return result;
}
}
