#include <drummer/PatternFamilies.h>
#include <drummer/PatchResolver.h>
#include <drummer/Json.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../src/LibrarySqlite.h"
#include <iostream>
#include <stdexcept>
using namespace drummer;
using namespace drummer::json;
namespace {
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
juce::String encode(const juce::var& value){return juce::JSON::toString(value,true);}
struct Fixture{
 juce::File root=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("rd-families","");
 Fixture(){check(root.createDirectory().wasOk(),"Cannot create family fixture");}
 ~Fixture(){root.deleteRecursively();}
 void midi(const juce::File& source,const juce::String& name,int seed,int numerator=4,int denominator=4){auto file=source.getChildFile(name);file.getParentDirectory().createDirectory();juce::MidiMessageSequence events;events.addEvent(juce::MidiMessage::timeSignatureMetaEvent(numerator,denominator),0);for(int i=0;i<8;++i){const int note=i%4==0?36:i%4==2?38:42;events.addEvent(juce::MidiMessage::noteOn(10,note,(juce::uint8)(60+(seed*7+i*3)%65)),i*480.+seed);events.addEvent(juce::MidiMessage::noteOff(10,note),i*480.+seed+96);}events.addEvent(juce::MidiMessage::endOfTrack(),numerator*3840./denominator);juce::MidiFile midi;midi.setTicksPerQuarterNote(960);midi.addTrack(events);juce::FileOutputStream stream(file);check(midi.writeTo(stream),"Cannot write family MIDI fixture");}
};
int count(const juce::var& status,const juce::String& id,const char* field="members"){for(const auto& family:*get(status,"families").getArray())if(get(family,"id").toString()==id)return (int)get(family,field);throw std::runtime_error("Registered family missing");}
juce::var suggestion(PatternFamilies& families,const juce::String& query){const auto page=families.suggestions(0,100);for(const auto& row:*get(page,"rows").getArray())if(get(row,"query").toString()==query)return row;throw std::runtime_error("Expected coherent family suggestion missing");}
juce::var decision(PatternFamilies& families,const juce::String& family,const juce::String& pattern,const char* action){return families.update(object({{"revision",get(families.status(),"revision")},{"familyId",family},{"patternId",pattern},{"action",action}}));}
}
int main(int argc,char** argv){try{
 if(argc!=2)throw std::runtime_error("Expected pinned neural assets");
 check(familyIdentity("Heavy Rock",4,4,"straight")==familyIdentity("Hard Rock",4,4,"straight"),"Canonical synonyms create duplicate families");
 check(familyIdentity("Heavy Rock 4/4")!=familyIdentity("Heavy Rock 7/8"),"Meter was embedded away from family identity");
 check(familyIdentity("4/4")!=familyIdentity("7/8"),"Explicit musical constraints cannot identify a family without genre words");
 check(familyIdentity("Heavy Rock",4,4,"straight")!=familyIdentity("Heavy Rock",4,4,"triplet"),"Feel missing from family identity");
 Fixture fixture;const auto source=fixture.root.getChildFile("first"),added=fixture.root.getChildFile("second"),db=fixture.root.getChildFile("library.sqlite");
 for(int i=0;i<5;++i)fixture.midi(source,"Hard Rock/Grooves/"+juce::String(i)+".mid",i+1);
 auto original=source.getChildFile("Hard Rock/Grooves/0.mid"),alias=source.getChildFile("GM MIDI Pack/Hard Rock/Grooves/99.mid");alias.getParentDirectory().createDirectory();check(original.copyFileTo(alias),"Cannot duplicate source alias");
 PatternLibrary catalog(db);catalog.importSource(source);NeuralIndex index(db,juce::File(juce::String::fromUTF8(argv[1])));index.build();PatternFamilies families(db);
 const auto rock=families.registerFamily(object({{"query","Heavy Rock"},{"numerator",4},{"denominator",4},{"feel","straight"}}));const auto rockId=get(rock,"id").toString();
 check(get(families.registerFamily(object({{"query","Hard Rock"},{"numerator",4},{"denominator",4},{"feel","straight"}})),"id").toString()==rockId,"Synonym registration created another family");
 check(get(families.status(),"families").getArray()->size()==1,"Duplicate registration grew the registry");
 const auto gpu=index.affinities("Heavy Rock"),cpu=index.affinities("Heavy Rock",{},true);check(gpu.groups.size()==cpu.groups.size(),"Family affinity inventory differs by provider");for(size_t i=0;i<gpu.groups.size();++i)check(gpu.groups[i].id==cpu.groups[i].id&&gpu.groups[i].score==cpu.groups[i].score,"Family GPU/CPU canonical affinities differ");
 check(get(families.reconcile(index),"state").toString()=="complete","Initial family snapshot did not publish");
 auto status=families.status();check(count(status,rockId)==5&&count(status,rockId,"ambiguous")==0,"Duplicate aliases inflate family support");
 const auto initialBuild=get(status,"buildId").toString();const auto initialMembers=families.members(object({{"familyId",rockId},{"buildId",initialBuild}}));
 {sql::Database writer(db);sql::Transaction preparation(writer);check(get(families.reconcile(index),"state").toString()=="busy"&&get(families.status(),"buildId").toString()==initialBuild,"Catalog preparation failed family matching instead of retaining its ready build for retry");}
 const auto base=juce::JSON::parse(R"({"version":1,"clips":[],"keys":[{"note":48,"parts":[36],"sound":{"path":"factory:kick"}}]})");
 auto resolveOptions=object({{"query","Heavy Rock"},{"indexId",get(index.status(),"readyIndexId")}});
 const auto initialResolved=resolveNeuralPatch(catalog,index,base,resolveOptions);
 check(get(get(initialResolved,"neural"),"familyBuildId").toString()==initialBuild,"Resolution did not pin its family decisions");
 const auto excluded=get((*get(initialMembers,"rows").getArray())[0],"patternId").toString();
 decision(families,rockId,excluded,"exclude");check((bool)get(families.status(),"stale"),"Manual decision does not mark membership stale");families.reconcile(index);
 const auto decisionRevision=get(families.status(),"revision");decision(families,rockId,excluded,"exclude");check(get(families.status(),"revision")==decisionRevision,"Repeating an unchanged family decision needlessly rebuilds membership");
 check(count(families.status(),rockId)==4,"Explicit pattern exclusion was not respected");
 const auto decisions=families.decisions(object({{"familyId",rockId}}));const auto decisionRows=get(decisions,"rows");check(decisionRows.getArray()->size()==1&&get((*decisionRows.getArray())[0],"patternId").toString()==excluded&&get((*decisionRows.getArray())[0],"decision").toString()=="exclude","Manual exclusions cannot be reviewed independently of current membership");
 const auto fresh=resolveNeuralPatch(catalog,index,base,resolveOptions);for(const auto& clip:*get(fresh,"clips").getArray())check(get(clip,"id").toString()!=excluded,"Excluded family member reached the resolved performance");
 auto pinnedOptions=resolveOptions.clone();set(pinnedOptions,"familyBuildId",initialBuild);
 check(encode(initialResolved)==encode(resolveNeuralPatch(catalog,index,base,pinnedOptions)),"Pinned family resolution changed after a manual decision");
 auto copied=initialResolved.clone();set(copied,"id","saved-family-copy");check(get(resolveNeuralPatch(catalog,index,copied,resolveOptions),"id").toString()=="saved-family-copy","Family growth changed a saved patch identity");
 check(encode(initialMembers)==encode(families.members(object({{"familyId",rockId},{"buildId",initialBuild}}))),"A later manual decision changed a committed membership snapshot");
 bool stale=false;try{families.update(object({{"revision",0},{"familyId",rockId},{"action","rename"},{"name","Stale"}}));}catch(const std::exception&){stale=true;}check(stale,"Stale family writer was accepted");
 fixture.midi(added,"Hard Rock/Fills/New.mid",50);
 for(const auto* style:{"Bossa Nova","Jazz Brushes","Funk"})for(int i=0;i<3;++i)fixture.midi(added,juce::String(style)+(i==2?"/Fills/":"/Grooves/")+juce::String(i)+".mid",60+(style[0]+i)%40);
 for(int i=0;i<3;++i)fixture.midi(added,"00071@FullKit/0991@4#4_STRAIGHT_HiHat/EighthNote/Theme-20/Variation-"+juce::String(i)+"/Beat-112-0.mid",110+i);
 for(int i=0;i<3;++i)fixture.midi(added,"Grooves/Half-Time/140 HT "+juce::String(i)+" QTR Ride.mid",120+i);
 for(int i=0;i<3;++i)fixture.midi(added,"Four on the Floor/Grooves/"+juce::String(i)+".mid",150+i);
 fixture.midi(added,"Hard Rock/Seven/Groove.mid",140,7,8);
 catalog.importSource(added);index.build();bool cancel=false;NeuralBuildOptions pause;pause.cancelled=[&]{return cancel;};pause.progress=[&](const auto& p){if(get(p,"stage").toString()=="family-features")cancel=true;};
 const auto previousBuild=get(families.status(),"buildId");check(get(families.reconcile(index,pause),"state").toString()=="paused","Family cancellation was not resumable");check(get(families.status(),"buildId")==previousBuild,"Partial family membership was published");
 families.reconcile(index);status=families.status();check(count(status,rockId)==5&&get(status,"families").getArray()->size()==1,"A close new import failed to expand the existing family or created a duplicate");
 const auto grownMembers=families.members(object({{"familyId",rockId}}));for(const auto& row:*get(grownMembers,"rows").getArray())check((int)get(row,"numerator")==4&&get(row,"patternId").toString()!=excluded,"Membership ignored meter or a persistent exclusion");
 const auto bossa=suggestion(families,"bossa nova"),jazz=suggestion(families,"jazz brushes"),funk=suggestion(families,"funk");
 check(get(suggestion(families,"four on floor"),"name").toString()=="Four On Floor","Naming discarded meaningful number words from a musical phrase");
 check((int)get(bossa,"patterns")==3&&number(bossa,"cohesion",0)>=.7,"Novel family lacks deduplicated support or cohesion");
 const auto proposed=families.suggestions(0,100);for(const auto& row:*get(proposed,"rows").getArray())check(!get(row,"query").toString().contains("theme")&&!get(row,"query").toString().contains("eighth")&&get(row,"query").toString()!="ht","Opaque identifiers invented a musical name");
 auto act=[&](juce::var row,const char* action,const juce::String& target={}){set(row,"buildId",get(families.status(),"buildId"));set(row,"revision",get(families.status(),"revision"));set(row,"action",action);if(target.isNotEmpty())set(row,"familyId",target);return families.decideSuggestion(row);};
 auto accepted=bossa.clone();set(accepted,"name","My Bossa");const auto acceptedFamily=get(act(accepted,"accept"),"family");const auto bossaId=get(acceptedFamily,"id").toString();check(get(acceptedFamily,"name").toString()=="My Bossa","Accepted family rename was lost");
 act(jazz,"merge",bossaId);act(funk,"dismiss");families.reconcile(index);
 check(count(families.status(),bossaId)>=6,"Merged query aliases do not include the full supported cohort");
 check(get(get(resolveNeuralPatch(catalog,index,base,object({{"query","Jazz Brushes"},{"indexId",get(index.status(),"readyIndexId")}})),"neural"),"familyId").toString()==bossaId,"A merged query created a separate resolved family");
 const auto any=families.registerFamily(object({{"query","Hard Rock"},{"numerator",4},{"denominator",4},{"feel","any"}}));const auto anyId=get(any,"id").toString();families.reconcile(index);
 check(count(families.status(),rockId,"ambiguous")>=5&&count(families.status(),anyId,"ambiguous")>=5,"Close competing families were silently assigned a winner");
 decision(families,rockId,excluded,"include");families.reconcile(index);bool manual=false;const auto manualMembers=families.members(object({{"familyId",rockId}}));for(const auto& row:*get(manualMembers,"rows").getArray())if(get(row,"patternId").toString()==excluded)manual=get(row,"state").toString()=="included";check(manual,"Manual inclusion did not resolve an ambiguous pattern");
 const auto bossaMembers=families.members(object({{"familyId",bossaId}}));juce::String includedBossa;for(const auto& row:*get(bossaMembers,"rows").getArray())if(get(row,"path").toString().contains("Bossa Nova")){includedBossa=get(row,"patternId").toString();break;}check(includedBossa.isNotEmpty(),"Bossa inclusion fixture missing");
 decision(families,rockId,includedBossa,"include");families.reconcile(index);
 const auto explicitInclusion=resolveNeuralPatch(catalog,index,base,object({{"query","Heavy Rock"},{"indexId",get(index.status(),"readyIndexId")}}));bool selectedBossa=false;for(const auto& clip:*get(explicitInclusion,"clips").getArray())selectedBossa=selectedBossa||get(clip,"id").toString()==includedBossa;check(selectedBossa,"Explicit inclusion outside top query groups was dropped by semantic filtering");
 // Mutate the registry while another instance holds the build lease. The
 // partial build is obsolete, and the previous ready pointer stays intact.
 const auto beforeRename=get(families.status(),"buildId");families.update(object({{"familyId",rockId},{"action","rename"},{"name","Before race"},{"revision",get(families.status(),"revision")}}));
 check(!(bool)get(families.status(),"stale")&&get(families.status(),"buildId")==beforeRename,"Display-only rename invalidated musical membership");
 decision(families,rockId,excluded,"exclude");
 const auto prior=get(families.status(),"buildId");bool raced=false;NeuralBuildOptions race;race.progress=[&](const auto& p){if(!raced&&get(p,"stage").toString()=="families"&&get(p,"state").toString()=="building"){raced=true;PatternFamilies other(db);check(get(other.reconcile(index),"state").toString()=="busy","Two instances acquired the same reconciliation lease");decision(other,rockId,includedBossa,"auto");}};
 check(get(families.reconcile(index,race),"state").toString()=="obsolete"&&get(families.status(),"buildId")==prior,"Concurrent registry edit published stale family membership");families.reconcile(index);
 PatternFamilies reopened(db);check(!(bool)get(reopened.status(),"stale"),"Reopened family registry lost its current snapshot");
 const auto remaining=reopened.suggestions(0,100);for(const auto& row:*get(remaining,"rows").getArray())check(get(row,"id")!=get(funk,"id"),"Dismissed suggestion reappeared");
 auto service=LibraryService::acquire(db);const int beforeGrowth=count(service->families->status(),rockId)+count(service->families->status(),rockId,"ambiguous");
 fixture.midi(added,"Hard Rock/Fills/Later.mid",170);service->start(added);
 for(int wait=0;wait<2000&&(bool)get(service->progress(),"running");++wait)juce::Thread::sleep(5);
 check(!(bool)get(service->progress(),"running")&&get(service->progress(),"state").toString()=="complete","Editor-free import/index/family worker did not finish");
 check(!(bool)get(service->families->status(),"stale")&&count(service->families->status(),rockId)+count(service->families->status(),rockId,"ambiguous")==beforeGrowth+1,"Import service did not automatically reconcile registered families");
 check(encode(initialResolved)==encode(resolveNeuralPatch(catalog,index,base,pinnedOptions)),"Committed family/index resolution changed after imports and display-name edits");
 const auto genericOptions=object({{"query","4/4"},{"indexId",get(index.status(),"readyIndexId")}});const auto generic=resolveNeuralPatch(catalog,index,base,genericOptions);families.registerFamily(get(generic,"neural"));families.reconcile(index);
 check(get(get(resolveNeuralPatch(catalog,index,base,genericOptions),"neural"),"familyBuildId").toString().isNotEmpty(),"A structural musical query cannot register and resolve its family");
 // Matching is incremental: a registration computes only the new family, and the
 // result equals a build from nothing. While it is pending, searches still resolve.
 {
  const auto readyBuild=get(families.status(),"buildId").toString();
  const auto added=families.registerFamily(object({{"query","Four on the Floor"},{"numerator",4},{"denominator",4},{"feel","straight"}}));const auto addedId=get(added,"id").toString();
  check((bool)get(families.status(),"stale")&&get(families.status(),"staleReason").toString()=="matching","A registration did not report pending matching");
  check(get(get(resolveNeuralPatch(catalog,index,base,genericOptions),"neural"),"familyBuildId").toString()==readyBuild,"Pending matching broke resolution of a matched family");
  const auto unmatched=resolveNeuralPatch(catalog,index,base,object({{"query","Four on the Floor"},{"indexId",get(index.status(),"readyIndexId")}}));check(get(get(unmatched,"neural"),"familyBuildId").toString().isEmpty()&&get(unmatched,"clips").getArray()->size()>0,"A family not matched yet did not resolve as a plain search");
  const auto incremental=families.reconcile(index);const int registered=get(families.status(),"families").getArray()->size();
  check(get(incremental,"state").toString()=="complete"&&(int)get(incremental,"computed")==1&&(int)get(incremental,"reused")==registered-1,"A registration rematched unchanged families");
  auto dump=[&]{sql::Database reader(db);juce::String text;const auto build=get(families.status(),"buildId").toString();
   for(const auto* query:{"SELECT family_id||pattern_id||binding_id||state,score FROM family_members WHERE build_id=? ORDER BY family_id,pattern_id","SELECT family_id||members||'/'||ambiguous,threshold FROM family_summaries WHERE build_id=? ORDER BY family_id","SELECT data,quality FROM family_suggestions WHERE build_id=? ORDER BY id","SELECT suggestion_id||pattern_id,0 FROM family_suggestion_members WHERE build_id=? ORDER BY suggestion_id,pattern_id"}){sql::Statement rows(reader,query);rows.text(1,build);while(rows.row())text<<rows.text(0)<<" "<<juce::String(rows.real(1),17)<<"\n";}
   return text;};
  const auto incrementalRows=dump();
  {sql::Database writer(db);const auto build=get(families.status(),"buildId").toString();for(const auto* table:{"family_groups","family_members","family_summaries","family_suggestions","family_suggestion_members","family_build_decisions"}){sql::Statement clear(writer,("DELETE FROM "+juce::String(table)+" WHERE build_id=?").toRawUTF8());clear.text(1,build);clear.done();}
   sql::Statement row(writer,"DELETE FROM family_builds WHERE id=?");row.text(1,build);row.done();writer.exec("DELETE FROM family_candidate_sets;DELETE FROM family_candidate_groups;DELETE FROM family_candidate_members;DELETE FROM family_cohorts;DELETE FROM family_cohort_patterns;DELETE FROM family_cohort_indexes");}
  const auto scratch=families.reconcile(index);check(get(scratch,"state").toString()=="complete"&&(int)get(scratch,"reused")==0,"Cleared matching caches were still used");
  check(dump()==incrementalRows&&incrementalRows.isNotEmpty(),"Incremental matching differs from a build from nothing");
  {sql::Database reader(db);sql::Statement dead(reader,"SELECT COUNT(*) FROM family_builds WHERE state!='complete'");dead.row();check(dead.integer(0)==0,"Abandoned family builds were kept");
   sql::Statement old(reader,"SELECT COUNT(*) FROM family_suggestions WHERE build_id!=(SELECT ready_build FROM family_meta WHERE id=1)");old.row();check(old.integer(0)==0,"Suggestions of superseded builds were kept");}
  // Saving keeps a family a search registered, without rematching; removal forgets it.
  const auto matchingBefore=get(families.status(),"matchingRevision");
  families.registerFamily(object({{"query","Four on the Floor"},{"numerator",4},{"denominator",4},{"feel","straight"},{"origin","saved"}}));
  bool saved=false;const auto kept=families.status();for(const auto& family:*get(kept,"families").getArray())if(get(family,"id").toString()==addedId)saved=get(family,"origin").toString()=="saved";
  check(saved&&get(families.status(),"matchingRevision")==matchingBefore&&!(bool)get(families.status(),"stale"),"Saving a registered family rematched it or lost its provenance");
  check((int)get(families.remove(object({{"revision",get(families.status(),"revision")},{"familyIds",juce::Array<juce::var>{addedId}}})),"removed")==1,"Family removal failed");
  const auto left=families.status();for(const auto& family:*get(left,"families").getArray())check(get(family,"id").toString()!=addedId,"A removed family remained registered");
  check((bool)get(families.status(),"stale"),"Removing a family did not rematch the others");check(get(families.reconcile(index),"state").toString()=="complete"&&!(bool)get(families.status(),"stale"),"Matching after a removal did not publish");
 }
 std::cout<<"families: pinned GPU affinities, identities, growth, deduplication, meter, manual decisions, immutable history, ambiguity, novel names, merge/dismiss, cancellation, concurrent publication, incremental matching, pending resolution, pruning, provenance and removal passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
