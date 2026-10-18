#include <drummer/PatchBank.h>
#include <drummer/SharedStore.h>
#include "../src/LibrarySqlite.h"
#include <iostream>
#include <stdexcept>
using namespace drummer;
using namespace drummer::json;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
juce::var field(const juce::var& snapshot,const char* key){for(const auto& row:*snapshot.getArray())if(get(row,"key").toString()==key)return row;return {};}
juce::var operation(const juce::String& key,const juce::var& value,int64_t revision=0){return object({{"key",key},{"value",value},{"revision",(juce::int64)revision}});}
juce::var patch(const juce::String& name,const juce::String& id={}){auto result=defaultDocument();set(result,"name",name);result.getDynamicObject()->removeProperty("id");if(id.isNotEmpty())set(result,"id",id);return result;}
void save(SharedStore& store,const juce::String& key,const juce::var& value){
 int64_t revision=0;const auto snapshot=store.snapshot();for(const auto& row:*snapshot.getArray())if(get(row,"key").toString()==key)revision=(juce::int64)get(row,"revision");
 check((bool)get(store.commit(juce::Array<juce::var>{operation(key,value,revision)}),"ok",false),"Patch save failed");
}
}
int main(){try{
 const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("rd-bank-test","");root.createDirectory();const auto file=root.getChildFile("shared.sqlite");
 // Build the prior on-disk schema directly: no identity or slot metadata.
 {
  sql::Database db(file);db.exec("CREATE TABLE fields(key TEXT PRIMARY KEY,value TEXT NOT NULL,revision INTEGER NOT NULL);CREATE TABLE clock(id INTEGER PRIMARY KEY,revision INTEGER NOT NULL);INSERT INTO clock VALUES(1,3)");
  for(const auto* name:{"Zulu","Alpha"}){sql::Statement insert(db,"INSERT INTO fields VALUES(?,?,2)");insert.text(1,"patch/"+juce::String(name));insert.text(2,juce::JSON::toString(patch(name),true));insert.done();}
  db.exec("INSERT INTO fields VALUES('camera','42',3)");
 }
 {
  SharedStore first(file),second(file);auto snapshot=first.snapshot();const auto initial=patchBank(snapshot);
  check(initial.size()==128&&get(initial[0],"id").toString()==factoryPatchId&&get(initial[1],"name").toString()=="Alpha"&&get(initial[2],"name").toString()=="Zulu","Migration did not preserve the old bank order");
  check(first.snapshot(3).getArray()->size()==3,"Patch identities and slot metadata were not published in one migration revision");
  check(get(field(snapshot,"camera"),"value")==juce::var(42),"Migration changed an unrelated shared field");
  save(first,"ui/theme","noir49");save(first,"ui/enabled",false);
  check(get(field(second.snapshot(),"ui/theme"),"value").toString()=="noir49"&&get(field(second.snapshot(),"ui/enabled"),"value").isBool(),"Scalar shared settings did not round-trip");
  const auto alpha=get(initial[1],"id"),zulu=get(initial[2],"id");
  auto renamed=initial[1].clone();set(renamed,"name","Renamed Alpha");
  const auto oldRevision=(juce::int64)get(field(snapshot,"patch/Alpha"),"revision");
  save(first,"patch/Alpha",renamed);save(second,"patch/Aardvark",patch("Aardvark","new-aardvark"));
  check(!(bool)get(second.commit(juce::Array<juce::var>{operation("patch/Alpha",initial[1],oldRevision)}),"ok",true),"A stale writer overwrote the renamed patch");
  const auto grown=patchBank(second.snapshot());
  check(get(grown[1],"id")==alpha&&get(grown[1],"name").toString()=="Renamed Alpha"&&get(grown[2],"id")==zulu&&get(grown[3],"name").toString()=="Aardvark","Rename or an earlier name shifted assigned programs");
  const auto unchanged=juce::JSON::toString(second.snapshot(),true);
  bool rejected=false;try{save(second,"patch/Collision",patch("Collision",alpha.toString()));}catch(...){rejected=true;}
  check(rejected&&juce::JSON::toString(second.snapshot(),true)==unchanged,"A duplicate identity changed the stored bank");
  rejected=false;try{save(second,"patch/Alpha",patch("Replacement","different-identity"));}catch(...){rejected=true;}
  check(rejected&&juce::JSON::toString(second.snapshot(),true)==unchanged,"A patch silently replaced its stable identity");
  save(second,"patch/Zulu",{});check(patchBank(first.snapshot())[2].isVoid(),"A missing program wrapped to a different patch");
  save(second,"patch/New",patch("New","new-after-delete"));check(get(patchBank(first.snapshot())[4],"id").toString()=="new-after-delete","A new patch stole a reserved missing program");
  save(second,"patch/Zulu",initial[2]);check(get(patchBank(first.snapshot())[2],"id")==zulu,"Restoring a saved patch did not restore its original program");
  juce::Array<juce::var> many;for(int i=0;i<130;++i)many.add(operation("patch/Capacity-"+juce::String(i).paddedLeft('0',3),patch("Capacity "+juce::String(i),"capacity-"+juce::String(i))));
  check((bool)get(first.commit(many),"ok",false),"A bank with more than 128 saved patches failed to save");
  const auto full=patchBank(first.snapshot());std::set<juce::String> ids;
  for(const auto& entry:full)check(entry.isObject()&&ids.insert(get(entry,"id").toString()).second,"Full MIDI bank contains a hole or duplicate assignment");
  for(size_t i=0;i<4;++i)check(get(full[i],"id")==get(grown[i],"id"),"Bank exhaustion evicted an assigned patch");
  const auto mapBefore=get(field(first.snapshot(),programBankKey),"value");
  save(second,"patch/Overflow",patch("Overflow","overflow"));
  check(juce::JSON::toString(mapBefore,true)==juce::JSON::toString(get(field(first.snapshot(),programBankKey),"value"),true),"Saving beyond capacity changed MIDI assignments");
 }
 {
  SharedStore reopened(file);const auto bank=patchBank(reopened.snapshot());check(get(bank[1],"name").toString()=="Renamed Alpha"&&get(bank[3],"name").toString()=="Aardvark","Fresh-process-style reopen changed stable slots");
 }
 // Older banks suppressed the builtin when a saved patch had its display name.
 const auto legacy=juce::Array<juce::var>{operation("patch/Alpha",patch("Alpha")),operation("patch/Z factory",patch(get(defaultDocument(),"name").toString()))};
 check(get(patchBank(legacy)[0],"name").toString()=="Alpha","Migration inserted a new factory slot ahead of an existing legacy assignment");
 root.deleteRecursively();std::cout<<"patch bank: legacy atomic migration, stable IDs, rename/growth, stale writers, missing/reserved programs, capacity, rollback and reopen passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
