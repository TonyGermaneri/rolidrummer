#include <drummer/NeuralIndex.h>
#include <drummer/VectorSearch.h>
#include <drummer/PatternAnalysis.h>
#include <drummer/Json.h>
#include <juce_cryptography/juce_cryptography.h>
#include "LibrarySqlite.h"
#include "PartQuery.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace drummer {
using namespace json;
namespace {
juce::String digest(const juce::String& value) { return juce::SHA256(value.toUTF8()).toHexString(); }
juce::var indexStatus(sql::Database& db, const juce::String& id) {
    sql::Statement row(db, "SELECT id,revision,model,state,stage,descriptions,total_descriptions,groups,completed,error FROM neural_indexes WHERE id=?");
    row.text(1, id);
    if (!row.row()) return object({{"state", "missing"}});
    return object({{"indexId", row.text(0)}, {"revision", (juce::int64) row.integer(1)}, {"modelVersion", row.text(2)}, {"state", row.text(3)},
                   {"stage", row.text(4)}, {"descriptions", (juce::int64) row.integer(5)}, {"totalDescriptions", (juce::int64) row.integer(6)},
                   {"groups", (juce::int64) row.integer(7)}, {"completed", (juce::int64) row.integer(8)}, {"error", row.text(9)}});
}
void state(sql::Database& db, const juce::String& id, const char* value, const juce::String& error = {}) {
    sql::Statement update(db, "UPDATE neural_indexes SET state=?,error=? WHERE id=?");
    update.text(1, value); update.text(2, error); update.text(3, id); update.done();
}
struct Snapshot {
    juce::String id, model;
    int64_t revision = 0;
    std::vector<juce::String> ids, descriptions;
    std::unique_ptr<VectorSearch> vectors;
    double preparationMilliseconds=0,rowReadMilliseconds=0,vectorPreparationMilliseconds=0;
    bool packedCacheHit=false;
    juce::String cacheError;
};
// A disposable, sequential representation of the immutable description matrix.
// Source vectors remain authoritative. The format uses little-endian scalars
// and a checksum so the same cache is safe across both macOS architectures.
constexpr int snapshotFormat=1,snapshotMagic=0x534e4452,maxCachedGroups=262144;
constexpr int64_t maxSnapshotBytes=512ll*1024*1024;
struct SnapshotRows {
    std::vector<juce::String> ids,descriptions;
    std::vector<TextVector> matrix;
};
juce::String snapshotDigest(const juce::MemoryBlock& bytes) {return juce::SHA256(bytes).toHexString();}
juce::MemoryBlock packSnapshot(const SnapshotRows& rows,const juce::String& id,const juce::String& model) {
    juce::MemoryOutputStream out;
    out.writeInt(snapshotMagic);out.writeInt(snapshotFormat);out.writeInt(textEmbeddingDimensions);out.writeInt((int)rows.ids.size());
    auto string=[&](const juce::String& value){const auto bytes=value.getNumBytesAsUTF8();out.writeInt(bytes);out.write(value.toRawUTF8(),(size_t)bytes);};
    string(id);string(model);
    for(size_t i=0;i<rows.ids.size();++i){
        string(rows.ids[i]);string(rows.descriptions[i]);
        for(const auto value:rows.matrix[i])out.writeFloat(value);
    }
    return out.getMemoryBlock();
}
SnapshotRows unpackSnapshot(const juce::MemoryBlock& bytes,int64_t expected,const juce::String& indexId,const juce::String& model) {
    juce::MemoryInputStream in(bytes,false);
    auto require=[&](bool condition){if(!condition)throw std::runtime_error("Invalid packed neural snapshot");};
    require(bytes.getSize()>=16&&in.readInt()==snapshotMagic&&in.readInt()==snapshotFormat&&in.readInt()==textEmbeddingDimensions);
    const auto count=in.readInt();require(count>0&&count<=maxCachedGroups&&count==expected);
    // Reject a forged row count before reserving proportional memory.
    require((uint64_t)count*(sizeof(TextVector)+8+64)<=bytes.getSize()-16);
    auto string=[&](int maximum){
        require(in.getNumBytesRemaining()>=4);const auto size=in.readInt();
        require(size>0&&size<=maximum&&in.getNumBytesRemaining()>=size);
        const auto* data=static_cast<const char*>(bytes.getData())+in.getPosition();
        require(juce::CharPointer_UTF8::isValidString(data,size));
        auto value=juce::String::fromUTF8(data,size);require(value.getNumBytesAsUTF8()==size);
        in.skipNextBytes(size);return value;
    };
    require(string(64)==indexId&&string(1024)==model);
    SnapshotRows result;result.ids.reserve((size_t)count);result.descriptions.reserve((size_t)count);result.matrix.reserve((size_t)count);
    for(int i=0;i<count;++i){
        auto id=string(64),description=string(65536);
        require(id.length()==64&&id.containsOnly("0123456789abcdef")&&(result.ids.empty()||result.ids.back()<id));
        require(in.getNumBytesRemaining()>=(int64_t)sizeof(TextVector));
        TextVector vector;double norm=0;
        for(auto& value:vector){value=in.readFloat();require(std::isfinite(value));norm+=double(value)*value;}
        require(std::abs(norm-1)<=.001);
        result.ids.push_back(std::move(id));result.descriptions.push_back(std::move(description));result.matrix.push_back(vector);
    }
    require(in.getNumBytesRemaining()==0);return result;
}
std::optional<SnapshotRows> readPackedSnapshot(sql::Database& db,const juce::String& id,const juce::String& model,int64_t groups,juce::String& error) {
    sql::Statement row(db,"SELECT format,model,groups,checksum,length(payload),payload FROM neural_snapshots WHERE index_id=?");row.text(1,id);
    if(!row.row())return {};
    try{
        if(row.integer(0)!=snapshotFormat||row.text(1)!=model||row.integer(2)!=groups||groups<=0||groups>maxCachedGroups||row.integer(4)<16||row.integer(4)>maxSnapshotBytes)
            throw std::runtime_error("Packed neural snapshot metadata mismatch");
        const auto bytes=row.blob(5);
        if(snapshotDigest(bytes)!=row.text(3))throw std::runtime_error("Packed neural snapshot checksum mismatch");
        return unpackSnapshot(bytes,groups,id,model);
    }catch(const std::exception& exception){error=exception.what();return {};}
}
std::optional<SnapshotRows> readSnapshotRows(sql::Database& db,const juce::String& id,const juce::String& model,const std::function<bool()>& cancelled={}) {
    sql::Statement rows(db,"SELECT m.semantic_id,t.text,v.vector FROM neural_members m JOIN neural_text t ON t.id=m.semantic_id JOIN neural_vectors v ON v.semantic_id=m.semantic_id AND v.model=? WHERE m.index_id=? ORDER BY m.semantic_id");
    rows.text(1,model);rows.text(2,id);SnapshotRows result;
    while(rows.row()){
        if(result.ids.size()%256==0&&cancelled&&cancelled())return {};
        const auto blob=rows.blob(2);
        if(blob.getSize()!=sizeof(TextVector))throw std::runtime_error("Invalid cached text-vector size");
        TextVector vector;std::memcpy(vector.data(),blob.getData(),sizeof(vector));
        result.matrix.push_back(vector);result.ids.push_back(rows.text(0));result.descriptions.push_back(rows.text(1));
    }
    return result;
}
void writePackedSnapshot(sql::Database& db,const juce::String& id,const juce::String& model,const SnapshotRows& rows) {
    if(rows.ids.empty()||rows.ids.size()>maxCachedGroups)return;
    uint64_t size=24+id.getNumBytesAsUTF8()+model.getNumBytesAsUTF8();
    for(size_t i=0;i<rows.ids.size();++i){size+=8+sizeof(TextVector)+rows.ids[i].getNumBytesAsUTF8()+rows.descriptions[i].getNumBytesAsUTF8();if(size>(uint64_t)maxSnapshotBytes)return;}
    const auto bytes=packSnapshot(rows,id,model);if(bytes.getSize()>(uint64_t)maxSnapshotBytes)return;
    sql::Transaction transaction(db);
    sql::Statement put(db,"INSERT OR REPLACE INTO neural_snapshots VALUES(?,?,?,?,?,?)");
    put.text(1,id);put.integer(2,snapshotFormat);put.text(3,model);put.integer(4,(int64_t)rows.ids.size());put.text(5,snapshotDigest(bytes));put.blob(6,bytes);put.done();
    // Bound disk growth while preserving historical recall through source rows.
    sql::Statement prune(db,"DELETE FROM neural_snapshots WHERE index_id<>? AND index_id NOT IN (SELECT id FROM neural_indexes WHERE state='complete' AND preprocessing=? ORDER BY revision DESC,id LIMIT 2)");
    prune.text(1,id);prune.integer(2,semanticDescriptionVersion);prune.done();transaction.commit();
}
bool preparePackedSnapshot(sql::Database& db,const juce::String& id,const juce::String& model,int64_t groups,const NeuralBuildOptions& options) {
    if(groups<=0||groups>maxCachedGroups)return true;
    if(options.cancelled&&options.cancelled())return false;
    juce::String error;if(readPackedSnapshot(db,id,model,groups,error))return true;
    auto rows=readSnapshotRows(db,id,model,options.cancelled);if(!rows||(options.cancelled&&options.cancelled()))return false;
    if((int64_t)rows->ids.size()!=groups)throw std::runtime_error("Neural snapshot inventory did not reconcile");
    writePackedSnapshot(db,id,model,*rows);return true;
}
bool hasCoveringIndex(sql::Database& db,const char* table,const char* name,std::initializer_list<const char*> expected) {
    bool full=false;
    {sql::Statement list(db,(juce::String("PRAGMA index_list(")+table+")").toRawUTF8());while(list.row())if(list.text(1)==name)full=list.integer(4)==0;}
    if(!full)return false;
    sql::Statement columns(db,(juce::String("PRAGMA index_info(")+name+")").toRawUTF8());
    auto next=expected.begin();while(columns.row()){if(next==expected.end()||columns.text(2)!=*next++)return false;}
    return next==expected.end();
}
bool hasSearchMetadata(sql::Database& db) {
    return hasCoveringIndex(db,"patterns","pattern_search_scalars",{"id","coverage","numerator","denominator","density","length"})
        &&hasCoveringIndex(db,"bindings","neural_binding_scalars",{"description_id","run_id","pattern_id","role","id"});
}
bool prepareSearchMetadata(sql::Database& db,const NeuralBuildOptions& options,bool& deferred) {
    deferred=false;
    if(hasSearchMetadata(db))return true;
    auto stop=[&]{return options.cancelled&&options.cancelled();};
    for(int index=0;index<2;++index){
        if(stop())return false;
        if(index==0?hasCoveringIndex(db,"patterns","pattern_search_scalars",{"id","coverage","numerator","denominator","density","length"}):hasCoveringIndex(db,"bindings","neural_binding_scalars",{"description_id","run_id","pattern_id","role","id"}))continue;
        try{
            sql::Transaction transaction(db,options.cancelled);
            // Inspect leases under the same write admission as CREATE INDEX.
            // Existing imports/family builders retain short write batches;
            // their final indexing pass can prepare these optional indexes.
            bool active=false;
            {sql::Statement imports(db,"SELECT 1 FROM import_leases WHERE expires>? LIMIT 1");imports.integer(1,juce::Time::currentTimeMillis());active=imports.row();}
            {sql::Statement table(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='family_lease'");
                if(table.row()){sql::Statement family(db,"SELECT 1 FROM family_lease WHERE expires>? LIMIT 1");family.integer(1,juce::Time::currentTimeMillis());active=active||family.row();}}
            if(active){transaction.commit();deferred=true;return true;}
            struct Cancellation {
                sql::Database& db;const NeuralBuildOptions& options;
                Cancellation(sql::Database& connection,const NeuralBuildOptions& value):db(connection),options(value){sqlite3_progress_handler(db.handle,2048,[](void* context)noexcept{try{auto& self=*static_cast<Cancellation*>(context);return self.options.cancelled&&self.options.cancelled()?1:0;}catch(...){return 1;}},this);}
                void clear(){sqlite3_progress_handler(db.handle,0,nullptr,nullptr);}
                ~Cancellation(){clear();}
            } cancellation(db,options);
            db.exec(index==0?"CREATE INDEX IF NOT EXISTS pattern_search_scalars ON patterns(id,coverage,numerator,denominator,density,length)":"CREATE INDEX IF NOT EXISTS neural_binding_scalars ON bindings(description_id,run_id,pattern_id,role,id)");
            cancellation.clear();transaction.commit();
        }catch(const std::exception&){if(stop())return false;throw;}
    }
    if(!hasSearchMetadata(db))throw std::runtime_error("A catalog search index has an incompatible definition");
    return true;
}
struct Lease {
    sql::Database& db;
    juce::String owner = juce::Uuid().toString();
    bool acquired=false;
    explicit Lease(sql::Database& database) : db(database) {
        try{
        sql::Statement row(db, "INSERT INTO neural_lease VALUES(1,?,?) ON CONFLICT(id) DO UPDATE SET owner=excluded.owner,expires=excluded.expires WHERE neural_lease.expires<?");
        const auto now = juce::Time::currentTimeMillis();
        row.text(1, owner); row.integer(2, now + 600000); row.integer(3, now); row.done();
        acquired=sqlite3_changes(db.handle)>0;
        }catch(const std::exception&){if(sqlite3_errcode(db.handle)!=SQLITE_BUSY&&sqlite3_errcode(db.handle)!=SQLITE_LOCKED)throw;}
    }
    void heartbeat() {
        sql::Statement row(db, "UPDATE neural_lease SET expires=? WHERE id=1 AND owner=?");
        row.integer(1, juce::Time::currentTimeMillis() + 600000); row.text(2, owner); row.done();
        if (!sqlite3_changes(db.handle)) throw std::runtime_error("Neural index lease expired");
    }
    ~Lease() { if(acquired)try { sql::Statement row(db, "DELETE FROM neural_lease WHERE owner=?"); row.text(1, owner); row.done(); } catch (...) {} }
};
}

juce::String semanticDescription(const juce::String& input) {
    // Preserve musical language; remove export products and opaque ordinal IDs.
    // Meter/tempo and actual part coverage are structured compatibility fields.
    auto value = input.toLowerCase().substring(0, 4096).toStdString();
    static const std::regex exports(R"(superior drummer [0-9]* drum midi|gm midi pack|groove monkee|studio drummer midi files|reason drum kits refill|reason nn-xt kits|session drummer|preview files|single track|multi[ -]track|type 0|groove clips|audioinst [0-9]*|notecount[ -]*[0-9 -]*|\.(sng|prt|lib)\b)");
    static const std::regex ordinal(R"(\b(rock|metal|funk|jazz|blues|punk|rb)[0-9]+\b)");
    value = std::regex_replace(value, exports, " ");
    value = std::regex_replace(value, ordinal, "$1");
    auto text = juce::String::fromUTF8(value.c_str()).replace("16ths", "sixteenth").replace("16th", "sixteenth")
        .replace("32nds", "thirty second").replace("32nd", "thirty second").replace("8ths", "eighth").replace("8th", "eighth")
        .replace("r&b", "rhythm blues").replace("hihat", "hi hat");
    static const std::set<juce::String> noise {"gm", "midi", "pack", "superior", "drummer", "drumkit", "ezx", "ezxdrumkit", "dfh", "ssd", "drum", "drums", "patterns",
        "mor", "live", "sd", "ls", "ext", "ga", "imap", "fpc", "session", "reason", "kits", "kit", "fullkit", "studio", "files", "file", "lib", "sng", "prt",
        "audioinst", "marker", "track", "notecount", "theme", "variation", "beat", "song", "bpm", "of", "from", "the", "type", "a", "b", "c", "d", "e", "f", "l", "r"};
    static const std::map<juce::String, juce::String> spellings {{"rb", "rhythm blues"}, {"rnb", "rhythm blues"}, {"fills", "fill"}, {"grooves", "groove"},
        {"intros", "intro"}, {"shuffles", "shuffle"}, {"qtr", "quarter"}, {"hats", "hat"}};
    juce::String word;
    juce::StringArray words;
    auto append = [&] {
        if (word.isEmpty()) return;
        bool digit = false;
        for (auto c : word) digit = digit || juce::CharacterFunctions::isDigit(c);
        if (!digit && !noise.contains(word)) {
            if (auto it = spellings.find(word); it != spellings.end()) word = it->second;
            words.add(word);
        }
        word.clear();
    };
    for (auto c : text) {
        if (juce::CharacterFunctions::isLetter(c) || juce::CharacterFunctions::isDigit(c)) word += juce::String::charToString(c);
        else append();
    }
    append();
    // Later path components carry the specific phrase: "Rock / Hard Rock"
    // should retain "Hard Rock", rather than reorder it into "Rock Hard".
    std::set<juce::String> seen;
    juce::StringArray unique;
    for (int i = words.size() - 1; i >= 0 && unique.size() < 64; --i) if (seen.insert(words[i]).second) unique.insert(0, words[i]);
    unique.add("drum patterns");
    return unique.joinIntoString(" ");
}

juce::String musicalQueryDescription(const juce::String& input) {
    // Domain vocabulary, not a pattern preset. The original requested title
    // stays visible and saved; every result still comes from vector retrieval.
    // MiniLM otherwise favors underspecified "rock" over the library's "hard
    // rock" term for "heavy rock", as measured on the complete personal corpus.
    return (" " + semanticDescription(input) + " ").replace(" heavy rock ", " hard rock ").trim();
}

struct NeuralIndex::Impl {
    juce::File database, assets;
    mutable std::mutex mutex;
    mutable std::shared_ptr<TextEmbedding> embedding;
    mutable double embeddingAcquireMilliseconds=0;
    mutable juce::var searchMetrics;
    mutable std::shared_ptr<Snapshot> snapshot;
    std::shared_ptr<TextEmbedding> model() const {
        std::lock_guard lock(mutex);
        if (!embedding) {const auto begin=juce::Time::getMillisecondCounterHiRes();embedding = TextEmbedding::acquire(assets);embeddingAcquireMilliseconds=juce::Time::getMillisecondCounterHiRes()-begin;}
        return embedding;
    }
    TextVector queryVector(const std::shared_ptr<Snapshot>& snapshot,const juce::String& normalizedQuery) const {
        const auto encoder = model();
        if (encoder->version() != snapshot->model) throw std::runtime_error("Neural index uses a different model; rebuild before searching");
        const auto queryId = digest("semantic-" + juce::String(semanticDescriptionVersion) + ":query-" + juce::String(musicalQueryVersion) + ":" + normalizedQuery);
        TextVector encodedQuery;
        {
            sql::Database cache(database);
            auto read = [&] {
                sql::Statement row(cache, "SELECT vector FROM neural_queries WHERE id=? AND model=?");
                row.text(1, queryId); row.text(2, snapshot->model);
                if (!row.row()) return false;
                const auto bytes = row.blob(0);
                if (bytes.getSize() != sizeof(TextVector)) throw std::runtime_error("Invalid cached query-vector size");
                std::memcpy(encodedQuery.data(), bytes.getData(), sizeof(encodedQuery)); return true;
            };
            if (!read()) {
                const auto vector = encoder->encode({normalizedQuery})[0];
                sql::Statement put(cache, "INSERT OR IGNORE INTO neural_queries VALUES(?,?,?,?)");
                put.text(1, queryId); put.text(2, snapshot->model); put.text(3, normalizedQuery); put.blob(4, juce::MemoryBlock(vector.data(), sizeof(vector))); put.done();
                // Read the first committed vector, including when another native
                // instance/CPU architecture won the insertion race. Recall/search
                // never re-encodes an already committed query with a new reduction.
                if (!read()) throw std::runtime_error("The committed query embedding is missing");
            }
        }
        return encodedQuery;
    }
    std::shared_ptr<Snapshot> load(const juce::String& wanted, bool gpu) const {
        const auto begin=juce::Time::getMillisecondCounterHiRes();
        sql::Database db(database);
        sql::Statement latest(db, wanted.isEmpty() ? "SELECT id,revision,model,groups FROM neural_indexes WHERE state='complete' AND preprocessing=? ORDER BY revision DESC,id LIMIT 1" : "SELECT id,revision,model,groups FROM neural_indexes WHERE state='complete' AND preprocessing=? AND id=?");
        latest.integer(1, semanticDescriptionVersion);
        if (wanted.isNotEmpty()) latest.text(2, wanted);
        if (!latest.row()) throw std::runtime_error("Build a neural index before searching this library");
        const auto id = latest.text(0), version = latest.text(2);
        const auto revision = latest.integer(1),groups=latest.integer(3);
        latest.reset();
        const auto key = database.getFullPathName() + ":" + id + (gpu ? ":gpu" : ":cpu");
        static std::mutex poolMutex;
        static std::map<juce::String, std::weak_ptr<Snapshot>> pool;
        std::lock_guard poolLock(poolMutex);
        if (auto found = pool[key].lock()) return found;
        auto result = std::make_shared<Snapshot>(); result->id = id; result->revision = revision; result->model = version;
        const auto rowBegin=juce::Time::getMillisecondCounterHiRes();
        auto rows=readPackedSnapshot(db,id,version,groups,result->cacheError);
        result->packedCacheHit=bool(rows);
        if(!rows){
            rows=readSnapshotRows(db,id,version);
            if((int64_t)rows->ids.size()!=groups)throw std::runtime_error("Neural snapshot inventory did not reconcile");
            // Cache publication is optional for a search. A competing writer or
            // unavailable disk must not discard valid authoritative vectors.
            try{writePackedSnapshot(db,id,version,*rows);}catch(const std::exception& error){result->cacheError=error.what();}
        }
        if (rows->matrix.empty()) throw std::runtime_error("The neural index contains no searchable descriptions");
        result->ids=std::move(rows->ids);result->descriptions=std::move(rows->descriptions);
        result->rowReadMilliseconds=juce::Time::getMillisecondCounterHiRes()-rowBegin;
        const auto vectorBegin=juce::Time::getMillisecondCounterHiRes();
        result->vectors = std::make_unique<VectorSearch>(std::move(rows->matrix), gpu);
        result->vectorPreparationMilliseconds=juce::Time::getMillisecondCounterHiRes()-vectorBegin;
        result->preparationMilliseconds=juce::Time::getMillisecondCounterHiRes()-begin;
        pool[key] = result; return result;
    }
};

NeuralIndex::NeuralIndex(juce::File database, juce::File assets) : impl(std::make_unique<Impl>()) {
    impl->database = std::move(database); impl->assets = std::move(assets);
    sql::Database db(impl->database);
    if(sql::schemaReady(db,"neural"))return;
    db.exec("CREATE TABLE IF NOT EXISTS neural_indexes(id TEXT PRIMARY KEY,revision INTEGER NOT NULL,model TEXT NOT NULL,preprocessing INTEGER NOT NULL,state TEXT NOT NULL,stage TEXT NOT NULL DEFAULT 'descriptions',descriptions INTEGER NOT NULL DEFAULT 0,total_descriptions INTEGER NOT NULL DEFAULT 0,last_description TEXT NOT NULL DEFAULT '',groups INTEGER NOT NULL DEFAULT 0,completed INTEGER NOT NULL DEFAULT 0,error TEXT NOT NULL DEFAULT '');"
            "CREATE TABLE IF NOT EXISTS neural_sources(index_id TEXT NOT NULL,source_id TEXT NOT NULL,run_id TEXT NOT NULL,PRIMARY KEY(index_id,source_id));"
            "CREATE TABLE IF NOT EXISTS neural_index_descriptions(index_id TEXT NOT NULL,description_id TEXT NOT NULL,PRIMARY KEY(index_id,description_id));"
            "CREATE INDEX IF NOT EXISTS neural_source_runs ON neural_sources(run_id,index_id);"
            "CREATE TABLE IF NOT EXISTS neural_text(id TEXT PRIMARY KEY,text TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS neural_descriptions(description_id TEXT NOT NULL,version INTEGER NOT NULL,semantic_id TEXT NOT NULL,PRIMARY KEY(description_id,version));"
            "CREATE INDEX IF NOT EXISTS neural_description_group ON neural_descriptions(semantic_id,version,description_id);"
            "CREATE TABLE IF NOT EXISTS neural_members(index_id TEXT NOT NULL,semantic_id TEXT NOT NULL,PRIMARY KEY(index_id,semantic_id));"
            "CREATE TABLE IF NOT EXISTS neural_vectors(semantic_id TEXT NOT NULL,model TEXT NOT NULL,vector BLOB NOT NULL,PRIMARY KEY(semantic_id,model));"
            "CREATE TABLE IF NOT EXISTS neural_queries(id TEXT NOT NULL,model TEXT NOT NULL,text TEXT NOT NULL,vector BLOB NOT NULL,PRIMARY KEY(id,model));"
            "CREATE TABLE IF NOT EXISTS neural_snapshots(index_id TEXT PRIMARY KEY,format INTEGER NOT NULL,model TEXT NOT NULL,groups INTEGER NOT NULL,checksum TEXT NOT NULL,payload BLOB NOT NULL);"
            "CREATE TABLE IF NOT EXISTS neural_lease(id INTEGER PRIMARY KEY,owner TEXT NOT NULL,expires INTEGER NOT NULL);");
    sql::Transaction schema(db);sql::sealSchema(db,"neural");schema.commit();
}
NeuralIndex::~NeuralIndex() = default;

juce::var NeuralIndex::build(const NeuralBuildOptions& options) {
    const auto model = impl->model();
    const auto version = model->version();
    sql::Database db(impl->database);
    Lease lease(db);
    if(!lease.acquired){auto busy=status();set(busy,"state","busy");set(busy,"stage","search-metadata");if(options.progress)options.progress(busy);return busy;}
    std::vector<std::pair<juce::String, juce::String>> sources;
    int64_t revision = 0;
    {
        db.exec("BEGIN");
        sql::Statement meta(db, "SELECT value FROM library_meta WHERE key='revision'"); if (meta.row()) revision = meta.integer(0);
        sql::Statement rows(db, "SELECT id,current_run FROM sources WHERE current_run IS NOT NULL ORDER BY id");
        while (rows.row()) sources.emplace_back(rows.text(0), rows.text(1));
        db.exec("COMMIT");
    }
    if (sources.empty()) throw std::runtime_error("Import a pattern library before building the neural index");
    juce::String identity = version + ":semantic-" + juce::String(semanticDescriptionVersion) + ":revision-" + juce::String(revision);
    for (const auto& [source, run] : sources) identity += ":" + source + ":" + run;
    const auto id = digest(identity);
    auto progress = [&] { const auto result = indexStatus(db, id); if (options.progress) options.progress(result); return result; };
    if (get(indexStatus(db, id), "state").toString() == "complete") {
        const auto status=indexStatus(db,id);
        // Backfill older complete indexes on the indexing worker, without
        // scanning their unchanged bindings or changing immutable identities.
        auto preparing=status.clone();set(preparing,"state","building");set(preparing,"stage","search-metadata");if(options.progress)options.progress(preparing);
        bool deferred=false;
        if(!prepareSearchMetadata(db,options,deferred)){
            set(preparing,"state","paused");if(options.progress)options.progress(preparing);return preparing;
        }
        if(deferred){set(preparing,"state","busy");if(options.progress)options.progress(preparing);return preparing;}
        set(preparing,"stage","snapshot");if(options.progress)options.progress(preparing);
        if(!preparePackedSnapshot(db,id,version,(juce::int64)get(status,"groups"),options)){
            auto paused=status.clone();set(paused,"state","paused");set(paused,"stage","snapshot");if(options.progress)options.progress(paused);return paused;
        }
        return progress();
    }
    {
        sql::Transaction transaction(db);
        sql::Statement create(db, "INSERT OR IGNORE INTO neural_indexes(id,revision,model,preprocessing,state) VALUES(?,?,?,?,'building')");
        create.text(1, id); create.integer(2, revision); create.text(3, version); create.integer(4, semanticDescriptionVersion); create.done();
        sql::Statement put(db, "INSERT OR IGNORE INTO neural_sources VALUES(?,?,?)");
        for (const auto& [source, run] : sources) { put.reset(); put.text(1, id); put.text(2, source); put.text(3, run); put.done(); }
        sql::Statement descriptions(db, "INSERT OR IGNORE INTO neural_index_descriptions SELECT ?,b.description_id FROM bindings b JOIN neural_sources s ON s.run_id=b.run_id WHERE s.index_id=? GROUP BY b.description_id");
        descriptions.text(1, id); descriptions.text(2, id); descriptions.done();
        transaction.commit();
    }
    state(db, id, "building");
    try {
        sql::Statement count(db, "SELECT COUNT(*) FROM neural_index_descriptions WHERE index_id=?");
        count.text(1, id); count.row(); const auto total = count.integer(0); count.reset();
        sql::Statement setTotal(db, "UPDATE neural_indexes SET total_descriptions=? WHERE id=?"); setTotal.integer(1, total); setTotal.text(2, id); setTotal.done();
        progress();
        for (;;) {
            if (options.cancelled && options.cancelled()) { state(db, id, "paused"); return progress(); }
            juce::String after;
            { sql::Statement row(db, "SELECT last_description FROM neural_indexes WHERE id=?"); row.text(1, id); row.row(); after = row.text(0); }
            std::vector<std::pair<juce::String, juce::String>> descriptions;
            {
                sql::Statement rows(db, "SELECT d.id,d.text FROM neural_index_descriptions i JOIN descriptions d ON d.id=i.description_id WHERE i.index_id=? AND i.description_id>? ORDER BY i.description_id LIMIT 512");
                rows.text(1, id); rows.text(2, after);
                while (rows.row()) descriptions.emplace_back(rows.text(0), rows.text(1));
            }
            if (descriptions.empty()) break;
            sql::Transaction transaction(db);
            sql::Statement lookup(db, "SELECT semantic_id FROM neural_descriptions WHERE description_id=? AND version=?");
            sql::Statement putText(db, "INSERT OR IGNORE INTO neural_text VALUES(?,?)");
            sql::Statement putDescription(db, "INSERT OR IGNORE INTO neural_descriptions VALUES(?,?,?)");
            sql::Statement putMember(db, "INSERT OR IGNORE INTO neural_members VALUES(?,?)");
            for (const auto& [descriptionId, text] : descriptions) {
                juce::String group;
                lookup.reset(); lookup.text(1, descriptionId); lookup.integer(2, semanticDescriptionVersion);
                if (lookup.row()) group = lookup.text(0);
                lookup.reset();
                if (group.isEmpty()) {
                    const auto normalized = semanticDescription(text);
                    group = digest("semantic-" + juce::String(semanticDescriptionVersion) + ":" + normalized);
                    putText.reset(); putText.text(1, group); putText.text(2, normalized); putText.done();
                    putDescription.reset(); putDescription.text(1, descriptionId); putDescription.integer(2, semanticDescriptionVersion); putDescription.text(3, group); putDescription.done();
                }
                putMember.reset(); putMember.text(1, id); putMember.text(2, group); putMember.done();
            }
            sql::Statement update(db, "UPDATE neural_indexes SET last_description=?,descriptions=descriptions+? WHERE id=?");
            update.text(1, descriptions.back().first); update.integer(2, (int64_t) descriptions.size()); update.text(3, id); update.done();
            lease.heartbeat(); transaction.commit(); progress();
        }
        {
            sql::Statement update(db, "UPDATE neural_indexes SET stage='embeddings',groups=(SELECT COUNT(*) FROM neural_members WHERE index_id=?),completed=(SELECT COUNT(*) FROM neural_members m JOIN neural_vectors v ON v.semantic_id=m.semantic_id AND v.model=? WHERE m.index_id=?) WHERE id=?");
            update.text(1, id); update.text(2, version); update.text(3, id); update.text(4, id); update.done();
        }
        progress();
        for (;;) {
            if (options.cancelled && options.cancelled()) { state(db, id, "paused"); return progress(); }
            std::vector<juce::String> ids, texts;
            {
                sql::Statement rows(db, "SELECT m.semantic_id,t.text FROM neural_members m JOIN neural_text t ON t.id=m.semantic_id LEFT JOIN neural_vectors v ON v.semantic_id=m.semantic_id AND v.model=? WHERE m.index_id=? AND v.semantic_id IS NULL ORDER BY m.semantic_id LIMIT 64");
                rows.text(1, version); rows.text(2, id);
                while (rows.row()) { ids.push_back(rows.text(0)); texts.push_back(rows.text(1)); }
            }
            if (ids.empty()) break;
            const auto vectors = model->encode(texts);
            sql::Transaction transaction(db);
            sql::Statement put(db, "INSERT OR IGNORE INTO neural_vectors VALUES(?,?,?)");
            for (size_t i = 0; i < ids.size(); ++i) { put.reset(); put.text(1, ids[i]); put.text(2, version); put.blob(3, juce::MemoryBlock(vectors[i].data(), sizeof(TextVector))); put.done(); }
            sql::Statement update(db, "UPDATE neural_indexes SET completed=completed+? WHERE id=?"); update.integer(1, (int64_t) ids.size()); update.text(2, id); update.done();
            lease.heartbeat(); transaction.commit(); progress();
        }
        {
            sql::Statement update(db,"UPDATE neural_indexes SET stage='search-metadata' WHERE id=?");update.text(1,id);update.done();
            progress();
            bool deferred=false;
            if(!prepareSearchMetadata(db,options,deferred)){state(db,id,"paused");return progress();}
        }
        {
            sql::Statement update(db,"UPDATE neural_indexes SET stage='snapshot' WHERE id=?");update.text(1,id);update.done();
            progress();
            const auto groups=(juce::int64)get(indexStatus(db,id),"groups");
            if(!preparePackedSnapshot(db,id,version,groups,options)){state(db,id,"paused");return progress();}
        }
        {
            sql::Transaction transaction(db);
            sql::Statement update(db, "UPDATE neural_indexes SET state='complete',stage='ready' WHERE id=? AND groups=completed AND descriptions=total_descriptions");
            update.text(1, id); update.done();
            if (!sqlite3_changes(db.handle)) throw std::runtime_error("Neural index inventory did not reconcile");
            transaction.commit();
        }
        return progress();
    } catch (const std::exception& error) { state(db, id, "failed", error.what()); progress(); throw; }
}

juce::var NeuralIndex::status() const {
    sql::Database db(impl->database);
    sql::Statement current(db, "SELECT value FROM library_meta WHERE key='revision'"); current.row(); const auto revision = current.integer(0);
    sql::Statement latest(db, "SELECT id FROM neural_indexes ORDER BY revision DESC,rowid DESC LIMIT 1");
    auto result = latest.row() ? indexStatus(db, latest.text(0)) : object({{"state", "missing"}});
    const auto reported = get(result, "state").toString();
    if (reported == "building") {
        sql::Statement lease(db, "SELECT expires FROM neural_lease WHERE id=1");
        if (!lease.row() || lease.integer(0) < juce::Time::currentTimeMillis()) set(result, "state", "paused");
    }
    sql::Statement ready(db, "SELECT id,revision FROM neural_indexes WHERE state='complete' AND preprocessing=? ORDER BY revision DESC,id LIMIT 1");
    ready.integer(1, semanticDescriptionVersion);
    if (ready.row()) { set(result, "readyIndexId", ready.text(0)); set(result, "readyRevision", (juce::int64) ready.integer(1)); }
    set(result, "catalogRevision", (juce::int64) revision);
    set(result, "stale", !result.hasProperty("readyRevision") || (juce::int64) get(result, "readyRevision") != revision);
    set(result,"preparationNeeded",result.hasProperty("readyIndexId")&&!hasSearchMetadata(db));
    return result;
}

SemanticAffinities NeuralIndex::affinities(const juce::String& query,const juce::String& id,bool cpu) const {
    const auto snapshot=impl->load(id,!cpu);
    {std::lock_guard lock(impl->mutex);impl->snapshot=snapshot;}
    SemanticAffinities result;result.indexId=snapshot->id;result.modelVersion=snapshot->model;result.revision=snapshot->revision;
    result.canonicalQuery=musicalQueryDescription(query);result.provider=snapshot->vectors->provider();
    const auto vector=impl->queryVector(snapshot,result.canonicalQuery);
    for(const auto& match:snapshot->vectors->search(vector,snapshot->ids.size()))
        result.groups.push_back({snapshot->ids[match.row],snapshot->descriptions[match.row],std::round(match.score*1e7)/1e7});
    return result;
}

juce::var NeuralIndex::resources() const {
    std::lock_guard lock(impl->mutex);
    const auto& snapshot=impl->snapshot;
    return object({{"indexId",snapshot?snapshot->id:juce::String{}},
        {"vectorRows",snapshot?(juce::int64)snapshot->ids.size():0},
        {"matrixBytes",snapshot?(juce::int64)(snapshot->ids.size()*sizeof(TextVector)):0},
        {"provider",snapshot?snapshot->vectors->provider():juce::String{}},
        {"snapshotReferences",(int)impl->snapshot.use_count()},
        {"embeddingReferences",(int)impl->embedding.use_count()},
        {"embeddingLoaded",bool(impl->embedding)},
        {"snapshotPreparationMilliseconds",snapshot?snapshot->preparationMilliseconds:0},
        {"snapshotRowReadMilliseconds",snapshot?snapshot->rowReadMilliseconds:0},
        {"packedSnapshotCacheHit",snapshot?snapshot->packedCacheHit:false},
        {"snapshotCacheError",snapshot?snapshot->cacheError:juce::String{}},
        {"vectorPreparationMilliseconds",snapshot?snapshot->vectorPreparationMilliseconds:0},
        {"embeddingAcquireMilliseconds",impl->embeddingAcquireMilliseconds},
        {"lastSearch",impl->searchMetrics.clone()}});
}

juce::var NeuralIndex::search(const juce::var& options) const {
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto query = get(options, "query").toString().trim().substring(0, 256);
    if (query.isEmpty()) throw std::runtime_error("Enter a musical patch name to search");
    const PartQuery part(options);
    const auto snapshot = impl->load(get(options, "indexId").toString(), !(bool) get(options, "cpu", false));
    { std::lock_guard lock(impl->mutex); impl->snapshot = snapshot; }
    const auto normalizedQuery = musicalQueryDescription(query);
    const auto encodedQuery = impl->queryVector(snapshot,normalizedQuery);
    const auto limit = (int) std::clamp(number(options, "limit", 50), 1.0, 200.0);
    const auto groups = snapshot->vectors->search(encodedQuery, (size_t) std::clamp(number(options, "groups", 128), 1.0, 512.0));
    const int numerator = (int) number(options, "numerator", 0), denominator = (int) number(options, "denominator", 0);
    const auto role = get(options, "role").toString();
    const double coverage = std::clamp(number(options, "minCoverage", .6), 0.0, 1.0);
    const auto candidateBegin=juce::Time::getMillisecondCounterHiRes();
    // Normal browsing uses scalar fields. Part-conditioned retrieval examines
    // committed part measurements before sampling; decode response feature
    // objects only for returned rows.
    struct Candidate { juce::var value; juce::String pattern, group; double score; };
    std::map<juce::String, Candidate> unique;
    sql::Database db(impl->database);
    const bool covering=hasCoveringIndex(db,"patterns","pattern_search_scalars",{"id","coverage","numerator","denominator","density","length"});
    const bool coveringBindings=hasCoveringIndex(db,"bindings","neural_binding_scalars",{"description_id","run_id","pattern_id","role","id"});
    const auto patternTable=covering?"patterns p INDEXED BY pattern_search_scalars":"patterns p";
    const auto bindingTable=coveringBindings?"bindings b INDEXED BY neural_binding_scalars":"bindings b";
    const auto candidateSql=juce::String("WITH eligible AS (SELECT MIN(b.id) AS binding_id,p.id,")+part.density+" AS density"+(part.enabled?",b.role AS part_role ":" ")+
                            "FROM neural_descriptions d JOIN "+bindingTable+" ON b.description_id=d.description_id JOIN neural_sources s ON s.run_id=b.run_id "
                            "JOIN "+patternTable+" ON p.id=b.pattern_id "
                            "WHERE s.index_id=? AND d.semantic_id=? AND d.version=? AND p.coverage>=? AND (?=0 OR p.numerator=?) AND (?=0 OR p.denominator=?) AND (?='' OR b.role=?) "+part.predicate+part.feelPredicate("?11")+
                            " GROUP BY p.id,b.role), ranked AS (SELECT binding_id,ROW_NUMBER() OVER(ORDER BY density,id"+(part.enabled?",part_role":"")+") AS ordinal,COUNT(*) OVER() AS total FROM eligible) "
                            "SELECT b.id,p.id,e.path,b.family,b.role,p.length,p.numerator,p.denominator,p.coverage "
                            "FROM ranked r JOIN bindings b ON b.id=r.binding_id JOIN "+patternTable+" ON p.id=b.pattern_id JOIN entries e ON e.run_id=b.run_id AND e.ordinal=b.ordinal "
                            "WHERE "+(part.enabled?PartQuery::sampleSql("r.ordinal","r.total",64):juce::String("((r.ordinal-1) % MAX(1,(r.total+63)/64))=0 OR r.ordinal=r.total"))+" ORDER BY r.ordinal LIMIT 64";
    sql::Statement rows(db,candidateSql.toRawUTF8());
    for (const auto& group : groups) {
        if (snapshot->descriptions[group.row] == "drum patterns" && normalizedQuery != "drum patterns") continue;
        if (group.score < number(options, "minSimilarity", .25)) continue;
        const int described=PartQuery::describedFeel(snapshot->descriptions[group.row]);
        if(part.enabled&&part.wantedFeel>=0&&described>=0&&described!=part.wantedFeel)continue;
        rows.reset(); rows.text(1, snapshot->id); rows.text(2, snapshot->ids[group.row]); rows.integer(3, semanticDescriptionVersion); rows.real(4, coverage);
        rows.integer(5, numerator); rows.integer(6, numerator); rows.integer(7, denominator); rows.integer(8, denominator); rows.text(9, role); rows.text(10, role);
        if(part.enabled&&part.wantedFeel>=0)rows.integer(11,described);
        while (rows.row()) {
            const auto pattern = rows.text(1), semantic = snapshot->ids[group.row];
            const auto score = group.score + .025 * rows.real(8);
            auto found = unique.find(pattern);
            if (found != unique.end() && (found->second.score > score || (found->second.score == score && get(found->second.value, "id").toString() < rows.text(0)))) continue;
            auto value = object({{"id", rows.text(0)}, {"patternId", pattern}, {"path", rows.text(2)}, {"name", rows.text(2).fromLastOccurrenceOf("/", false, false)},
                                 {"family", rows.text(3)}, {"role", rows.text(4)}, {"length", rows.real(5)}, {"numerator", (int) rows.integer(6)}, {"denominator", (int) rows.integer(7)},
                                 {"coverage", rows.real(8)}, {"semanticDescription", snapshot->descriptions[group.row]}, {"semanticId", semantic},
                                 {"description", snapshot->descriptions[group.row]}, {"similarity", group.score}, {"score", score}});
            unique.insert_or_assign(pattern, Candidate {value, pattern, semantic, score});
        }
    }
    std::vector<Candidate> candidates;
    for (const auto& [id, value] : unique) candidates.push_back(value);
    const auto rankBegin=juce::Time::getMillisecondCounterHiRes();
    juce::Array<juce::var> result;
    std::map<juce::String, int> votes;
    sql::Statement features(db,"SELECT features FROM patterns WHERE id=?");
    while (!candidates.empty() && result.size() < limit) {
        auto best = candidates.begin();
        auto adjusted = [&](const Candidate& value) { return (int64_t) std::llround((value.score - .015 * votes[value.group]) * 10000000); };
        for (auto it = candidates.begin() + 1; it != candidates.end(); ++it) if (adjusted(*it) > adjusted(*best) || (adjusted(*it) == adjusted(*best) && it->pattern < best->pattern)) best = it;
        set(best->value, "rankScore", double(adjusted(*best)) / 10000000);
        // Reading thousands of feature payloads before diversity ranking
        // created substantial cold I/O. Only final returned rows need them.
        features.reset();features.text(1,best->pattern);
        if(!features.row())throw std::runtime_error("A selected pattern's features are missing");
        set(best->value, "features",juce::JSON::parse(features.text(0)));
        ++votes[best->group]; result.add(best->value); candidates.erase(best);
    }
    {std::lock_guard lock(impl->mutex);impl->searchMetrics=object({{"coveringScalarIndex",covering},{"coveringBindingIndex",coveringBindings},{"candidateMilliseconds",rankBegin-candidateBegin},{"rankingAndFeaturesMilliseconds",juce::Time::getMillisecondCounterHiRes()-rankBegin}});}
    return object({{"query", query}, {"interpretedQuery", normalizedQuery}, {"queryVersion", musicalQueryVersion}, {"indexId", snapshot->id}, {"revision", (juce::int64) snapshot->revision}, {"modelVersion", snapshot->model},
                   {"preprocessingVersion", semanticDescriptionVersion}, {"provider", snapshot->vectors->provider()}, {"groupsSearched", (int) groups.size()}, {"rows", result},
                   {"milliseconds", juce::Time::getMillisecondCounterHiRes() - start}});
}
}
