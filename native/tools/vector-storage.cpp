// Development-only storage/fidelity experiment. No quantized data is published
// to the catalog and the instrument continues to use its canonical float32 rows.
#include <drummer/NeuralIndex.h>
#include <drummer/VectorSearch.h>
#include <drummer/Json.h>
#include <juce_cryptography/juce_cryptography.h>
#include <onnxruntime_float16.h>
#include <sqlite3.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace {
using namespace drummer;
using namespace drummer::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Half:onnxruntime_float16::Float16Impl<Half>{
    explicit Half(float value){val=ToUint16Impl(value);}
    float value()const{return ToFloatImpl();}
};
// Deliberately use READONLY rather than the catalog's writer-capable adapter.
struct Reader {
    sqlite3* handle=nullptr;
    explicit Reader(const juce::File& file){
        if(sqlite3_open_v2(file.getFullPathName().toRawUTF8(),&handle,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX,nullptr)!=SQLITE_OK){
            const std::string error=handle?sqlite3_errmsg(handle):"Cannot open catalog";
            if(handle)sqlite3_close(handle);handle=nullptr;throw std::runtime_error(error);
        }
        sqlite3_busy_timeout(handle,5000);
        if(sqlite3_exec(handle,"BEGIN",nullptr,nullptr,nullptr)!=SQLITE_OK){const std::string error=sqlite3_errmsg(handle);sqlite3_close(handle);handle=nullptr;throw std::runtime_error(error);}
    }
    ~Reader(){if(handle)sqlite3_close(handle);}
};
struct Query {
    Reader& db;sqlite3_stmt* handle=nullptr;
    Query(Reader& owner,const char* sql):db(owner){if(sqlite3_prepare_v2(db.handle,sql,-1,&handle,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db.handle));}
    ~Query(){sqlite3_finalize(handle);}
    void bind(int at,const juce::String& value){if(sqlite3_bind_text(handle,at,value.toRawUTF8(),value.getNumBytesAsUTF8(),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db.handle));}
    bool row(){const int result=sqlite3_step(handle);if(result!=SQLITE_ROW&&result!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db.handle));return result==SQLITE_ROW;}
    juce::String text(int at)const{return juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(handle,at)),sqlite3_column_bytes(handle,at));}
    TextVector vector(int at)const{require(sqlite3_column_bytes(handle,at)==sizeof(TextVector),"Invalid committed vector size");TextVector value;std::memcpy(value.data(),sqlite3_column_blob(handle,at),sizeof(value));return value;}
};
TextVector normalized(TextVector value){double norm=0;for(float v:value){require(std::isfinite(v),"Nonfinite vector");norm+=double(v)*v;}require(norm>0,"Zero vector");for(float& v:value)v=float(v/std::sqrt(norm));return value;}
int64_t rankScore(double value){return (int64_t)std::llround(value*10000000);}
bool better(const VectorMatch& a,const VectorMatch& b){const auto x=rankScore(a.score),y=rankScore(b.score);return x!=y?x>y:a.row<b.row;}
juce::String orderHash(const std::vector<VectorMatch>& matches,size_t count,const std::vector<juce::String>& ids){juce::StringArray result;for(size_t i=0;i<std::min(count,matches.size());++i)result.add(ids[matches[i].row]);return juce::SHA256(result.joinIntoString("\n").toUTF8()).toHexString();}
juce::var compare(const std::vector<VectorMatch>& expected,const std::vector<VectorMatch>& actual,int limit){
    const auto count=std::min(size_t(limit),expected.size());std::set<size_t> wanted;for(size_t i=0;i<count;++i)wanted.insert(expected[i].row);
    int overlap=0;bool exact=actual.size()>=count;
    for(size_t i=0;i<std::min(count,actual.size());++i){overlap+=wanted.contains(actual[i].row);exact=exact&&actual[i].row==expected[i].row;}
    return object({{"limit",limit},{"compared",(int)count},{"overlap",overlap},{"recall",count?double(overlap)/count:1.},{"identicalOrder",exact}});
}
}
int main(int argc,char** argv){try{
    using namespace drummer;using namespace drummer::json;
    require(argc==4||argc==5,"Usage: drummer-vector-storage CATALOG QUERY_JSON OUTPUT_JSON [ASSETS]");
    const juce::File catalog(juce::String::fromUTF8(argv[1])),queriesFile(juce::String::fromUTF8(argv[2])),output(juce::String::fromUTF8(argv[3]));
    require(catalog.existsAsFile()&&queriesFile.existsAsFile(),"Catalog or query suite is missing");
    require(output!=catalog&&output!=queriesFile,"Output must differ from input files");
    const auto suite=juce::JSON::parse(queriesFile.loadFileAsString()),cases=get(suite,"queries");require(cases.isArray()&&cases.size()>0&&cases.size()<=128,"Expected one to 128 query cases");
    // Independent known IEEE-754 binary16 values check the conversion path.
    require(Half(1).val==0x3c00&&Half(-.5f).val==0xb800&&Half(std::ldexp(1.f,-24)).val==1,"Binary16 conversion control failed");
    const double begin=juce::Time::getMillisecondCounterHiRes();Reader db(catalog);
    Query index(db,"SELECT id,model,groups,preprocessing FROM neural_indexes WHERE state='complete' ORDER BY revision DESC,id LIMIT 1");require(index.row(),"No complete index");
    const auto indexId=index.text(0),modelVersion=index.text(1);const int groups=sqlite3_column_int(index.handle,2);
    require(sqlite3_column_int(index.handle,3)==semanticDescriptionVersion,"Index requires another preprocessing version");
    std::vector<TextVector> matrix;std::vector<juce::String> ids;juce::MemoryBlock identity;
    Query rows(db,"SELECT m.semantic_id,v.vector FROM neural_members m JOIN neural_vectors v ON v.semantic_id=m.semantic_id AND v.model=? WHERE m.index_id=? ORDER BY m.semantic_id");rows.bind(1,modelVersion);rows.bind(2,indexId);
    while(rows.row()){const auto id=rows.text(0);const auto vector=rows.vector(1);ids.push_back(id);matrix.push_back(vector);identity.append(id.toRawUTF8(),id.getNumBytesAsUTF8());identity.append(vector.data(),sizeof(vector));}
    require(matrix.size()==size_t(groups)&&groups>0,"Index row count did not reconcile");
    const auto assets=argc==5?juce::File(juce::String::fromUTF8(argv[4])):neuralAssetsDirectory();auto encoder=TextEmbedding::acquire(assets);
    require(encoder->version()==modelVersion,"Encoder differs from the committed model");
    VectorSearch reference(matrix,false);const double loadMs=juce::Time::getMillisecondCounterHiRes()-begin;
    struct Case{juce::String original,canonical,source;TextVector vector;std::vector<VectorMatch> expected;};std::vector<Case> queries;std::set<juce::String> seen;
    for(const auto& row:*cases.getArray()){
        const auto original=get(row,"query").toString().trim(),canonical=musicalQueryDescription(original);require(original.isNotEmpty(),"Empty query");if(!seen.insert(canonical).second)continue;
        Case item;item.original=original;item.canonical=canonical;
        const auto queryId=juce::SHA256(("semantic-"+juce::String(semanticDescriptionVersion)+":query-"+juce::String(musicalQueryVersion)+":"+canonical).toUTF8()).toHexString();
        Query cached(db,"SELECT vector FROM neural_queries WHERE id=? AND model=?");cached.bind(1,queryId);cached.bind(2,modelVersion);
        if(cached.row()){item.vector=cached.vector(0);item.source="committed";}else{item.vector=encoder->encode({canonical})[0];item.source="native CPU, not cached";}
        item.expected=reference.search(item.vector,128);queries.push_back(std::move(item));
    }
    juce::Array<juce::var> formats;
    for(const auto* format:{"float32-control","float16-l2","int8-per-row-l2"}){
        const double conversionStart=juce::Time::getMillisecondCounterHiRes();std::vector<TextVector> decoded;decoded.reserve(matrix.size());
        for(const auto& row:matrix){auto value=row;
            if(juce::String(format)=="float16-l2"){for(float& v:value)v=Half(v).value();value=normalized(value);}
            else if(juce::String(format)=="int8-per-row-l2"){float peak=0;for(float v:row)peak=std::max(peak,std::abs(v));const float scale=peak/127;require(scale>0,"Invalid int8 scale");for(float& v:value)v=float(std::clamp(std::lround(v/scale),-127L,127L))*scale;value=normalized(value);}
            decoded.push_back(value);
        }
        const double conversionMs=juce::Time::getMillisecondCounterHiRes()-conversionStart;VectorSearch search(std::move(decoded),false);juce::Array<juce::var> measurements;
        for(const auto& item:queries){
            const double queryStart=juce::Time::getMillisecondCounterHiRes();const auto result=search.search(item.vector,512);const double queryMs=juce::Time::getMillisecondCounterHiRes()-queryStart;
            double error=0,best=-2,approximateBest=-2;std::vector<double> exactScores,approximateScores;exactScores.reserve(matrix.size());approximateScores.reserve(matrix.size());
            for(size_t i=0;i<matrix.size();++i){const double exact=VectorSearch::similarity(matrix[i],item.vector),approximate=VectorSearch::similarity(search.vectors()[i],item.vector);error=std::max(error,std::abs(exact-approximate));best=std::max(best,exact);approximateBest=std::max(approximateBest,approximate);exactScores.push_back(exact);approximateScores.push_back(approximate);}
            const double floor=std::max(.5,best-.12),approximateFloor=std::max(.5,approximateBest-.12);int flips=0;
            for(size_t i=0;i<matrix.size();++i)flips+=(exactScores[i]>=floor)!=(approximateScores[i]>=approximateFloor);
            juce::Array<juce::var> fidelities,rerank;
            for(int k:{5,20,128})fidelities.add(compare(item.expected,result,k));
            for(int budget:{128,256,512}){auto shortlist=result;shortlist.resize(std::min(shortlist.size(),size_t(budget)));for(auto& hit:shortlist)hit.score=VectorSearch::similarity(matrix[hit.row],item.vector);std::sort(shortlist.begin(),shortlist.end(),better);auto metric=compare(item.expected,shortlist,128);set(metric,"shortlist",budget);rerank.add(metric);}
            if(juce::String(format)=="float32-control")require((bool)get(fidelities[2],"identicalOrder")&&error==0&&flips==0,"Float32 ranking control failed");
            measurements.add(object({{"query",item.original},{"canonicalQuery",item.canonical},{"queryVectorSource",item.source},{"queryVectorSha256",juce::SHA256(juce::MemoryBlock(item.vector.data(),sizeof(TextVector))).toHexString()},{"canonicalOrderSha256",orderHash(item.expected,128,ids)},{"decodedOrderSha256",orderHash(result,128,ids)},{"maxScoreError",error},{"rawAffinityThresholdFlips",flips},{"search512Milliseconds",queryMs},{"fidelity",fidelities},{"canonicalRerank",rerank}}));
        }
        const int width=juce::String(format)=="float32-control"?4:juce::String(format)=="float16-l2"?2:1;
        const int scaleBytes=width==1?4:0;
        formats.add(object({{"format",format},{"encodedVectorBytes",(juce::int64)matrix.size()*(textEmbeddingDimensions*width+scaleBytes)},{"decodedMatrixBytes",(juce::int64)(matrix.size()*sizeof(TextVector))},{"conversionMilliseconds",conversionMs},{"queries",measurements}}));
    }
    const char* architecture=
#if defined(__aarch64__) || defined(__arm64__)
        "arm64";
#elif defined(__x86_64__)
        "x86_64";
#else
        "other";
#endif
    int translated=0;
#if defined(__APPLE__)
    size_t translatedBytes=sizeof(translated);sysctlbyname("sysctl.proc_translated",&translated,&translatedBytes,nullptr,0);
#endif
    const auto report=object({{"state","complete"},{"architecture",architecture},{"translated",translated!=0},{"indexId",indexId},{"modelVersion",modelVersion},{"preprocessingVersion",semanticDescriptionVersion},{"queryVersion",musicalQueryVersion},{"groups",groups},{"dimensions",textEmbeddingDimensions},{"matrixAndOrderSha256",juce::SHA256(identity).toHexString()},{"querySuiteSha256",juce::SHA256(queriesFile.loadFileAsString().toUTF8()).toHexString()},{"catalogAndModelLoadMilliseconds",loadMs},{"formats",formats},
        {"scope","Read-only committed semantic vectors. Full-catalog vector-level ranking against canonical native CPU scores; no rhythm, meter, role or diversity filters and no human relevance judgment. Library rows are quantized, decoded to float32 and L2-normalized; query vectors remain canonical. Encoded byte counts exclude metadata, index and checksum overhead. Decoded resident matrix size is unchanged. Search timings use the same float32 CPU kernel, not a low-precision GPU kernel. Threshold flips are an unfiltered .5/best-minus-.12 probe, not actual family membership. Canonical reranking retains access to original float32 rows; no storage format or catalog data is changed."}});
    require(output.getParentDirectory().createDirectory().wasOk()&&output.replaceWithText(juce::JSON::toString(report)),"Cannot write report");
    std::cout<<juce::JSON::toString(object({{"state","complete"},{"report",output.getFullPathName()},{"groups",groups},{"queries",(int)queries.size()}}),true)<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
