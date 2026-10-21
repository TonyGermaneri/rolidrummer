#include <drummer/PatternFamilies.h>
#include <drummer/VectorSearch.h>
#include <drummer/Json.h>
#include <juce_cryptography/juce_cryptography.h>
#include "LibrarySqlite.h"
#include "PartQuery.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>

namespace drummer {
using namespace json;
namespace {
constexpr double affinityFloor=.5,affinityWindow=.12,ambiguityMargin=.035,minimumCohesion=.70;
constexpr int minimumSupport=3;
juce::String hash(const juce::String& text){return juce::SHA256(text.toUTF8()).toHexString();}
juce::String encode(const juce::var& value){return juce::JSON::toString(value,true);}
int64_t epoch(sql::Database& db){sql::Statement row(db,"SELECT epoch FROM family_meta WHERE id=1");row.row();return row.integer(0);}
int64_t matchingEpoch(sql::Database& db){sql::Statement row(db,"SELECT matching_epoch FROM family_meta WHERE id=1");row.row();return row.integer(0);}
int64_t advance(sql::Database& db,bool musical=true){db.exec(musical?"UPDATE family_meta SET epoch=epoch+1,matching_epoch=matching_epoch+1 WHERE id=1":"UPDATE family_meta SET epoch=epoch+1 WHERE id=1");return epoch(db);}
void expected(sql::Database& db,const juce::var& operation){if(!operation.hasProperty("revision")||(int64_t)(juce::int64)get(operation,"revision")!=epoch(db))throw std::runtime_error("Another instance changed the families. Refresh and try again.");}
juce::String ready(sql::Database& db){sql::Statement row(db,"SELECT ready_build FROM family_meta WHERE id=1");row.row();return row.text(0);}
// Builds other than `keep` that never completed (obsolete, failed, paused or abandoned mid-build). Only complete
// builds are published or pinned, so nothing reads these rows; left alone they grow with every registry change.
void discard(sql::Database& db,const juce::String& keep){
 sql::Transaction transaction(db);
 for(const auto* table:{"family_groups","family_members","family_summaries","family_suggestions","family_suggestion_members","family_build_decisions"}){sql::Statement clear(db,("DELETE FROM "+juce::String(table)+" WHERE build_id IN(SELECT id FROM family_builds WHERE state!='complete' AND id!=?)").toRawUTF8());clear.text(1,keep);clear.done();}
 sql::Statement builds(db,"DELETE FROM family_builds WHERE state!='complete' AND id!=?");builds.text(1,keep);builds.done();transaction.commit();
}
juce::String latestIndex(sql::Database& db){sql::Statement row(db,"SELECT id FROM neural_indexes WHERE state='complete' AND preprocessing=? ORDER BY revision DESC,id LIMIT 1");row.integer(1,semanticDescriptionVersion);return row.row()?row.text(0):juce::String();}
juce::var definition(sql::Database& db,const juce::String& id){sql::Statement row(db,"SELECT definition,revision FROM pattern_families WHERE id=?");row.text(1,id);if(!row.row())throw std::runtime_error("This pattern family no longer exists");auto value=juce::JSON::parse(row.text(0));set(value,"revision",(juce::int64)row.integer(1));return value;}
juce::var insertFamily(sql::Database& db,const juce::var& value){
 const auto id=get(value,"id").toString();sql::Statement row(db,"SELECT id FROM pattern_families WHERE id=?");row.text(1,id);const bool exists=row.row();row.reset();
 if(!exists){const auto revision=advance(db);sql::Statement put(db,"INSERT INTO pattern_families VALUES(?,?,?)");put.text(1,id);put.text(2,encode(value));put.integer(3,revision);put.done();}
 return definition(db,id);
}
int describedFeel(const juce::String& text){const auto words=" "+text+" ";if(words.contains(" swing ")||words.contains(" shuffle ")||words.contains(" triplet "))return 1;if(words.contains(" straight "))return 0;return -1;}
int measuredFeel(const juce::var& features){if(number(features,"tripletFit",0)>.9&&number(features,"sixteenthFit",1)<.8)return 1;if(number(features,"sixteenthFit",0)>.9&&number(features,"tripletFit",1)<.8)return 0;return -1;}
juce::String styleName(const juce::String& description){
 static const std::set<juce::String> noise{"drum","patterns","pattern","groove","fill","intro","ending","sparse","relaxed","verse","chorus","bridge","hi","hat","hats","kick","snare","tom","toms","ride","crash","cymbal","percussion","eighth","sixteenth","thirty","second","quarter","note","notes","straight","swing","shuffle","triplet","half","time","double","closed","open","pedal","full","beat","beats","related","new","test","unknown","untitled","default","theme","variation","song","all"};
 juce::StringArray words;for(const auto& word:juce::StringArray::fromTokens(description," ",""))if(!noise.contains(word))words.add(word);
 // Descriptive style words are required. Opaque IDs were removed by the
 // semantic preprocessor; instrument/subdivision-only folders stay unnamed.
 static const std::set<juce::String> namingNoise{"variations","endings","intros","outros","loops","bonus","feel","combos","combo","oddgrooves","bjm"};
 for(int i=words.size()-1;i>=0;--i)if(namingNoise.contains(words[i]))words.remove(i);
 if(words.isEmpty()||words.size()>8)return {};
 static const std::set<juce::String> numberWords{"one","two","three","four","five","six","seven","eight","nine","ten","eleven","twelve","thirteen","fourteen","fifteen","sixteen"};
 if(std::all_of(words.begin(),words.end(),[](const auto& word){return numberWords.contains(word);}))return {};
 if(words.size()==1&&words[0].length()<3)return {};
 return words.joinIntoString(" ");
}
juce::String displayName(const juce::String& style){juce::StringArray words;for(const auto& word:juce::StringArray::fromTokens(style," ",""))words.add(word.substring(0,1).toUpperCase()+word.substring(1));return words.joinIntoString(" ");}
struct Lease {
 sql::Database& db;juce::String owner=juce::Uuid().toString();bool acquired=false;
 explicit Lease(sql::Database& connection):db(connection){try{sql::Statement row(db,"INSERT INTO family_lease VALUES(1,?,?) ON CONFLICT(id) DO UPDATE SET owner=excluded.owner,expires=excluded.expires WHERE family_lease.expires<?");const auto now=juce::Time::currentTimeMillis();row.text(1,owner);row.integer(2,now+600000);row.integer(3,now);row.done();acquired=sqlite3_changes(db.handle)>0;}catch(const std::exception&){if(sqlite3_errcode(db.handle)!=SQLITE_BUSY&&sqlite3_errcode(db.handle)!=SQLITE_LOCKED)throw;}}
 void heartbeat(){sql::Statement row(db,"UPDATE family_lease SET expires=? WHERE id=1 AND owner=?");row.integer(1,juce::Time::currentTimeMillis()+600000);row.text(2,owner);row.done();if(!sqlite3_changes(db.handle))throw std::runtime_error("Family reconciliation lease expired");}
 ~Lease(){if(acquired)try{sql::Statement row(db,"DELETE FROM family_lease WHERE owner=?");row.text(1,owner);row.done();}catch(...) {}}
};
struct SqlCancellation {
 sql::Database& db;const NeuralBuildOptions& options;
 SqlCancellation(sql::Database& connection,const NeuralBuildOptions& value):db(connection),options(value){sqlite3_progress_handler(db.handle,10000,[](void* context)noexcept{try{auto& self=*static_cast<SqlCancellation*>(context);return self.options.cancelled&&self.options.cancelled()?1:0;}catch(...){return 1;}},this);}
 void clear() noexcept{sqlite3_progress_handler(db.handle,0,nullptr,nullptr);}
 ~SqlCancellation(){clear();}
};
}

juce::var familyDefinition(const juce::var& options){
 const auto query=get(options,"query").toString().trim().substring(0,256);
 if(query.isEmpty())throw std::runtime_error("Enter a musical name for the family");
 int numerator=(int)number(options,"numerator",0),denominator=(int)number(options,"denominator",0);
 const auto text=query.toStdString();static const std::regex meter(R"(\b([1-9][0-9]?)\s*[/#]\s*(1|2|4|8|16|32)\b)");std::smatch match;
 if(std::regex_search(text,match,meter)){const int n=std::stoi(match[1]),d=std::stoi(match[2]);if(numerator>0&&(numerator!=n||denominator!=d))throw std::runtime_error("The family meter conflicts with its query");numerator=n;denominator=d;}
 if((numerator==0)!=(denominator==0)||numerator<0||numerator>64||(denominator!=0&&denominator!=1&&denominator!=2&&denominator!=4&&denominator!=8&&denominator!=16&&denominator!=32))throw std::runtime_error("Unsupported family meter");
 auto feel=get(options,"feel").toString();if(feel.isEmpty()){const auto inferred=describedFeel(semanticDescription(query));feel=inferred<0?"any":inferred?"triplet":"straight";}
 if(feel!="straight"&&feel!="triplet"&&feel!="any")throw std::runtime_error("Unsupported family feel");
 const auto canonical=musicalQueryDescription(query);
 const auto id="family-"+hash("family-1:query-"+juce::String(musicalQueryVersion)+":"+canonical+":"+juce::String(numerator)+"/"+juce::String(denominator)+":"+feel);
 auto name=get(options,"name",query).toString().trim().substring(0,128);if(name.isEmpty())throw std::runtime_error("The family name cannot be empty");
 auto value=object({{"version",1},{"id",id},{"name",name},{"query",query},{"canonicalQuery",canonical},{"queryVersion",musicalQueryVersion},{"numerator",numerator},{"denominator",denominator},{"feel",feel}});
 // Display-only provenance: a family kept by saving a searched groove.
 if(get(options,"origin").toString()=="saved")set(value,"origin","saved");
 return value;
}
juce::String familyIdentity(const juce::String& query,int numerator,int denominator,const juce::String& feel){return get(familyDefinition(object({{"query",query},{"numerator",numerator},{"denominator",denominator},{"feel",feel}})),"id").toString();}

PatternFamilies::PatternFamilies(juce::File file):database(std::move(file)){
 sql::Database db(database);if(!sql::schemaReady(db,"families")){db.exec(
 "CREATE TABLE IF NOT EXISTS family_meta(id INTEGER PRIMARY KEY,epoch INTEGER NOT NULL,ready_build TEXT NOT NULL);INSERT OR IGNORE INTO family_meta(id,epoch,ready_build) VALUES(1,0,'');"
 "CREATE TABLE IF NOT EXISTS pattern_families(id TEXT PRIMARY KEY,definition TEXT NOT NULL,revision INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS family_decisions(family_id TEXT NOT NULL,pattern_id TEXT NOT NULL,choice TEXT NOT NULL,PRIMARY KEY(family_id,pattern_id));"
 "CREATE TABLE IF NOT EXISTS family_build_decisions(build_id TEXT NOT NULL,family_id TEXT NOT NULL,pattern_id TEXT NOT NULL,choice TEXT NOT NULL,PRIMARY KEY(build_id,family_id,pattern_id));"
 "CREATE TABLE IF NOT EXISTS family_builds(id TEXT PRIMARY KEY,index_id TEXT NOT NULL,epoch INTEGER NOT NULL,policy INTEGER NOT NULL,state TEXT NOT NULL,model TEXT NOT NULL,error TEXT NOT NULL DEFAULT '');"
 "CREATE TABLE IF NOT EXISTS family_groups(build_id TEXT NOT NULL,family_id TEXT NOT NULL,semantic_id TEXT NOT NULL,score REAL NOT NULL,PRIMARY KEY(build_id,family_id,semantic_id));"
 "CREATE TABLE IF NOT EXISTS family_members(build_id TEXT NOT NULL,family_id TEXT NOT NULL,pattern_id TEXT NOT NULL,binding_id TEXT NOT NULL,score REAL NOT NULL,state TEXT NOT NULL,PRIMARY KEY(build_id,family_id,pattern_id));"
 "CREATE INDEX IF NOT EXISTS family_member_pattern ON family_members(build_id,pattern_id,state);"
 "CREATE INDEX IF NOT EXISTS family_member_overlap ON family_members(build_id,pattern_id,family_id);"
 "CREATE TABLE IF NOT EXISTS family_summaries(build_id TEXT NOT NULL,family_id TEXT NOT NULL,members INTEGER NOT NULL,ambiguous INTEGER NOT NULL,threshold REAL NOT NULL,PRIMARY KEY(build_id,family_id));"
 "CREATE TABLE IF NOT EXISTS family_rhythms(pattern_id TEXT PRIMARY KEY,feel INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS family_feature_indexes(index_id TEXT PRIMARY KEY,analysis_version INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS family_suggestions(build_id TEXT NOT NULL,id TEXT NOT NULL,data TEXT NOT NULL,support INTEGER NOT NULL DEFAULT 0,quality REAL NOT NULL DEFAULT 0,PRIMARY KEY(build_id,id));"
 "CREATE TABLE IF NOT EXISTS family_suggestion_members(build_id TEXT NOT NULL,suggestion_id TEXT NOT NULL,pattern_id TEXT NOT NULL,PRIMARY KEY(build_id,suggestion_id,pattern_id));"
 "CREATE TABLE IF NOT EXISTS family_suggestion_decisions(id TEXT PRIMARY KEY,choice TEXT NOT NULL,family_id TEXT NOT NULL);"
 "CREATE TABLE IF NOT EXISTS family_lease(id INTEGER PRIMARY KEY,owner TEXT NOT NULL,expires INTEGER NOT NULL);");
 sql::Transaction migration(db);bool matching=false;{sql::Statement columns(db,"PRAGMA table_info(family_meta)");while(columns.row())matching=matching||columns.text(1)=="matching_epoch";}if(!matching)db.exec("ALTER TABLE family_meta ADD COLUMN matching_epoch INTEGER NOT NULL DEFAULT 0;UPDATE family_meta SET matching_epoch=epoch");
 bool support=false,quality=false;{sql::Statement columns(db,"PRAGMA table_info(family_suggestions)");while(columns.row()){support=support||columns.text(1)=="support";quality=quality||columns.text(1)=="quality";}}
 if(!support)db.exec("ALTER TABLE family_suggestions ADD COLUMN support INTEGER NOT NULL DEFAULT 0");if(!quality)db.exec("ALTER TABLE family_suggestions ADD COLUMN quality REAL NOT NULL DEFAULT 0");
 // Earlier complete family builds already analyzed every indexed pattern.
 // Preserve that evidence so registry edits do not rescan the large catalog.
 db.exec("INSERT OR IGNORE INTO family_feature_indexes SELECT DISTINCT index_id,1 FROM family_builds WHERE state='complete'");sql::sealSchema(db,"families");migration.commit();}
 // Matching caches, a schema component of their own so instruments without them
 // still open the catalog. Every row is a pure function of its key: a family's
 // candidates of one index, definition and decisions; an index's name cohorts.
 if(sql::schemaReady(db,"family-cache"))return;sql::Transaction cache(db);db.exec(
 "CREATE TABLE IF NOT EXISTS family_candidate_sets(key TEXT PRIMARY KEY,index_id TEXT NOT NULL,threshold REAL NOT NULL);"
 "CREATE TABLE IF NOT EXISTS family_candidate_groups(key TEXT NOT NULL,semantic_id TEXT NOT NULL,score REAL NOT NULL,PRIMARY KEY(key,semantic_id));"
 "CREATE TABLE IF NOT EXISTS family_candidate_members(key TEXT NOT NULL,pattern_id TEXT NOT NULL,binding_id TEXT NOT NULL,score REAL NOT NULL,state TEXT NOT NULL,PRIMARY KEY(key,pattern_id));"
 "CREATE TABLE IF NOT EXISTS family_cohort_indexes(index_id TEXT PRIMARY KEY,version INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS family_cohorts(index_id TEXT NOT NULL,key TEXT NOT NULL,style TEXT NOT NULL,numerator INTEGER NOT NULL,denominator INTEGER NOT NULL,feel INTEGER NOT NULL,cohesion REAL NOT NULL,PRIMARY KEY(index_id,key));"
 "CREATE TABLE IF NOT EXISTS family_cohort_patterns(index_id TEXT NOT NULL,key TEXT NOT NULL,pattern_id TEXT NOT NULL,binding_id TEXT NOT NULL,PRIMARY KEY(index_id,key,pattern_id));");
 sql::sealSchema(db,"family-cache");cache.commit();
}
juce::var PatternFamilies::registerFamily(const juce::var& options) const{
 auto value=familyDefinition(options);sql::Database db(database);sql::Transaction transaction(db);auto result=insertFamily(db,value);
 // Saving keeps a family an earlier search registered: record it, without rematching.
 if(value.hasProperty("origin")&&!result.hasProperty("origin")){const auto id=get(value,"id").toString();set(result,"origin",get(value,"origin"));result.getDynamicObject()->removeProperty("revision");sql::Statement put(db,"UPDATE pattern_families SET definition=?,revision=? WHERE id=?");put.text(1,encode(result));put.integer(2,advance(db,false));put.text(3,id);put.done();result=definition(db,id);}
 transaction.commit();return result;
}
juce::var PatternFamilies::update(const juce::var& operation) const{
 sql::Database db(database);sql::Transaction transaction(db);expected(db,operation);
 const auto id=get(operation,"familyId").toString();auto value=definition(db,id);const auto action=get(operation,"action").toString();
 if(action=="rename"){
  const auto name=get(operation,"name").toString().trim().substring(0,128);if(name.isEmpty())throw std::runtime_error("The family name cannot be empty");set(value,"name",name);value.getDynamicObject()->removeProperty("revision");
  if(get(definition(db,id),"name").toString()!=name){sql::Statement put(db,"UPDATE pattern_families SET definition=?,revision=? WHERE id=?");put.text(1,encode(value));put.integer(2,advance(db,false));put.text(3,id);put.done();}
 }else if(action=="include"||action=="exclude"||action=="auto"){
  const auto pattern=get(operation,"patternId").toString();sql::Statement exists(db,"SELECT numerator,denominator,features FROM patterns WHERE id=?");exists.text(1,pattern);if(!exists.row())throw std::runtime_error("This pattern is not in the catalog");
  if(action=="include"){const int n=(int)get(value,"numerator"),d=(int)get(value,"denominator"),feel=measuredFeel(juce::JSON::parse(exists.text(2)));const auto wanted=get(value,"feel").toString();if((n>0&&(n!=exists.integer(0)||d!=exists.integer(1)))||(feel>=0&&wanted!="any"&&feel!=(wanted=="triplet"?1:0)))throw std::runtime_error("This pattern has an incompatible meter or feel for the family");}exists.reset();
  juce::String previous;{sql::Statement row(db,"SELECT choice FROM family_decisions WHERE family_id=? AND pattern_id=?");row.text(1,id);row.text(2,pattern);if(row.row())previous=row.text(0);}
  if((action=="auto"&&previous.isNotEmpty())||(action!="auto"&&previous!=action)){
   if(action=="auto"){sql::Statement remove(db,"DELETE FROM family_decisions WHERE family_id=? AND pattern_id=?");remove.text(1,id);remove.text(2,pattern);remove.done();}
   else{sql::Statement put(db,"INSERT INTO family_decisions VALUES(?,?,?) ON CONFLICT(family_id,pattern_id) DO UPDATE SET choice=excluded.choice");put.text(1,id);put.text(2,pattern);put.text(3,action);put.done();}advance(db);
  }
 }else throw std::runtime_error("Unknown family decision");
 const auto revision=epoch(db);transaction.commit();return object({{"revision",(juce::int64)revision},{"family",definition(db,id)}});
}
juce::var PatternFamilies::remove(const juce::var& operation) const{
 sql::Database db(database);sql::Transaction transaction(db);expected(db,operation);
 const auto ids=get(operation,"familyIds");if(!ids.isArray()||ids.getArray()->isEmpty())throw std::runtime_error("Choose the families to remove");
 int removed=0;
 for(const auto& value:*ids.getArray()){
  const auto id=value.toString();sql::Statement family(db,"DELETE FROM pattern_families WHERE id=?");family.text(1,id);family.done();if(!sqlite3_changes(db.handle))continue;++removed;
  sql::Statement decisions(db,"DELETE FROM family_decisions WHERE family_id=?");decisions.text(1,id);decisions.done();
  // A suggestion accepted as (or merged into) this family is offered again.
  sql::Statement suggestions(db,"DELETE FROM family_suggestion_decisions WHERE family_id=? AND choice!='dismiss'");suggestions.text(1,id);suggestions.done();
 }
 if(removed)advance(db);const auto revision=epoch(db);transaction.commit();return object({{"revision",(juce::int64)revision},{"removed",removed}});
}

juce::var PatternFamilies::reconcile(NeuralIndex& index,const NeuralBuildOptions& options) const{
 const double started=juce::Time::getMillisecondCounterHiRes();
 sql::Database db(database);Lease lease(db);if(!lease.acquired)return object({{"state","busy"},{"stage","families"}});
 const auto indexId=latestIndex(db);if(indexId.isEmpty())throw std::runtime_error("Build a neural index before reconciling families");
 const auto revision=matchingEpoch(db);const auto id=hash(indexId+":"+juce::String(revision)+":family-policy-"+juce::String(familyPolicyVersion));
 juce::String model;{sql::Statement row(db,"SELECT model FROM neural_indexes WHERE id=?");row.text(1,indexId);row.row();model=row.text(0);}
 std::vector<juce::var> families;{sql::Statement rows(db,"SELECT definition FROM pattern_families ORDER BY id");while(rows.row())families.push_back(juce::JSON::parse(rows.text(0)));}
 {sql::Transaction transaction(db);sql::Statement row(db,"INSERT OR IGNORE INTO family_builds(id,index_id,epoch,policy,state,model) VALUES(?,?,?,?,'building',?)");row.text(1,id);row.text(2,indexId);row.integer(3,revision);row.integer(4,familyPolicyVersion);row.text(5,model);row.done();transaction.commit();}
 int computed=0,reused=0;
 auto publish=[&](const char* state,const char* stage,int64_t completed=0){auto result=object({{"state",state},{"stage",stage},{"buildId",id},{"indexId",indexId},{"revision",(juce::int64)revision},{"completed",(juce::int64)completed},{"groups",(int)families.size()},{"computed",computed},{"reused",reused},{"milliseconds",juce::Time::getMillisecondCounterHiRes()-started}});if(options.progress)options.progress(result);return result;};
 auto stop=[&]{if(options.cancelled&&options.cancelled()){sql::Statement row(db,"UPDATE family_builds SET state='paused' WHERE id=? AND state!='complete'");row.text(1,id);row.done();return true;}return false;};
 // A registry edit or a newer index makes this build useless: stop at once
 // rather than finishing work nothing will publish.
 auto obsolete=[&]{if(matchingEpoch(db)==revision&&latestIndex(db)==indexId)return false;sql::Statement row(db,"UPDATE family_builds SET state='obsolete' WHERE id=?");row.text(1,id);row.done();return true;};
 {sql::Statement row(db,"SELECT state FROM family_builds WHERE id=?");row.text(1,id);row.row();if(row.text(0)=="complete")return publish("complete","families");}
 discard(db,id);
 SqlCancellation cancellation(db,options);
 try{
  // Cache immutable musical feel once per normalized performance. This is
  // resumable and avoids decoding every pattern's features on each import.
  int64_t analysed=0;bool featuresReady=false;{sql::Statement known(db,"SELECT index_id FROM family_feature_indexes WHERE index_id=? AND analysis_version=1");known.text(1,indexId);featuresReady=known.row();}
  if(!featuresReady){
   db.exec("CREATE TEMP TABLE family_pending(pattern_id TEXT PRIMARY KEY)");
   sql::Statement pending(db,"INSERT OR IGNORE INTO family_pending SELECT b.pattern_id FROM neural_sources s JOIN bindings b INDEXED BY binding_run ON b.run_id=s.run_id LEFT JOIN family_rhythms r ON r.pattern_id=b.pattern_id WHERE s.index_id=? AND r.pattern_id IS NULL");pending.text(1,indexId);pending.done();
  }
  while(!featuresReady){if(stop())return publish("paused","family-features",analysed);
   std::vector<std::pair<juce::String,int>> batch;
   {sql::Statement rows(db,"SELECT pattern_id,(SELECT features FROM patterns WHERE id=family_pending.pattern_id) FROM family_pending ORDER BY pattern_id LIMIT 256");while(rows.row())batch.emplace_back(rows.text(0),measuredFeel(juce::JSON::parse(rows.text(1))));}
   if(batch.empty()){sql::Statement done(db,"INSERT OR REPLACE INTO family_feature_indexes VALUES(?,1)");done.text(1,indexId);done.done();break;}
   sql::Transaction transaction(db);sql::Statement put(db,"INSERT OR IGNORE INTO family_rhythms VALUES(?,?)");for(const auto& [pattern,feel]:batch){put.reset();put.text(1,pattern);put.integer(2,feel);put.done();}sql::Statement consumed(db,"DELETE FROM family_pending WHERE pattern_id<=?");consumed.text(1,batch.back().first);consumed.done();lease.heartbeat();transaction.commit();analysed+=(int64_t)batch.size();publish("building","family-features",analysed);
  }
  {sql::Transaction transaction(db);for(const auto* table:{"family_groups","family_members","family_summaries","family_suggestions","family_suggestion_members","family_build_decisions"}){sql::Statement clear(db,("DELETE FROM "+juce::String(table)+" WHERE build_id=?").toRawUTF8());clear.text(1,id);clear.done();}sql::Statement decisions(db,"INSERT INTO family_build_decisions SELECT ?,family_id,pattern_id,choice FROM family_decisions");decisions.text(1,id);decisions.done();sql::Statement state(db,"UPDATE family_builds SET state='building',error='' WHERE id=?");state.text(1,id);state.done();transaction.commit();}
  publish("building","families");int completed=0;std::set<juce::String> used;std::vector<std::pair<juce::String,juce::String>> keys;
  for(const auto& family:families){
   if(stop())return publish("paused","families",completed);
   if(obsolete())return publish("obsolete","families",completed);
   const auto familyId=get(family,"id").toString();const int n=(int)get(family,"numerator"),d=(int)get(family,"denominator");const auto feel=get(family,"feel").toString();const int wantedFeel=feel=="any"?-1:feel=="triplet"?1:0;
   // A family's candidates depend only on the index, its musical definition and
   // its own decisions, so a registry edit recomputes just the families it touched.
   juce::String decided;{sql::Statement rows(db,"SELECT pattern_id,choice FROM family_build_decisions WHERE build_id=? AND family_id=? ORDER BY pattern_id");rows.text(1,id);rows.text(2,familyId);while(rows.row())decided+=rows.text(0)+"="+rows.text(1)+";";}
   const auto key=hash("family-candidates-1:"+indexId+":"+juce::String(familyPolicyVersion)+":"+juce::String(semanticDescriptionVersion)+":"+encode(object({{"query",get(family,"query")},{"queries",get(family,"queries")},{"canonicalQuery",get(family,"canonicalQuery")},{"queryVersion",get(family,"queryVersion")},{"numerator",n},{"denominator",d},{"feel",feel}}))+":"+decided);
   used.insert(key);keys.emplace_back(familyId,key);
   double threshold=1;bool cached=false;{sql::Statement row(db,"SELECT threshold FROM family_candidate_sets WHERE key=?");row.text(1,key);if(row.row()){threshold=row.real(0);cached=true;}}
   std::map<juce::String,SemanticAffinity> combined;
   if(!cached){juce::Array<juce::var> queries{get(family,"query")};if(auto aliases=get(family,"queries");aliases.isArray())for(const auto& alias:*aliases.getArray())queries.add(alias);
    for(const auto& query:queries){const auto scores=index.affinities(query.toString(),indexId);const double cutoff=std::max(affinityFloor,scores.groups.empty()?1.:scores.groups[0].score-affinityWindow);threshold=std::min(threshold,cutoff);for(const auto& score:scores.groups)if(score.score>=cutoff){auto found=combined.find(score.id);if(found==combined.end()||found->second.score<score.score)combined.insert_or_assign(score.id,score);}}}
   if(!cached){
    sql::Transaction transaction(db);
    sql::Statement group(db,"INSERT INTO family_candidate_groups VALUES(?,?,?)");
    for(const auto& [semantic,score]:combined)if((score.description!="drum patterns"||get(family,"canonicalQuery").toString()=="drum patterns")&&(wantedFeel<0||describedFeel(score.description)<0||describedFeel(score.description)==wantedFeel)){group.reset();group.text(1,key);group.text(2,score.id);group.real(3,score.score);group.done();}
    sql::Statement members(db,"INSERT INTO family_candidate_members SELECT ?,p.id,MIN(b.id),MAX(g.score),'candidate' FROM family_candidate_groups g JOIN neural_descriptions nd ON nd.semantic_id=g.semantic_id AND nd.version=? JOIN bindings b INDEXED BY binding_description ON b.description_id=nd.description_id JOIN neural_sources s ON s.run_id=b.run_id AND s.index_id=? JOIN patterns p ON p.id=b.pattern_id JOIN family_rhythms r ON r.pattern_id=p.id WHERE g.key=? AND p.coverage>=.6 AND (?=0 OR p.numerator=?) AND (?=0 OR p.denominator=?) AND (?<0 OR r.feel<0 OR r.feel=?) GROUP BY p.id");
    members.text(1,key);members.integer(2,semanticDescriptionVersion);members.text(3,indexId);members.text(4,key);members.integer(5,n);members.integer(6,n);members.integer(7,d);members.integer(8,d);members.integer(9,wantedFeel);members.integer(10,wantedFeel);members.done();
    sql::Statement exclude(db,"DELETE FROM family_candidate_members WHERE key=? AND pattern_id IN(SELECT pattern_id FROM family_build_decisions WHERE build_id=? AND family_id=? AND choice='exclude')");exclude.text(1,key);exclude.text(2,id);exclude.text(3,familyId);exclude.done();
    sql::Statement include(db,"INSERT OR REPLACE INTO family_candidate_members SELECT ?,p.id,MIN(b.id),1.,'included' FROM family_build_decisions decisions JOIN patterns p ON p.id=decisions.pattern_id JOIN bindings b INDEXED BY binding_pattern ON b.pattern_id=p.id JOIN neural_sources s ON s.run_id=b.run_id AND s.index_id=? JOIN family_rhythms r ON r.pattern_id=p.id WHERE decisions.build_id=? AND decisions.family_id=? AND decisions.choice='include' AND (?=0 OR p.numerator=?) AND (?=0 OR p.denominator=?) AND (?<0 OR r.feel<0 OR r.feel=?) GROUP BY p.id");
    include.text(1,key);include.text(2,indexId);include.text(3,id);include.text(4,familyId);include.integer(5,n);include.integer(6,n);include.integer(7,d);include.integer(8,d);include.integer(9,wantedFeel);include.integer(10,wantedFeel);include.done();
    sql::Statement record(db,"INSERT INTO family_candidate_sets VALUES(?,?,?)");record.text(1,key);record.text(2,indexId);record.real(3,threshold);record.done();lease.heartbeat();transaction.commit();++computed;
   }else ++reused;
   publish("building","families",++completed);
  }
  // Then every family's candidates into this build, in one transaction.
  {sql::Transaction transaction(db);sql::Statement groups(db,"INSERT INTO family_groups SELECT ?,?,semantic_id,score FROM family_candidate_groups WHERE key=?"),members(db,"INSERT INTO family_members SELECT ?,?,pattern_id,binding_id,score,state FROM family_candidate_members WHERE key=?"),summary(db,"INSERT INTO family_summaries SELECT ?,?,0,0,threshold FROM family_candidate_sets WHERE key=?");
   for(const auto& [familyId,key]:keys)for(auto* statement:{&groups,&members,&summary}){statement->reset();statement->text(1,id);statement->text(2,familyId);statement->text(3,key);statement->done();}
   lease.heartbeat();transaction.commit();}
  {
   sql::Transaction transaction(db);
   db.exec("CREATE TEMP TABLE family_best(pattern_id TEXT PRIMARY KEY,score REAL NOT NULL)");sql::Statement best(db,"INSERT INTO family_best SELECT pattern_id,MAX(score) FROM family_members WHERE build_id=? AND state='candidate' GROUP BY pattern_id");best.text(1,id);best.done();
   sql::Statement remove(db,"DELETE FROM family_members WHERE build_id=? AND state='candidate' AND score<(SELECT score FROM family_best WHERE family_best.pattern_id=family_members.pattern_id)-?");remove.text(1,id);remove.real(2,ambiguityMargin);remove.done();
   sql::Statement mark(db,"UPDATE family_members AS m SET state=CASE WHEN (SELECT COUNT(*) FROM family_members alternatives WHERE alternatives.build_id=m.build_id AND alternatives.pattern_id=m.pattern_id AND alternatives.state IN('candidate','matched','ambiguous'))>1 THEN 'ambiguous' ELSE 'matched' END WHERE build_id=? AND state='candidate'");mark.text(1,id);mark.done();
   sql::Statement summary(db,"UPDATE family_summaries AS s SET members=(SELECT COUNT(*) FROM family_members m WHERE m.build_id=s.build_id AND m.family_id=s.family_id AND m.state!='ambiguous'),ambiguous=(SELECT COUNT(*) FROM family_members m WHERE m.build_id=s.build_id AND m.family_id=s.family_id AND m.state='ambiguous') WHERE build_id=?");summary.text(1,id);summary.done();transaction.commit();
  }
  if(obsolete())return publish("obsolete","families",completed);
  publish("building","family-suggestions");
  // Name cohorts (style, meter and feel, with their patterns and cohesion) depend
  // on the index alone. Scan the catalog for them once per index; each build then
  // only counts which of their patterns its families already cover.
  bool cohortsReady=false;{sql::Statement known(db,"SELECT index_id FROM family_cohort_indexes WHERE index_id=? AND version=1");known.text(1,indexId);cohortsReady=known.row();}
  if(!cohortsReady){
   struct Group{juce::String style,description;TextVector vector;int feel=-1;};std::map<juce::String,Group> groups;
   {sql::Statement rows(db,"SELECT m.semantic_id,t.text,v.vector FROM neural_members m JOIN neural_text t ON t.id=m.semantic_id JOIN neural_vectors v ON v.semantic_id=m.semantic_id AND v.model=? WHERE m.index_id=? ORDER BY m.semantic_id");rows.text(1,model);rows.text(2,indexId);while(rows.row()){const auto bytes=rows.blob(2);if(bytes.getSize()!=sizeof(TextVector))throw std::runtime_error("Invalid family text vector");Group group;group.description=rows.text(1);group.style=styleName(group.description);group.feel=describedFeel(group.description);std::memcpy(group.vector.data(),bytes.getData(),sizeof(TextVector));groups.emplace(rows.text(0),std::move(group));}}
   // A pattern's first binding (lowest description) stands for it among the examples.
   struct Cohort{juce::String style;int numerator=4,denominator=4,feel=-1;std::map<juce::String,juce::String> patterns;std::set<juce::String> groups;};std::map<juce::String,Cohort> cohorts;
   std::map<juce::String,juce::String> namingContexts;
   {
    sql::Statement rows(db,"SELECT p.id,nd.semantic_id,p.numerator,p.denominator,r.feel,MIN(b.id),MIN(b.family) FROM neural_sources s JOIN bindings b ON b.run_id=s.run_id JOIN neural_descriptions nd ON nd.description_id=b.description_id AND nd.version=? JOIN patterns p ON p.id=b.pattern_id JOIN family_rhythms r ON r.pattern_id=p.id WHERE s.index_id=? AND p.coverage>=.6 GROUP BY p.id,nd.semantic_id ORDER BY p.id,nd.semantic_id");
    rows.integer(1,semanticDescriptionVersion);rows.text(2,indexId);int scanned=0;
    while(rows.row()){
     if((++scanned%1024)==0){if(stop())return publish("paused","family-suggestions",scanned);lease.heartbeat();publish("building","family-suggestions",scanned);}
     const auto semantic=rows.text(1);const auto found=groups.find(semantic);if(found==groups.end())continue;const auto& group=found->second;
     // Directory context avoids turning a filename's hand/instrument/export
     // suffix into a style. Fall back to the filename when folders are generic.
     const auto context=rows.text(6);auto [cached,inserted]=namingContexts.try_emplace(context);
     if(inserted)cached->second=styleName(semanticDescription(context.replace("drumkit from hell","").replace("drum kit from hell","")));
     auto style=cached->second;if(style.isEmpty())style=group.style;if(style.isEmpty())continue;
     if(group.feel>=0&&rows.integer(4)>=0&&group.feel!=rows.integer(4))continue;
     const int n=(int)rows.integer(2),d=(int)rows.integer(3),feel=group.feel<0?(int)rows.integer(4):group.feel;
     const auto key=style+":"+juce::String(n)+"/"+juce::String(d)+":"+juce::String(feel);auto& cohort=cohorts[key];cohort.style=style;cohort.numerator=n;cohort.denominator=d;cohort.feel=feel;cohort.patterns.emplace(rows.text(0),rows.text(5));cohort.groups.insert(semantic);
    }
   }
   if(stop())return publish("paused","family-suggestions");
   sql::Transaction transaction(db);
   for(const auto* table:{"family_cohorts","family_cohort_patterns"}){sql::Statement clear(db,("DELETE FROM "+juce::String(table)+" WHERE index_id=?").toRawUTF8());clear.text(1,indexId);clear.done();}
   sql::Statement put(db,"INSERT INTO family_cohorts VALUES(?,?,?,?,?,?,?)"),member(db,"INSERT INTO family_cohort_patterns VALUES(?,?,?,?)");
   for(const auto& [key,cohort]:cohorts){
    // Cohorts that could never be suggested, whatever the families cover.
    if(cohort.patterns.size()<minimumSupport)continue;
    TextVector centroid{};for(const auto& semantic:cohort.groups)for(size_t i=0;i<centroid.size();++i)centroid[i]+=groups.at(semantic).vector[i]/(float)cohort.groups.size();
    double norm=0;for(float v:centroid)norm+=v*v;if(norm<1e-12)continue;for(float& v:centroid)v=(float)(v/std::sqrt(norm));
    double cohesion=1;for(const auto& semantic:cohort.groups)cohesion=std::min(cohesion,VectorSearch::similarity(centroid,groups.at(semantic).vector));if(cohesion<minimumCohesion)continue;
    put.reset();put.text(1,indexId);put.text(2,key);put.text(3,cohort.style);put.integer(4,cohort.numerator);put.integer(5,cohort.denominator);put.integer(6,cohort.feel);put.real(7,cohesion);put.done();
    for(const auto& [pattern,binding]:cohort.patterns){member.reset();member.text(1,indexId);member.text(2,key);member.text(3,pattern);member.text(4,binding);member.done();}
   }
   sql::Statement done(db,"INSERT OR REPLACE INTO family_cohort_indexes VALUES(?,1)");done.text(1,indexId);done.done();lease.heartbeat();transaction.commit();
  }
  struct Candidate{juce::String key,style;int numerator,denominator,feel;double cohesion;int64_t patterns,covered;};std::vector<Candidate> candidates;
  {sql::Statement rows(db,"SELECT c.key,c.style,c.numerator,c.denominator,c.feel,c.cohesion,COUNT(*),SUM(EXISTS(SELECT 1 FROM family_members m WHERE m.build_id=? AND m.pattern_id=cp.pattern_id)) FROM family_cohorts c JOIN family_cohort_patterns cp ON cp.index_id=c.index_id AND cp.key=c.key WHERE c.index_id=? GROUP BY c.key ORDER BY c.key");rows.text(1,id);rows.text(2,indexId);
   while(rows.row())candidates.push_back({rows.text(0),rows.text(1),(int)rows.integer(2),(int)rows.integer(3),(int)rows.integer(4),rows.real(5),rows.integer(6),rows.integer(7)});}
  for(const auto& cohort:candidates){
   if(stop())return publish("paused","family-suggestions");const auto uncovered=cohort.patterns-cohort.covered;if(uncovered<minimumSupport||uncovered*2<cohort.patterns)continue;
   const auto suggestionId="suggestion-"+hash("family-suggestion-1:"+cohort.key);juce::Array<juce::var> examples;
   std::vector<std::pair<juce::String,juce::String>> chosen;{sql::Statement rows(db,"SELECT cp.pattern_id,cp.binding_id FROM family_cohort_patterns cp WHERE cp.index_id=? AND cp.key=? AND NOT EXISTS(SELECT 1 FROM family_members m WHERE m.build_id=? AND m.pattern_id=cp.pattern_id) ORDER BY cp.pattern_id LIMIT 3");rows.text(1,indexId);rows.text(2,cohort.key);rows.text(3,id);while(rows.row())chosen.emplace_back(rows.text(0),rows.text(1));}
   for(const auto& [pattern,binding]:chosen){sql::Statement row(db,"SELECT e.path FROM bindings b JOIN entries e ON e.run_id=b.run_id AND e.ordinal=b.ordinal WHERE b.id=?");row.text(1,binding);if(row.row())examples.add(object({{"patternId",pattern},{"bindingId",binding},{"path",row.text(0)}}));}
   const auto feel=cohort.feel<0?"any":cohort.feel?"triplet":"straight";
   juce::Array<juce::var> overlap;if(cohort.covered){sql::Statement rows(db,"SELECT m.family_id,COUNT(DISTINCT cp.pattern_id) FROM family_cohort_patterns cp JOIN family_members m ON m.build_id=? AND m.pattern_id=cp.pattern_id WHERE cp.index_id=? AND cp.key=? GROUP BY m.family_id ORDER BY m.family_id");rows.text(1,id);rows.text(2,indexId);rows.text(3,cohort.key);while(rows.row())overlap.add(object({{"familyId",rows.text(0)},{"patterns",(juce::int64)rows.integer(1)}}));}
   const auto quality=std::round(cohort.cohesion*1e7)/1e7;
   auto suggestion=object({{"id",suggestionId},{"name",displayName(cohort.style)},{"query",cohort.style},{"numerator",cohort.numerator},{"denominator",cohort.denominator},{"feel",feel},{"patterns",(juce::int64)uncovered},{"supportedPatterns",(juce::int64)cohort.patterns},{"overlap",overlap},{"cohesion",quality},{"examples",examples}});
   sql::Transaction transaction(db);sql::Statement put(db,"INSERT INTO family_suggestions(build_id,id,data,support,quality) VALUES(?,?,?,?,?)");put.text(1,id);put.text(2,suggestionId);put.text(3,encode(suggestion));put.integer(4,uncovered);put.real(5,quality);put.done();
   sql::Statement member(db,"INSERT OR IGNORE INTO family_suggestion_members SELECT ?,?,pattern_id FROM family_cohort_patterns WHERE index_id=? AND key=?");member.text(1,id);member.text(2,suggestionId);member.text(3,indexId);member.text(4,cohort.key);member.done();transaction.commit();
  }
  {
   sql::Transaction transaction(db);lease.heartbeat();const bool current=matchingEpoch(db)==revision&&latestIndex(db)==indexId;
   sql::Statement state(db,"UPDATE family_builds SET state=? WHERE id=?");state.text(1,current?"complete":"obsolete");state.text(2,id);state.done();
   if(current){
    sql::Statement point(db,"UPDATE family_meta SET ready_build=? WHERE id=1");point.text(1,id);point.done();
    // Suggestions are only read from the ready build; older complete builds keep
    // their membership for pinned recall. Caches this build did not use are spent.
    for(const auto* table:{"family_suggestions","family_suggestion_members"}){sql::Statement clear(db,("DELETE FROM "+juce::String(table)+" WHERE build_id!=?").toRawUTF8());clear.text(1,id);clear.done();}
    db.exec("CREATE TEMP TABLE family_used(key TEXT PRIMARY KEY)");sql::Statement keep(db,"INSERT INTO family_used VALUES(?)");for(const auto& key:used){keep.reset();keep.text(1,key);keep.done();}
    db.exec("DELETE FROM family_candidate_groups WHERE key NOT IN(SELECT key FROM family_used);DELETE FROM family_candidate_members WHERE key NOT IN(SELECT key FROM family_used);DELETE FROM family_candidate_sets WHERE key NOT IN(SELECT key FROM family_used)");
    for(const auto* table:{"family_cohorts","family_cohort_patterns","family_cohort_indexes"}){sql::Statement clear(db,("DELETE FROM "+juce::String(table)+" WHERE index_id!=?").toRawUTF8());clear.text(1,indexId);clear.done();}
   }
   transaction.commit();return publish(current?"complete":"obsolete","families",completed);
  }
 }catch(const std::exception& error){cancellation.clear();if(options.cancelled&&options.cancelled()){sql::Statement state(db,"UPDATE family_builds SET state='paused' WHERE id=?");state.text(1,id);state.done();return publish("paused","families");}sql::Statement state(db,"UPDATE family_builds SET state='failed',error=? WHERE id=?");state.text(1,error.what());state.text(2,id);state.done();publish("failed","families");throw;}
}

juce::var PatternFamilies::status() const{
 sql::Database db(database);db.exec("BEGIN");const auto build=ready(db);const auto revision=epoch(db);juce::Array<juce::var> families;
 sql::Statement rows(db,"SELECT f.definition,f.revision,COALESCE(s.members,0),COALESCE(s.ambiguous,0),COALESCE(s.threshold,0),(SELECT COUNT(*) FROM family_decisions d WHERE d.family_id=f.id),EXISTS(SELECT 1 FROM family_suggestion_decisions sd WHERE sd.family_id=f.id AND sd.choice!='dismiss') FROM pattern_families f LEFT JOIN family_summaries s ON s.family_id=f.id AND s.build_id=? ORDER BY f.id");rows.text(1,build);
 while(rows.row()){auto value=juce::JSON::parse(rows.text(0));set(value,"revision",(juce::int64)rows.integer(1));set(value,"members",(juce::int64)rows.integer(2));set(value,"ambiguous",(juce::int64)rows.integer(3));set(value,"threshold",rows.real(4));set(value,"decisions",(juce::int64)rows.integer(5));set(value,"suggested",rows.integer(6)!=0);families.add(value);}
 auto result=object({{"revision",(juce::int64)revision},{"matchingRevision",(juce::int64)matchingEpoch(db)},{"buildId",build},{"policyVersion",familyPolicyVersion},{"families",families},{"state","missing"},{"stale",true},{"staleReason","missing"}});
 // Why membership is behind: "index" and "matching" wait for family matching;
 // "search" waits for the semantic index to catch up with the catalog first.
 {sql::Statement row(db,"SELECT b.index_id,b.epoch,b.state,b.policy,i.revision,(SELECT value FROM library_meta WHERE key='revision') FROM family_builds b JOIN neural_indexes i ON i.id=b.index_id WHERE b.id=?");row.text(1,build);if(row.row()){set(result,"indexId",row.text(0));set(result,"state",row.text(2));const bool index=row.text(0)!=latestIndex(db),matching=row.integer(1)!=matchingEpoch(db)||row.integer(3)!=familyPolicyVersion,search=row.integer(4)!=row.integer(5);set(result,"stale",index||matching||search);set(result,"staleReason",index?"index":matching?"matching":search?"search":"");}}
 sql::Statement suggestions(db,"SELECT COUNT(*) FROM family_suggestions s LEFT JOIN family_suggestion_decisions d ON d.id=s.id WHERE s.build_id=? AND d.id IS NULL");suggestions.text(1,build);suggestions.row();set(result,"suggestions",(juce::int64)suggestions.integer(0));db.exec("COMMIT");return result;
}
juce::var PatternFamilies::members(const juce::var& options) const{
 sql::Database db(database);const auto build=get(options,"buildId",ready(db)).toString(),family=get(options,"familyId").toString();const int offset=std::max(0,(int)number(options,"offset",0)),limit=std::clamp((int)number(options,"limit",50),1,100);
 sql::Statement exists(db,"SELECT id FROM family_builds WHERE id=? AND state='complete'");exists.text(1,build);if(!exists.row())throw std::runtime_error("Family membership is not ready");exists.reset();
 sql::Statement rows(db,"SELECT m.binding_id,m.pattern_id,m.score,m.state,e.path,p.numerator,p.denominator,b.role,COALESCE(d.choice,'auto') FROM family_members m JOIN bindings b ON b.id=m.binding_id JOIN entries e ON e.run_id=b.run_id AND e.ordinal=b.ordinal JOIN patterns p ON p.id=m.pattern_id LEFT JOIN family_build_decisions d ON d.build_id=m.build_id AND d.family_id=m.family_id AND d.pattern_id=m.pattern_id WHERE m.build_id=? AND m.family_id=? ORDER BY m.score DESC,m.pattern_id LIMIT ? OFFSET ?");rows.text(1,build);rows.text(2,family);rows.integer(3,limit+1);rows.integer(4,offset);juce::Array<juce::var> result;
 while(rows.row()){
  auto value=object({{"bindingId",rows.text(0)},{"patternId",rows.text(1)},{"score",rows.real(2)},{"state",rows.text(3)},{"path",rows.text(4)},{"numerator",(int)rows.integer(5)},{"denominator",(int)rows.integer(6)},{"role",rows.text(7)},{"decision",rows.text(8)}});
  juce::Array<juce::var> alternatives;sql::Statement other(db,"SELECT family_id,score,state FROM family_members INDEXED BY family_member_overlap WHERE build_id=? AND pattern_id=? AND family_id!=? ORDER BY score DESC,family_id LIMIT 17");other.text(1,build);other.text(2,rows.text(1));other.text(3,family);while(other.row())alternatives.add(object({{"familyId",other.text(0)},{"score",other.real(1)},{"state",other.text(2)}}));const bool moreAlternatives=alternatives.size()>16;if(moreAlternatives)alternatives.removeLast();set(value,"alternatives",alternatives);set(value,"moreAlternatives",moreAlternatives);result.add(value);
 }const bool more=result.size()>limit;if(more)result.removeLast();return object({{"buildId",build},{"familyId",family},{"offset",offset},{"more",more},{"rows",result}});
}
juce::var PatternFamilies::suggestions(int offset,int limit) const{
 sql::Database db(database);const auto build=ready(db);offset=std::max(0,offset);limit=std::clamp(limit,1,100);sql::Statement rows(db,"SELECT s.data FROM family_suggestions s LEFT JOIN family_suggestion_decisions d ON d.id=s.id WHERE s.build_id=? AND d.id IS NULL ORDER BY s.support DESC,s.quality DESC,s.id LIMIT ? OFFSET ?");rows.text(1,build);rows.integer(2,limit+1);rows.integer(3,offset);juce::Array<juce::var> result;while(rows.row())result.add(juce::JSON::parse(rows.text(0)));const bool more=result.size()>limit;if(more)result.removeLast();return object({{"buildId",build},{"revision",(juce::int64)epoch(db)},{"offset",offset},{"more",more},{"rows",result}});
}
juce::var PatternFamilies::decisions(const juce::var& options) const{
 sql::Database db(database);db.exec("BEGIN");const auto family=get(options,"familyId").toString(),index=latestIndex(db);const int offset=std::max(0,(int)number(options,"offset",0)),limit=std::clamp((int)number(options,"limit",25),1,100);
 sql::Statement rows(db,"WITH page AS(SELECT pattern_id,choice FROM family_decisions WHERE family_id=? ORDER BY pattern_id LIMIT ? OFFSET ?),selected AS(SELECT page.*,COALESCE((SELECT MIN(b.id) FROM bindings b INDEXED BY binding_pattern JOIN neural_sources s ON s.run_id=b.run_id AND s.index_id=? WHERE b.pattern_id=page.pattern_id),(SELECT MIN(b.id) FROM bindings b INDEXED BY binding_pattern WHERE b.pattern_id=page.pattern_id)) AS binding_id FROM page) SELECT selected.pattern_id,selected.choice,selected.binding_id,e.path,p.numerator,p.denominator FROM selected JOIN patterns p ON p.id=selected.pattern_id LEFT JOIN bindings b ON b.id=selected.binding_id LEFT JOIN entries e ON e.run_id=b.run_id AND e.ordinal=b.ordinal ORDER BY selected.pattern_id");rows.text(1,family);rows.integer(2,limit+1);rows.integer(3,offset);rows.text(4,index);juce::Array<juce::var> result;
 while(rows.row())result.add(object({{"patternId",rows.text(0)},{"decision",rows.text(1)},{"bindingId",rows.text(2)},{"path",rows.text(3)},{"numerator",(int)rows.integer(4)},{"denominator",(int)rows.integer(5)}}));const bool more=result.size()>limit;if(more)result.removeLast();const auto revision=epoch(db);db.exec("COMMIT");return object({{"revision",(juce::int64)revision},{"familyId",family},{"offset",offset},{"more",more},{"rows",result}});
}
juce::var PatternFamilies::decideSuggestion(const juce::var& options) const{
 sql::Database db(database);sql::Transaction transaction(db);expected(db,options);const auto build=get(options,"buildId").toString(),id=get(options,"id").toString(),action=get(options,"action").toString();
 if(build!=ready(db))throw std::runtime_error("The suggestions changed. Refresh and try again.");sql::Statement row(db,"SELECT data FROM family_suggestions WHERE build_id=? AND id=?");row.text(1,build);row.text(2,id);if(!row.row())throw std::runtime_error("This family suggestion no longer exists");auto suggestion=juce::JSON::parse(row.text(0));row.reset();
 juce::var family;juce::String familyId;
 if(action=="accept"){if(options.hasProperty("name"))set(suggestion,"name",get(options,"name"));if(options.hasProperty("query"))set(suggestion,"query",get(options,"query"));family=insertFamily(db,familyDefinition(suggestion));familyId=get(family,"id").toString();}
 else if(action=="merge"){familyId=get(options,"familyId").toString();family=definition(db,familyId);}
 else if(action!="dismiss")throw std::runtime_error("Unknown suggestion decision");
 if(family.isObject()){
  const int n=(int)get(family,"numerator"),d=(int)get(family,"denominator");const auto feel=get(family,"feel").toString(),cohortFeel=get(suggestion,"feel").toString();
  if((n>0&&(n!=(int)get(suggestion,"numerator")||d!=(int)get(suggestion,"denominator")))||(feel!="any"&&cohortFeel!="any"&&feel!=cohortFeel))throw std::runtime_error("Merge into a family with compatible meter and feel");
  sql::Statement include(db,"INSERT OR REPLACE INTO family_decisions SELECT ?,pattern_id,'include' FROM family_suggestion_members WHERE build_id=? AND suggestion_id=?");include.text(1,familyId);include.text(2,build);include.text(3,id);include.done();
 }
 // A merge is a semantic query alias, not a few representative inclusions.
 // Persist the cohort query so all current/future compatible descriptions
 // can join this family through exactly the same affinity policy.
 if(action=="merge"){
  auto aliases=get(family,"queries",juce::Array<juce::var>{});if(!aliases.isArray())aliases=juce::Array<juce::var>{};if(!aliases.getArray()->contains(get(suggestion,"query")))aliases.getArray()->add(get(suggestion,"query"));set(family,"queries",aliases);family.getDynamicObject()->removeProperty("revision");sql::Statement update(db,"UPDATE pattern_families SET definition=?,revision=? WHERE id=?");update.text(1,encode(family));update.integer(2,advance(db));update.text(3,familyId);update.done();
 }
 sql::Statement put(db,"INSERT INTO family_suggestion_decisions VALUES(?,?,?) ON CONFLICT(id) DO UPDATE SET choice=excluded.choice,family_id=excluded.family_id");put.text(1,id);put.text(2,action);put.text(3,familyId);put.done();const auto revision=advance(db,action!="dismiss");transaction.commit();return object({{"revision",(juce::int64)revision},{"family",family},{"action",action}});
}
juce::var PatternFamilies::selection(NeuralIndex& index,const juce::var& options,const SemanticAffinities* prepared) const{
 sql::Database db(database);db.exec("BEGIN");
 const auto query=get(options,"query").toString(),canonical=musicalQueryDescription(query),wantedIndex=get(options,"indexId").toString();
 const int n=(int)get(options,"numerator"),d=(int)get(options,"denominator");const auto feel=get(options,"feel").toString();
 const auto preferred=get(options,"familyId").toString();juce::var chosen;
 auto accepts=[&](const juce::var& family){
  const int fn=(int)get(family,"numerator"),fd=(int)get(family,"denominator");const auto ff=get(family,"feel").toString();if((fn>0&&(fn!=n||fd!=d))||(ff!="any"&&ff!=feel))return false;
  if(get(family,"canonicalQuery").toString()==canonical)return true;
  if(auto aliases=get(family,"queries");aliases.isArray())for(const auto& alias:*aliases.getArray())if(musicalQueryDescription(alias.toString())==canonical)return true;
  return false;
 };
 if(preferred.isNotEmpty()){chosen=definition(db,preferred);if(!accepts(chosen))throw std::runtime_error("Choose a query, meter and feel compatible with this family");}
 else{
  const auto exact=familyIdentity(query,n,d,feel);sql::Statement row(db,"SELECT definition FROM pattern_families WHERE id=?");row.text(1,exact);
  if(row.row())chosen=juce::JSON::parse(row.text(0));row.reset();
  if(!chosen.isObject()){sql::Statement rows(db,"SELECT definition FROM pattern_families ORDER BY id");while(rows.row()){const auto candidate=juce::JSON::parse(rows.text(0));if(!accepts(candidate))continue;if(chosen.isObject())throw std::runtime_error("Several families share this query. Choose a family to resolve.");chosen=candidate;}}
 }
 if(!chosen.isObject()){db.exec("COMMIT");return object({{"available",false}});}
 const auto requested=get(options,"familyBuildId").toString();juce::String build=requested;
 const auto family=get(chosen,"id").toString();int64_t decisionRevision=0;int policy=0;
 if(requested.isNotEmpty()){sql::Statement row(db,"SELECT b.index_id,b.epoch,b.policy FROM family_builds b JOIN family_summaries s ON s.build_id=b.id AND s.family_id=? WHERE b.id=? AND b.state='complete'");row.text(1,family);row.text(2,requested);
  if(!row.row())throw std::runtime_error("This family membership is no longer available");
  if(row.text(0)!=wantedIndex)throw std::runtime_error("This family membership belongs to another library index");
  decisionRevision=row.integer(1);policy=(int)row.integer(2);
 }else{
  // While matching catches up, resolve from the family's latest complete
  // membership of this index (the ready build first): a consistent snapshot,
  // pinned in the result. A family never matched yet resolves as a plain search.
  sql::Statement row(db,"SELECT b.id,b.epoch,b.policy FROM family_builds b JOIN family_summaries s ON s.build_id=b.id AND s.family_id=? WHERE b.state='complete' AND b.index_id=? AND b.policy=? ORDER BY b.id=? DESC,b.epoch DESC,b.id LIMIT 1");row.text(1,family);row.text(2,wantedIndex);row.integer(3,familyPolicyVersion);row.text(4,ready(db));
  if(!row.row()){db.exec("COMMIT");return object({{"available",false}});}
  build=row.text(0);decisionRevision=row.integer(1);policy=(int)row.integer(2);
 }
 const auto loaded=prepared?SemanticAffinities{}:index.affinities(query,wantedIndex,(bool)get(options,"cpu",false));const auto& affinities=prepared?*prepared:loaded;
 if(affinities.indexId!=wantedIndex||affinities.canonicalQuery!=canonical)throw std::runtime_error("Prepared family affinities do not match this query and index");
 std::map<juce::String,SemanticAffinity> scores;for(const auto& group:affinities.groups)scores.emplace(group.id,group);
 const bool batch=options.hasProperty("partMasks");std::vector<std::set<int>> masks;
 auto constraints=options;
 if(batch){
  const auto requested=get(options,"partMasks");if(!requested.isArray()||requested.size()>128)throw std::runtime_error("Select at most 128 pattern part masks");
  if(options.hasProperty("parts"))throw std::runtime_error("Choose parts or partMasks, not both");
  for(const auto& value:*requested.getArray())masks.push_back(PartQuery::mask(value));
  constraints=options.clone();set(constraints,"parts",juce::Array<juce::var>{});
 }
 const PartQuery part(constraints,!batch);
 // Choose compatible aliases once, then sample each mask/role independently.
 // The batch expands committed part counts after that shared materialization;
 // its HAVING clause applies the presence filter deferred by PartQuery above.
 // Manual inclusions remain eligible outside the top semantic groups.
 juce::String requestedSql;
 if(batch){
  juce::StringArray values;for(size_t i=0;i<masks.size();++i)for(const auto& role:PartQuery::roles(masks[i]))values.add("("+juce::String((int)i)+",'"+role+"')");
  requestedSql="requested(mask,part) AS("+(values.isEmpty()?juce::String("SELECT -1,'' WHERE 0"):"VALUES "+values.joinIntoString(","))+"),";
 }
 // The optional hint appeared in SQLite 3.35. Older system libraries use the
 // equivalent ordinary CTE; materializeParts=false exercises that path.
 // https://sqlite.org/lang_with.html#materialization_hints
 const bool materialize=batch&&sqlite3_libversion_number()>=3035000&&(bool)get(options,"materializeParts",true);
 juce::String selectionSql="WITH "+requestedSql+"eligible AS "+(materialize?juce::String("MATERIALIZED "):juce::String())+"(SELECT m.pattern_id,b.role,MIN(b.id) AS binding_id,"+(batch?juce::String("0"):part.density)+" AS density,m.score,m.state"+(batch?",p.features AS features,p.length AS length":"")+" FROM family_members m JOIN bindings b INDEXED BY binding_pattern ON b.pattern_id=m.pattern_id JOIN neural_sources s ON s.run_id=b.run_id AND s.index_id=? JOIN patterns p ON p.id=m.pattern_id";
 if(part.enabled&&part.wantedFeel>=0)selectionSql+=" JOIN neural_descriptions source_description ON source_description.description_id=b.description_id AND source_description.version="+juce::String(semanticDescriptionVersion)+" JOIN neural_text source_text ON source_text.id=source_description.semantic_id";
 selectionSql+=" WHERE m.build_id=? AND m.family_id=? AND m.state IN('matched','included') AND b.role IN('groove','sparse','fill') AND (?=0 OR p.numerator=?) AND (?=0 OR p.denominator=?)"+part.predicate+part.feelPredicate(PartQuery::describedFeelSql("source_text.text"))+" GROUP BY m.pattern_id,b.role),";
 if(batch)selectionSql+="per_part AS(SELECT q.mask,e.binding_id,e.pattern_id,e.role,e.score,e.state,SUM(COALESCE(json_extract(j.value,'$.notes'),0))*1.0/e.length AS density FROM eligible e JOIN json_each(e.features,'$.parts') j JOIN requested q ON q.part=j.key GROUP BY q.mask,e.pattern_id,e.role HAVING SUM(COALESCE(json_extract(j.value,'$.notes'),0))>0),";
 selectionSql+="ranked AS(SELECT *,ROW_NUMBER() OVER(PARTITION BY "+juce::String(batch?"mask,role":"role")+" ORDER BY density,pattern_id) AS ordinal,COUNT(*) OVER(PARTITION BY "+juce::String(batch?"mask,role":"role")+") AS total FROM "+juce::String(batch?"per_part":"eligible")+") "
  "SELECT r.binding_id,r.pattern_id,e.path,b.family,b.role,p.length,p.numerator,p.denominator,p.coverage,p.features,nd.semantic_id,r.score,r.state"+juce::String(batch?",r.mask":"")+" FROM ranked r JOIN bindings b ON b.id=r.binding_id JOIN entries e ON e.run_id=b.run_id AND e.ordinal=b.ordinal JOIN patterns p ON p.id=r.pattern_id JOIN neural_descriptions nd ON nd.description_id=b.description_id AND nd.version=? WHERE "+(part.enabled?PartQuery::sampleSql("r.ordinal","r.total",128):juce::String("((r.ordinal-1)%MAX(1,(r.total+127)/128))=0 OR r.ordinal=r.total"))+" ORDER BY "+juce::String(batch?"r.mask,":"")+"r.role,r.ordinal";
 sql::Statement rows(db,selectionSql.toRawUTF8());
 rows.text(1,wantedIndex);rows.text(2,build);rows.text(3,family);rows.integer(4,n);rows.integer(5,n);rows.integer(6,d);rows.integer(7,d);rows.integer(8,semanticDescriptionVersion);juce::Array<juce::var> candidates;
 std::vector<juce::Array<juce::var>> pools(masks.size());std::map<juce::String,juce::var> features;
 while(rows.row()){
  juce::var measured;
  if(batch){const auto id=rows.text(1);auto found=features.find(id);if(found==features.end())found=features.emplace(id,juce::JSON::parse(rows.text(9))).first;measured=found->second;}
  else measured=juce::JSON::parse(rows.text(9));
  const auto& score=scores.at(rows.text(10));auto candidate=object({{"id",rows.text(0)},{"patternId",rows.text(1)},{"path",rows.text(2)},{"name",rows.text(2).fromLastOccurrenceOf("/",false,false)},{"family",rows.text(3)},{"role",rows.text(4)},{"length",rows.real(5)},{"numerator",(int)rows.integer(6)},{"denominator",(int)rows.integer(7)},{"coverage",rows.real(8)},{"features",measured},{"semanticId",score.id},{"semanticDescription",score.description},{"description",score.description},{"similarity",score.score},{"membershipScore",rows.real(11)},{"membershipState",rows.text(12)}});
  if(batch)pools.at((size_t)rows.integer(13)).add(candidate);else candidates.add(candidate);
 }
 db.exec("COMMIT");auto result=object({{"available",true},{"familyId",family},{"familyBuildId",build},{"familyRevision",(juce::int64)decisionRevision},{"familyPolicyVersion",policy},{"libraryRevision",(juce::int64)affinities.revision},{"modelVersion",affinities.modelVersion},{"interpretedQuery",affinities.canonicalQuery},{"rows",candidates}});
 if(batch){juce::Array<juce::var> values;for(size_t i=0;i<masks.size();++i){juce::Array<juce::var> notes;for(int note:masks[i])notes.add(note);values.add(object({{"parts",notes},{"rows",pools[i]}}));}set(result,"pools",values);}
 return result;
}
juce::var PatternFamilies::lookup(const juce::var& options) const{
 const auto intent=familyDefinition(options);const auto canonical=get(intent,"canonicalQuery").toString();const int n=(int)get(intent,"numerator"),d=(int)get(intent,"denominator");const auto feel=get(intent,"feel").toString();const auto preferred=get(options,"familyId").toString();
 sql::Database db(database);juce::var chosen;bool ambiguous=false;
 auto matches=[&](const juce::var& family){const int fn=(int)get(family,"numerator"),fd=(int)get(family,"denominator");const auto ff=get(family,"feel").toString();if((n>0&&fn>0&&(n!=fn||d!=fd))||(feel!="any"&&ff!="any"&&feel!=ff))return false;if(get(family,"canonicalQuery").toString()==canonical)return true;if(auto aliases=get(family,"queries");aliases.isArray())for(const auto& alias:*aliases.getArray())if(musicalQueryDescription(alias.toString())==canonical)return true;return false;};
 if(preferred.isNotEmpty()){chosen=definition(db,preferred);if(!matches(chosen))throw std::runtime_error("Choose a query, meter and feel compatible with this family");return chosen;}
 {sql::Statement exact(db,"SELECT definition FROM pattern_families WHERE id=?");exact.text(1,get(intent,"id").toString());if(exact.row())return juce::JSON::parse(exact.text(0));}
 sql::Statement rows(db,"SELECT definition FROM pattern_families ORDER BY id");while(rows.row()){const auto candidate=juce::JSON::parse(rows.text(0));if(!matches(candidate))continue;if(chosen.isObject())ambiguous=true;else chosen=candidate;}return ambiguous?juce::var():chosen;
}
}
