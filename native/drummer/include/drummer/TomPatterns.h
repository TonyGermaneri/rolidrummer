#pragma once
#include "Json.h"
#include "Scheduler.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace drummer {
// Worker-only document normalization; drum masks, never key names/pitches,
// identify toms. Mixed masks (for example a whole-kit key) stay independent.
inline bool isTomKey(const juce::var& key) {
 const auto parts=json::get(key,"parts");const auto* notes=parts.getArray();
 return notes&&!notes->isEmpty()&&std::all_of(notes->begin(),notes->end(),[](const juce::var& note){
  const double value=(double)note;return (note.isInt()||note.isInt64()||note.isDouble())&&std::isfinite(value)&&value>=0&&value<=127&&value==(int)value&&isTomNote((int)value);
 });
}
inline void synchronizeTomZones(juce::var& document) {
 auto keys=json::get(document,"keys");std::vector<juce::var> toms;
 if(auto* list=keys.getArray())for(const auto& key:*list)if(isTomKey(key))toms.push_back(key);
 if(toms.size()<2)return;
 std::sort(toms.begin(),toms.end(),[](const auto& a,const auto& b){
  const bool left=(bool)json::get(a,"neuralLock",false),right=(bool)json::get(b,"neuralLock",false);
  return left!=right?left>right:(int)json::get(a,"note")<(int)json::get(b,"note");
 });
 auto zones=json::get(toms.front(),"zones").clone();if(!zones.isArray()||zones.size()==0)return;
 // A tom zone selects one whole roll. Independent pressure/glide candidate
 // selection would split that roll back into unrelated source performances.
 for(auto& zone:*zones.getArray()){if(!zone.isObject())throw std::runtime_error("A tom zone must be an object");zone.getDynamicObject()->removeProperty("candidates");}
 for(auto& key:toms){
  auto* object=key.getDynamicObject();object->setProperty("zones",zones.clone());
  object->setProperty("zoneSource",json::get(toms.front(),"zoneSource",2));
  object->setProperty("neuralLock",json::get(toms.front(),"neuralLock",false));
  auto intent=json::get(key,"patternIntent");if(intent.isObject())intent.getDynamicObject()->setProperty("enabled",false);
 }
}
}
