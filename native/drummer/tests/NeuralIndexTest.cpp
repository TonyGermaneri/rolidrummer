#include <drummer/NeuralIndex.h>
#include <drummer/PatchResolver.h>
#include <drummer/PatternFamilies.h>
#include <drummer/RhythmSpace.h>
#include <drummer/Scheduler.h>
#include <drummer/Json.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_cryptography/juce_cryptography.h>
#include "../src/LibrarySqlite.h"
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace drummer;
using namespace drummer::json;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Fixture {
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("rd-neural-test", "");
    Fixture() { check(root.createDirectory().wasOk(), "Cannot create neural test directory"); }
    ~Fixture() { root.deleteRecursively(); }
    void midi(const juce::File& directory, const juce::String& name, int seed) {
        auto file = directory.getChildFile(name); file.getParentDirectory().createDirectory();
        juce::MidiMessageSequence track;
        track.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
        for (int i = 0; i < 16 - (seed % 3) * 4; ++i) {
            const int note = i % 4 == 0 ? 36 : i % 4 == 2 ? 38 : name.contains("/Fills/") ? 45 : 42;
            const auto at = i * 240.0 + seed % 29;
            track.addEvent(juce::MidiMessage::noteOn(10, note, (juce::uint8) (60 + (seed * 7 + i * 3) % 65)), at);
            track.addEvent(juce::MidiMessage::noteOff(10, note), at + 80);
        }
        track.addEvent(juce::MidiMessage::endOfTrack(), 3840);
        juce::MidiFile midi; midi.setTicksPerQuarterNote(960); midi.addTrack(track);
        juce::FileOutputStream stream(file); check(midi.writeTo(stream), "Cannot write neural MIDI fixture");
    }
};
juce::var first(const juce::var& result) { auto rows = get(result, "rows"); check(rows.isArray() && !rows.getArray()->isEmpty(), "Neural search returned no candidates"); return (*rows.getArray())[0]; }
}
int main(int argc, char** argv) { try {
    if (argc != 2) throw std::runtime_error("Expected pinned neural asset directory");
    check(semanticDescription("GM MIDI Pack / GM - Rock 1 / DFH / Hard Rock / 180 Hard Rock 04b Fill 2") == semanticDescription("Hard Rock/Fill 37"), "Export IDs prevent semantic description caching");
    check(semanticDescription("Metal/Groove F4") != semanticDescription("Metal/Fill 4"), "An opaque F4 suffix was invented into a fill label");
    check(semanticDescription("Funk/16th Hats") != semanticDescription("Funk/8th Hats"), "Musical subdivision was removed with ordinal IDs");
    check(musicalQueryDescription("Heavy Rock")=="hard rock drum patterns" && musicalQueryDescription("Heavy Rockabilly").contains("heavy rockabilly"),"Musical synonym normalization changed another word");
    Fixture fixture;
    const auto source = fixture.root.getChildFile("source"), db = fixture.root.getChildFile("library.sqlite");
    int seed = 0;
    for (const auto* style : {"Hard Rock", "Bossa Nova", "Jazz Brushes", "Heavy Metal", "Funk"})
        for (int i = 0; i < 8; ++i) fixture.midi(source, juce::String(style) + (i < 5 ? "/Grooves/" : "/Fills/") + juce::String(i) + ".mid", ++seed);
    for(int i=0;i<11;++i)fixture.midi(source,"Hard Rock/Fills/Related "+juce::String(i)+".mid",200+i);
    // Same mapped performance, aliases in the same musical family.
    auto original = source.getChildFile("Hard Rock/Grooves/0.mid");
    auto alias = source.getChildFile("GM MIDI Pack/GM - Rock 1/DFH/Hard Rock/Grooves/999.mid"); alias.getParentDirectory().createDirectory(); original.copyFileTo(alias);
    PatternLibrary catalog(db); catalog.importSource(source);
    NeuralIndex index(db, juce::File(juce::String::fromUTF8(argv[1])));
    bool cancel = false;
    NeuralBuildOptions paused;
    paused.cancelled = [&] { return cancel; };
    paused.progress = [&](const auto& progress) { if (number(progress, "descriptions", 0) > 0) cancel = true; };
    check(get(index.build(paused), "state").toString() == "paused", "Index cancellation did not checkpoint");
    check(!index.status().hasProperty("readyIndexId"), "Incomplete vectors were published for retrieval");
    cancel=false;
    NeuralBuildOptions snapshotPause;snapshotPause.cancelled=[&]{return cancel;};
    snapshotPause.progress=[&](const auto& progress){if(get(progress,"stage").toString()=="snapshot")cancel=true;};
    const auto pausedSnapshot=index.build(snapshotPause);
    check(get(pausedSnapshot,"state").toString()=="paused"&&get(pausedSnapshot,"stage").toString()=="snapshot"&&!index.status().hasProperty("readyIndexId"),"An incomplete packed snapshot was published or could not pause");
    const auto completed = index.build();
    check(get(completed, "state").toString() == "complete" && number(completed, "groups", 0) < number(completed, "descriptions", 0), "Embedding resume or normalized description grouping failed");
    const auto oldId = get(completed, "indexId");
    {PatternFamilies initialized(db);}
    {sql::Database writer(db);sql::Transaction preparation(writer);
        PatternLibrary opened(db);NeuralIndex reader(db,juce::File(juce::String::fromUTF8(argv[1])));PatternFamilies openedFamilies(db);
        check(get(reader.status(),"readyIndexId")==oldId&&number(opened.status(),"patterns",0)>0,"Opening another initialized instance required catalog write admission");
        check(get(reader.build(),"state").toString()=="busy"&&get(reader.status(),"readyIndexId")==oldId,"Concurrent preparation discarded the ready index instead of reporting a retryable busy state");
    }
    auto query = object({{"query", "Heavy Rock"}, {"limit", 20}, {"groups", 10}, {"numerator", 4}, {"denominator", 4}});
    const auto rock = index.search(query);
    check((bool)get(index.resources(),"packedSnapshotCacheHit"),"Indexing did not prepare a reusable packed snapshot");
    {NeuralIndex other(db,juce::File(juce::String::fromUTF8(argv[1])));other.search(query);
        const auto resources=other.resources();
        check(number(resources,"snapshotReferences",0)>=2&&number(resources,"embeddingReferences",0)>=2&&number(resources,"vectorRows",0)>0,"Instances duplicated their immutable model or search matrix");
    }
    check(get(first(rock), "semanticDescription").toString().contains("hard rock"), "Semantic synonym query did not retrieve Hard Rock");
    std::set<juce::String> patterns;
    for (const auto& row : *get(rock, "rows").getArray()) check(patterns.insert(get(row, "patternId").toString()).second, "Duplicate aliases crowd semantic results");
    set(query, "cpu", true); const auto oracle = index.search(query);
    auto expected = get(oracle, "rows"), actual = get(rock, "rows");
    check(expected.getArray()->size() == actual.getArray()->size(), "CPU/GPU semantic result count differs");
    for (int i = 0; i < actual.getArray()->size(); ++i) check(get((*actual.getArray())[i], "patternId") == get((*expected.getArray())[i], "patternId"), "CPU/GPU semantic ranking differs");
    check((bool)get(get(index.resources(),"lastSearch"),"coveringScalarIndex")&&(bool)get(get(index.resources(),"lastSearch"),"coveringBindingIndex"),"Indexing did not prepare both covering search paths");
    {sql::Database database(db);database.exec("DROP INDEX pattern_search_scalars;DROP INDEX neural_binding_scalars;");}
    std::vector<juce::var> filterCases;std::vector<juce::String> fallbackResults;
    for(const auto* role:{"","groove","fill"})for(double coverage:{0.,.6,1.}){
        auto filters=query.clone();set(filters,"role",role);set(filters,"minCoverage",coverage);filterCases.push_back(filters);
    }
    {auto odd=query.clone();set(odd,"numerator",7);set(odd,"denominator",8);filterCases.push_back(odd);}
    for(const auto& filters:filterCases)fallbackResults.push_back(juce::JSON::toString(get(index.search(filters),"rows"),true));
    {sql::Database database(db);database.exec("CREATE INDEX pattern_search_scalars ON patterns(id) WHERE coverage>.9;CREATE INDEX neural_binding_scalars ON bindings(description_id) WHERE role='fill';");}
    check(juce::JSON::toString(get(index.search(query),"rows"),true)==juce::JSON::toString(get(oracle,"rows"),true)&&!(bool)get(get(index.resources(),"lastSearch"),"coveringScalarIndex")&&!(bool)get(get(index.resources(),"lastSearch"),"coveringBindingIndex"),"An incompatible partial index bypassed search eligibility filters");
    {sql::Database database(db);database.exec("DROP INDEX pattern_search_scalars;DROP INDEX neural_binding_scalars;WITH RECURSIVE seq(n) AS (VALUES(1) UNION ALL SELECT n+1 FROM seq WHERE n<4096) INSERT INTO patterns SELECT 'cancel-fixture-'||n,payload_id,channel,track,mapping,length,numerator,denominator,density,velocity,coverage,features FROM seq CROSS JOIN (SELECT * FROM patterns LIMIT 1);");}
    bool metadataArmed=false;int metadataPolls=0;NeuralBuildOptions metadataPause;
    metadataPause.progress=[&](const auto& value){if(get(value,"stage").toString()=="search-metadata")metadataArmed=true;};
    metadataPause.cancelled=[&]{return metadataArmed&&++metadataPolls>=4;};
    const auto pausedMetadata=index.build(metadataPause);
    check(get(pausedMetadata,"state").toString()=="paused"&&get(pausedMetadata,"stage").toString()=="search-metadata"&&get(index.status(),"readyIndexId")==oldId,"Cancelling SQLite index preparation unpublished the prior complete index");
    {sql::Database database(db);sql::Statement partial(database,"SELECT 1 FROM sqlite_master WHERE name IN ('pattern_search_scalars','neural_binding_scalars')");check(!partial.row(),"Cancelled CREATE INDEX left a partial search index");partial.reset();database.exec("DELETE FROM patterns WHERE id LIKE 'cancel-fixture-%'");}
    check(get(index.build(),"indexId")==oldId&&!(bool)get(index.status(),"preparationNeeded"),"Metadata resume changed index identity or failed to prepare search");
    for(size_t i=0;i<filterCases.size();++i)check(juce::JSON::toString(get(index.search(filterCases[i]),"rows"),true)==fallbackResults[i],"Covering search changed filtering, alias selection or diversity ranking");
    {sql::Database database(db);database.exec("DROP INDEX neural_binding_scalars");sql::Statement lease(database,"INSERT INTO import_leases VALUES('metadata-fixture','active-import',?)");lease.integer(1,juce::Time::currentTimeMillis()+60000);lease.done();}
    index.build();check((bool)get(index.status(),"preparationNeeded"),"Search preparation did not defer to an active import lease");
    {sql::Database database(db);database.exec("DELETE FROM import_leases WHERE source_id='metadata-fixture'");}
    {std::shared_ptr<LibraryService> service;
        {sql::Database writer(db);sql::Transaction preparation(writer);service=LibraryService::acquire(db);
            for(int wait=0;wait<2000&&get(service->progress(),"state").toString()!="busy";++wait)juce::Thread::sleep(5);
            check(get(service->progress(),"state").toString()=="busy"&&(bool)get(service->progress(),"running")&&get(service->neural->status(),"readyIndexId")==oldId,"A new instance could not open and queue preparation behind another catalog writer");
            preparation.commit();}
        for(int wait=0;wait<2000&&(bool)get(service->progress(),"running");++wait)juce::Thread::sleep(5);
        check(!(bool)get(service->progress(),"running")&&get(service->progress(),"state").toString()=="complete"&&!(bool)get(service->neural->status(),"preparationNeeded")&&get(service->neural->status(),"readyIndexId")==oldId,"Opening an older complete catalog did not retry and prepare search on its background worker");}
    // The main instance now retains CPU resources. Fresh GPU instances must
    // recover missing/damaged disposable caches without changing source results.
    auto gpuQuery=query.clone();set(gpuQuery,"cpu",false);set(gpuQuery,"indexId",oldId);
    for(int damage=0;damage<4;++damage){
        {
            sql::Database database(db);
            if(damage==0)database.exec("DELETE FROM neural_snapshots");
            if(damage==1)database.exec("UPDATE neural_snapshots SET payload=substr(payload,1,32)");
            if(damage==2)database.exec("UPDATE neural_snapshots SET format=999");
            if(damage==3){
                sql::Statement read(database,"SELECT payload FROM neural_snapshots WHERE index_id=?");read.text(1,oldId.toString());check(read.row(),"Snapshot recovery did not republish its cache");
                auto bytes=read.blob(0);read.reset();bytes.setSize(24);
                sql::Statement update(database,"UPDATE neural_snapshots SET payload=?,checksum=? WHERE index_id=?");
                update.blob(1,bytes);update.text(2,juce::SHA256(bytes).toHexString());update.text(3,oldId.toString());update.done();
            }
        }
        {
            NeuralIndex repaired(db,juce::File(juce::String::fromUTF8(argv[1])));
            check(juce::JSON::toString(get(repaired.search(gpuQuery),"rows"),true)==juce::JSON::toString(get(rock,"rows"),true),"A missing or corrupt packed snapshot changed authoritative search results");
            const auto resource=repaired.resources();
            check(!(bool)get(resource,"packedSnapshotCacheHit")&&(damage==0||get(resource,"snapshotCacheError").toString().isNotEmpty()),"Snapshot corruption was not detected before rebuilding");
        }
        {NeuralIndex reopened(db,juce::File(juce::String::fromUTF8(argv[1])));reopened.search(gpuQuery);check((bool)get(reopened.resources(),"packedSnapshotCacheHit"),"A repaired packed snapshot cannot be reopened");}
    }
    {sql::Database database(db);database.exec("DELETE FROM neural_snapshots");}
    NeuralBuildOptions pauseBackfill;pauseBackfill.cancelled=[]{return true;};
    const auto backfillPaused=index.build(pauseBackfill);
    check(get(backfillPaused,"state").toString()=="paused"&&get(backfillPaused,"stage").toString()=="snapshot"&&get(index.status(),"readyIndexId")==oldId,"Pausing optional cache preparation unpublished a complete index");
    check(get(index.build(),"indexId")==oldId,"Resuming cache preparation changed the index identity");
    const auto oldBinding = get(first(rock), "id").toString();
    check(!get(catalog.pattern(oldBinding), "notes").getArray()->isEmpty(), "Retrieved binding cannot prepare playback");
    const auto bossa = index.search(object({{"query", "Bossa Nova"}, {"role", "fill"}}));
    check(get(first(bossa), "semanticDescription").toString().contains("bossa nova") && get(first(bossa), "role").toString() == "fill", "Style or role compatibility filter failed");
    auto originalZones = juce::JSON::parse(R"([{"upper":1,"clipId":"manual"}])");
    juce::Array<juce::var> keys;
    for (const auto& [key, part] : std::vector<std::pair<int,int>> {{48,36},{50,42},{52,38},{53,81},{55,42},{56,47}})
        keys.add(object({{"note", key}, {"parts", juce::Array<juce::var> {part}}, {"zones", originalZones.clone()}, {"neuralLock", key == 55}, {"sound", object({{"path", "factory:snare"}})}}));
    const auto base = object({{"version", 1}, {"keys", keys}, {"clips", juce::Array<juce::var>()}, {"fx", object({{"delayMix", .37}})}});
    const auto resolved = resolveNeuralPatch(catalog, index, base, object({{"query", "Heavy Rock"}, {"indexId", oldId}}));
    const auto resolution = get(resolved, "neural");
    check(get(resolution,"familyId").toString()==familyIdentity("Heavy Rock",4,4,"straight"),"Resolved family identity lost its musical constraints");
    auto copied=resolved.clone();set(copied,"id","saved-explicit-copy");
    check(get(resolveNeuralPatch(catalog,index,copied,object({{"query","Hard Rock"},{"indexId",oldId}})),"id").toString()=="saved-explicit-copy","Resolving a synonymous family replaced a saved copy's identity");
    check(get(resolved, "name").toString() == "Heavy Rock" && number(get(resolved, "fx"), "delayMix", 0) == .37, "Resolution did not preserve the kit's sound settings");
    auto resolvedKeys = get(resolution, "keys");
    check(resolvedKeys.getArray()->size() == 4, "Compatible parts were lost or locked drum parts were overwritten");
    for (const auto& key : *resolvedKeys.getArray()) {
        check(number(key, "sparseDensity", 0) <= number(key, "steadyDensity", 0), "Sparse region is denser than its steady region");
        if ((int) get(key, "key") == 50) check((bool) get(key, "fillFallback"), "A tom fill was selected for a hat key with no hat notes");
    }
    const auto repeated = resolveNeuralPatch(catalog, index, base, object({{"query", "Heavy Rock"}, {"indexId", oldId}}));
    check(juce::JSON::toString(resolved, true) == juce::JSON::toString(repeated, true), "Fixed query, index and mapping did not resolve deterministically");
    const auto cpuResolved=resolveNeuralPatch(catalog,index,base,object({{"query","Heavy Rock"},{"indexId",oldId},{"cpu",true}}));
    check(juce::JSON::toString(cpuResolved,true)==juce::JSON::toString(resolved,true),"CPU/Metal search changed the complete resolved patch");
    auto preparedKeys = get(resolved, "keys");
    check(get((*get((*preparedKeys.getArray())[3],"zones").getArray())[0],"clipId").toString().isEmpty(),"A missing drum part was routed into an unrelated fallback pattern");
    check(juce::JSON::toString(get((*preparedKeys.getArray())[4], "zones")) == juce::JSON::toString(originalZones), "Manual locks lost their assignments");
    const auto tomZones=get((*preparedKeys.getArray())[5],"zones");
    check(get((*tomZones.getArray())[0],"clipId").toString().isEmpty()&&get((*tomZones.getArray())[1],"clipId").toString().isEmpty()&&get((*tomZones.getArray())[2],"clipId").toString().isNotEmpty(),"A fill-only drum part lost its upper region or played unrelated grooves");
    const auto space=get((*preparedKeys.getArray())[0],"rhythmSpace");
    check(get(space,"components").getArray()->size()==rhythmDimensions && get(space,"eigenvalues").getArray()->size()==rhythmDimensions,"The resolved family did not persist its fitted rhythm space");
    check((int)get(space,"version")==RhythmSpace::version&&number(space,"precision",0)==1e-10,"Resolved PCA lacks its versioned numerical precision");
    const auto generatedOptions=object({{"query","Heavy Rock"},{"indexId",oldId},{"seed","musician-session"},{"variationCount",2}});
    const auto generated=resolveNeuralPatch(catalog,index,base,generatedOptions);
    check(number(get(generated,"neural"),"generatedCount",0)>0,"Opt-in patch resolution did not add generated notes");
    const auto generatedKeys=get(generated,"keys"),tomFillChoices=get((*get((*generatedKeys.getArray())[5],"zones").getArray())[2],"candidates");
    int originalFills=0,newFills=0;for(const auto& choice:*tomFillChoices.getArray())((bool)get(choice,"generated",false)?newFills:originalFills)++;
    check(originalFills==14&&newFills==2,"Fourteen related fills did not retain all originals plus two generated candidates");
    for(const auto& key:*get(generated,"keys").getArray())for(const auto& zone:*get(key,"zones").getArray()){
        const auto candidates=get(zone,"candidates");if(!candidates.isArray())continue;
        check(candidates.getArray()->size()<=16,"Generated variations exceeded the per-region runtime budget");
        for(const auto& candidate:*candidates.getArray())if((bool)get(candidate,"generated",false)){
            bool found=false;
            for(const auto& clip:*get(generated,"clips").getArray())if(get(clip,"id")==get(candidate,"clipId")){
                found=true;check(get(clip,"generation").isObject(),"Generated candidate lost its provenance");
                for(const auto& note:*get(clip,"notes").getArray())check(get(key,"parts").getArray()->contains(get(note,"note")),"Generated clip contains another key's drum part");
            }
            check(found,"Generated candidate does not embed its actual notes");
        }
    }
    check(juce::JSON::toString(generated,true)==juce::JSON::toString(resolveNeuralPatch(catalog,index,base,generatedOptions),true),"Seeded patch resolution was not deterministic");
    // Auto uses an explicit generation budget; seeds never enable it.
    const std::map<int,juce::String> autoSeeds{{0,"0"},{1,"1"},{2,"2"},{3,"3"},{4,"4"}};
    const juce::String alternateOriginal="patterns-only-alternative";
    juce::var originalAuto,lastAuto;
    for(const auto& [count,autoSeed]:autoSeeds){
        const auto options=object({{"query","Heavy Rock"},{"indexId",oldId},{"autoMode",true},{"seed",autoSeed},{"variationCount",count},{"phraseBars",1}});
        const auto automatic=resolveNeuralPatch(catalog,index,base,options),intent=get(automatic,"neural");
        check((bool)get(intent,"autoMode")&&(int)get(intent,"phraseBars")==8&&(int)get(intent,"variationCount")==count,"Auto ignored the explicit generation count or exposed manual phrase choices");
        check(get(intent,"keys").getArray()->size()==5,"Auto did not assign a previously locked compatible key");
        check(number(get(automatic,"fx"),"delayMix",0)==.37,"Auto changed sound settings");
        check((count==0)==(number(intent,"generatedCount",0)==0),"Auto ignored the requested originals versus generated material");
        check(number(intent,"generatedCount",0)<=count,"Auto exceeded its kit-wide limit of 1–4 generated tracks");
        for(const auto& key:*get(automatic,"keys").getArray())for(const auto& zone:*get(key,"zones").getArray()){
            const auto choices=get(zone,"candidates");if(!choices.isArray())continue;
            int generatedChoices=0;bool generatedPrimary=false;
            for(const auto& choice:*choices.getArray()){
                bool found=false;for(const auto& clip:*get(automatic,"clips").getArray())if(get(clip,"id")==get(choice,"clipId")){
                    found=true;if((bool)get(choice,"generated",false)){
                        ++generatedChoices;generatedPrimary|=get(zone,"clipId")==get(clip,"id");
                        for(const auto& note:*get(clip,"notes").getArray())check(get(key,"parts").getArray()->contains(get(note,"note")),"Auto generated notes leaked across drum parts");
                    }
                }check(found,"Auto zone contains a dangling clip reference");
            }
            check(generatedChoices<=count,"Auto exceeded the explicitly selected generation count");
            check(!generatedChoices||generatedPrimary,"Generated Auto seed still starts an original pattern");
        }
        auto repeatOptions=options.clone();set(repeatOptions,"phraseBars",64);set(repeatOptions,"cpu",true);
        check(juce::JSON::toString(automatic,true)==juce::JSON::toString(resolveNeuralPatch(catalog,index,base,repeatOptions),true),"Auto seed is not deterministic across manual settings or CPU/Metal");
        if(count==0)originalAuto=automatic;
        if(!lastAuto.isVoid()){
            const auto rerolled=resolveNeuralPatch(catalog,index,lastAuto,options);
            check(juce::JSON::toString(get(rerolled,"clips"),true)==juce::JSON::toString(get(automatic,"clips"),true),"Auto rerolls accumulate abandoned pattern pools");
        }
        lastAuto=automatic;
    }
    const auto defaultAuto=resolveNeuralPatch(catalog,index,base,object({{"query","Heavy Rock"},{"indexId",oldId},{"autoMode",true},{"seed",autoSeeds.at(0)}}));
    check(juce::JSON::toString(defaultAuto,true)==juce::JSON::toString(originalAuto,true),"Auto defaults can enable generation without an explicit choice");
    const auto originalsAgain=resolveNeuralPatch(catalog,index,lastAuto,object({{"query","Heavy Rock"},{"indexId",oldId},{"autoMode",true},{"seed",autoSeeds.at(0)},{"variationCount",0}}));
    check(juce::JSON::toString(get(originalsAgain,"clips"),true)==juce::JSON::toString(get(originalAuto,"clips"),true),"Patterns Only retained generated tracks from a previous resolve");
    for(const auto& clip:*get(originalsAgain,"clips").getArray())check(!get(clip,"generation").isObject(),"Patterns Only leaked a generated clip into the active pool");
    const auto alternative=resolveNeuralPatch(catalog,index,base,object({{"query","Heavy Rock"},{"indexId",oldId},{"autoMode",true},{"seed",alternateOriginal}}));
    check(juce::JSON::toString(get(alternative,"keys"),true)!=juce::JSON::toString(get(originalAuto,"keys"),true),"Original-only seeds do not change actual assignments");
    auto performance=[](const juce::var& document){
        Configuration config;std::map<juce::String,int> clips;
        for(const auto& clip:*get(document,"clips").getArray()){
            clips[get(clip,"id").toString()]=(int)config.clips.size();Clip runtime;
            runtime.id=(uint64_t)juce::SHA256(juce::JSON::toString(get(clip,"notes")).toUTF8()).toHexString().substring(0,16).getHexValue64();
            config.clips.push_back(runtime);
        }
        for(const auto& binding:*get(document,"keys").getArray()){
            auto& key=config.keys[(size_t)(int)get(binding,"note")];key.enabled=true;key.intentEnabled=true;
            const auto zones=get(binding,"zones");key.zoneCount=zones.getArray()->size();
            for(int i=0;i<key.zoneCount;++i){
                const auto& source=(*zones.getArray())[i];auto& zone=key.zones[(size_t)i];zone.upper=(float)(double)get(source,"upper");
                const auto primary=get(source,"clipId").toString();zone.clip=primary.isEmpty()?-1:clips.at(primary);
                const auto choices=get(source,"candidates");if(!choices.isArray())continue;
                PatternGroup group;
                for(const auto& choice:*choices.getArray()){
                    auto& candidate=group.candidates[(size_t)group.count++];candidate.clip=clips.at(get(choice,"clipId").toString());
                    const auto position=get(choice,"position");for(int d=0;d<intentDimensions;++d)candidate.position[(size_t)d]=(float)(double)(*position.getArray())[d];
                }
                zone.group=(int)config.groups.size();config.groups.push_back(group);
            }
        }
        return config;
    };
    const auto originalRuntime=performance(originalAuto),alternativeRuntime=performance(alternative);
    auto selectedContent=[](const Configuration& config,int key,int slide,int press,int glide){
        Scheduler scheduler;const Midi messages[]={{0,0xb1,74,(uint8_t)slide},{0,0xd1,(uint8_t)press,0},{0,0xe1,(uint8_t)(glide&127),(uint8_t)(glide>>7)},{0,0x91,(uint8_t)key,100}};
        scheduler.process(config,{},1,messages,4,[](void*,const Event&){},nullptr);
        const int clip=scheduler.contacts()[1].clip;return clip<0?uint64_t(0):config.clips[(size_t)clip].id;
    };
    bool differentPerformance=false;
    for(int note:{48,50,52,55,56})for(int slide:{0,64,127})for(int press:{0,64,127})for(int glide:{0,8192,16383})
        differentPerformance|=selectedContent(originalRuntime,note,slide,press,glide)!=selectedContent(alternativeRuntime,note,slide,press,glide);
    check(differentPerformance,"Original-only seed changes never reach the scheduler's gesture-selected notes");
    auto preservedTemplate=lastAuto.clone();const auto kept=get((*get(lastAuto,"clips").getArray())[0],"id");
    set(preservedTemplate,"templateZones",juce::Array<juce::var>{object({{"upper",1.0},{"clipId",kept}})});
    const auto pruned=resolveNeuralPatch(catalog,index,preservedTemplate,object({{"query","Heavy Rock"},{"indexId",oldId},{"autoMode",true},{"seed",autoSeeds.at(0)}}));
    bool keptTemplate=false;for(const auto& clip:*get(pruned,"clips").getArray())keptTemplate|=get(clip,"id")==kept;
    check(keptTemplate,"Auto pruning removed a clip still used by template zones");
    fixture.midi(source, "Disco/Groove.mid", 100);
    catalog.importSource(source);
    check((bool) get(index.status(), "stale"), "New imports do not mark the searchable index stale");
    set(query, "indexId", oldId);
    auto preserved = get(index.search(query), "rows");
    for (int i = 0; i < expected.getArray()->size(); ++i) check(get((*expected.getArray())[i], "id") == get((*preserved.getArray())[i], "id"), "Pinned query changed after a source revision");
    check(get(catalog.pattern(oldBinding), "id").toString() == get(first(rock), "patternId").toString(), "Committed old-index binding stopped being playable");
    const auto recalled = resolveNeuralPatch(catalog, index, base, object({{"query", "Heavy Rock"}, {"indexId", oldId}}));
    check(juce::JSON::toString(recalled, true) == juce::JSON::toString(resolved, true), "Pinned patch resolution changed after a library import");
    check(juce::JSON::toString(generated,true)==juce::JSON::toString(resolveNeuralPatch(catalog,index,base,generatedOptions),true),"Pinned generated performance changed after an import");
    const auto updated = index.build();
    check(get(updated, "indexId") != oldId && !(bool) get(index.status(), "stale"), "New complete index did not publish its snapshot");
    check(get(first(index.search(object({{"query", "Disco"}}))), "semanticDescription").toString().contains("disco"), "New patterns were not added to semantic retrieval");
    for(int i=0;i<3;++i){fixture.midi(source,"Disco/Later "+juce::String(i)+".mid",101+i);catalog.importSource(source);index.build();}
    {sql::Database database(db);sql::Statement count(database,"SELECT COUNT(*) FROM neural_snapshots");count.row();check(count.integer(0)<=3,"Disposable snapshot storage grows without a revision bound");
        sql::Statement old(database,"SELECT 1 FROM neural_snapshots WHERE index_id=?");old.text(1,oldId.toString());check(!old.row(),"Old snapshot pruning fixture did not exercise historical fallback");}
    check(juce::JSON::toString(get(index.search(gpuQuery),"rows"),true)==juce::JSON::toString(get(rock,"rows"),true),"Pruning a disposable cache broke pinned historical retrieval");
    std::cout << "neural index: pretrained synonym retrieval, normalized/shared/packed cache, corruption recovery, bounded snapshot history, aliases, role/meter filters, CPU/Metal ranking, checkpoint/resume and immutable revision recall passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
