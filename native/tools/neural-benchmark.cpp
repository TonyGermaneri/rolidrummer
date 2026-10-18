#include <drummer/NeuralIndex.h>
#include <drummer/PatchResolver.h>
#include <drummer/State.h>
#include <drummer/Json.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>
#include "LibrarySqlite.h"
#include <algorithm>
#include <future>
#include <iostream>
#include <mach/mach.h>
#include <stdexcept>

namespace {
double memoryMiB(){mach_task_basic_info_data_t info{};mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;return task_info(mach_task_self(),MACH_TASK_BASIC_INFO,reinterpret_cast<task_info_t>(&info),&count)==KERN_SUCCESS?double(info.resident_size)/1048576:0;}
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
    using namespace drummer;using namespace drummer::json;
    if(argc<3||argc>8)throw std::runtime_error("Usage: drummer-neural-benchmark CATALOG QUERY [search|resolve [REPEATS [INSTANCES [cpu|gpu [OUTPUT_JSON]]]]]");
    const juce::File file(juce::String::fromUTF8(argv[1]));if(!file.existsAsFile())throw std::runtime_error("Catalog does not exist");
    const auto query=juce::String::fromUTF8(argv[2]),mode=argc>3?juce::String(argv[3]):"search";
    const int repeats=argc>4?juce::String(argv[4]).getIntValue():3,instances=argc>5?juce::String(argv[5]).getIntValue():2;
    const auto provider=argc>6?juce::String(argv[6]):"gpu";
    if((mode!="search"&&mode!="resolve")||repeats<1||repeats>20||instances<1||instances>8||(provider!="gpu"&&provider!="cpu"))throw std::runtime_error("Invalid benchmark mode or bounds");
    const double started=juce::Time::getMillisecondCounterHiRes(),initialMemory=memoryMiB();
    PatternLibrary library(file);std::vector<std::unique_ptr<NeuralIndex>> indexes;
    std::vector<std::unique_ptr<Runtime>> retained(static_cast<size_t>(instances));
    for(int i=0;i<instances;++i)indexes.push_back(std::make_unique<NeuralIndex>(file));
    const auto status=indexes.front()->status();const auto id=get(status,"readyIndexId").toString();if(id.isEmpty())throw std::runtime_error("No complete index is available");
    juce::String model;
    {sql::Database db(file);sql::Statement row(db,"SELECT model FROM neural_indexes WHERE id=?");row.text(1,id);if(!row.row())throw std::runtime_error("Missing pinned index");model=row.text(0);}
    const auto queryId=juce::SHA256(("semantic-"+juce::String(semanticDescriptionVersion)+":query-"+juce::String(musicalQueryVersion)+":"+musicalQueryDescription(query)).toUTF8()).toHexString();
    auto cached=[&]{sql::Database db(file);sql::Statement row(db,"SELECT 1 FROM neural_queries WHERE id=? AND model=?");row.text(1,queryId);row.text(2,model);return row.row();};
    const double setup=juce::Time::getMillisecondCounterHiRes()-started;
    auto call=[&](int instance,const juce::String& phase){
        const bool cache=cached();const double beforeMemory=memoryMiB(),begin=juce::Time::getMillisecondCounterHiRes();
        auto options=object({{"query",query},{"indexId",id},{"cpu",provider=="cpu"},{"limit",50},{"variationCount",0},{"seed","latency-evaluation"}});
        PatchResolveMetrics metrics;
        auto result=mode=="search"?indexes[size_t(instance)]->search(options):resolveNeuralPatch(library,*indexes[size_t(instance)],defaultDocument(),options,&metrics);
        const double resolved=juce::Time::getMillisecondCounterHiRes();
        int clips=0,groups=0;
        if(mode=="resolve"){auto runtime=compileDocument(result);clips=int(runtime->config.clips.size());groups=int(runtime->config.groups.size());retained[size_t(instance)]=std::move(runtime);}
        const double finished=juce::Time::getMillisecondCounterHiRes();
        auto stable=result.clone();if(auto* object=stable.getDynamicObject())object->removeProperty("milliseconds");
        const auto digest=juce::SHA256(juce::JSON::toString(stable).toUTF8()).toHexString();
        return object({{"phase",phase},{"instance",instance},{"queryCachedBefore",cache},{"milliseconds",finished-begin},{"operationMilliseconds",resolved-begin},{"compileMilliseconds",finished-resolved},
            {"residentBeforeMiB",beforeMemory},{"residentAfterMiB",memoryMiB()},{"sha256",digest},{"rows",get(result,"rows").size()},{"compiledClips",clips},{"candidateGroups",groups},
            {"familyId",get(get(result,"neural"),"familyId")},{"familyBuildId",get(get(result,"neural"),"familyBuildId")},{"warnings",get(get(result,"neural"),"warnings")},{"resources",indexes[size_t(instance)]->resources()},
            {"resolvePhases",mode=="resolve"?object({{"intentMilliseconds",metrics.intentMilliseconds},{"partCandidateMilliseconds",metrics.partCandidateMilliseconds},{"clipLoadMilliseconds",metrics.clipLoadMilliseconds},{"selectionMilliseconds",metrics.selectionMilliseconds},{"documentMilliseconds",metrics.documentMilliseconds},{"totalMilliseconds",metrics.totalMilliseconds},{"partMasks",metrics.partMasks},{"clipLoads",metrics.clipLoads}}):juce::var()}});
    };
    juce::Array<juce::var> reports;reports.add(call(0,"first-in-process"));const auto expected=get(reports[0],"sha256");
    for(int i=0;i<repeats;++i)reports.add(call(0,"warm-repeat"));
    for(int instance=1;instance<instances;++instance)reports.add(call(instance,"first-in-shared-instance"));
    if(instances>1){std::vector<std::future<juce::var>> pending;
        for(int instance=0;instance<instances;++instance)pending.push_back(std::async(std::launch::async,[&,instance]{return call(instance,"concurrent-shared-instances");}));
        for(auto& result:pending)reports.add(result.get());
    }
    for(const auto& report:reports)if(get(report,"sha256")!=expected)throw std::runtime_error("Repeated or concurrent instance results changed");
    juce::Array<juce::var> resources;for(const auto& index:indexes){const auto value=index->resources();if(number(value,"snapshotReferences",0)<instances||number(value,"embeddingReferences",0)<instances)throw std::runtime_error("Instances did not retain shared matrix/model resources");resources.add(value);}
    const auto output=object({{"state","complete"},{"query",query},{"mode",mode},{"indexId",id},{"modelVersion",model},{"providerRequested",provider},{"instances",instances},{"repeats",repeats},{"setupMilliseconds",setup},
        {"initialResidentMiB",initialMemory},{"finalResidentMiB",memoryMiB()},{"scope","Fresh process, existing filesystem cache; first-query cache state is recorded. No system cache purge. All operations are off audio."},
        {"identicalResults",true},{"resources",resources},{"calls",reports}});
    const auto text=juce::JSON::toString(output);
    if(argc>7&&!juce::File(juce::String::fromUTF8(argv[7])).replaceWithText(text))throw std::runtime_error("Cannot write benchmark report");
    std::cout<<text<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
