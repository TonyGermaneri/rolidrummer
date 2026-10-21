#include <drummer/PatternLibrary.h>
#include <drummer/PatternAnalysis.h>
#include <drummer/MidiImport.h>
#include <drummer/NeuralIndex.h>
#include <drummer/PatternFamilies.h>
#include <drummer/Json.h>
#include <juce_cryptography/juce_cryptography.h>
#include "LibrarySqlite.h"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <vector>
#include <array>
#include <cstring>
#include <archive.h>
#include <archive_entry.h>

namespace drummer {
using namespace json;
namespace {
juce::String digest(const juce::String& text) { return juce::SHA256(text.toUTF8()).toHexString(); }
juce::String serialise(const juce::var& value) { return juce::JSON::toString(value, true); }
bool eligible(const juce::String& path) { return path.endsWithIgnoreCase(".mid") || path.endsWithIgnoreCase(".midi"); }
bool metadata(const juce::String& path) {
    const auto parts = juce::StringArray::fromTokens(path.replace("\\", "/"), "/", "");
    for (const auto& part : parts) if (part == "__MACOSX" || part.startsWith("._")) return true;
    return false;
}
juce::String mapCacheKey(const juce::String& path, const juce::var& policy) {
    // Matches analyzePattern's conservative automatic policy without making
    // every filename a separate parsing cache entry.
    const auto selected = selectPatternMapping(path, policy);
    auto key = serialise(selected);
    if (get(selected, "id", "auto").toString() == "auto") {
        const auto words = " " + describePattern(path) + " ";
        const bool instrument = words.contains("africa ") || words.contains("asia ") || words.contains("middle east ") || words.contains("south america ");
        key += instrument ? ":instrument" : ":gm";
        if(patternUsesVendorArticulations(words))key+=":vendor";
    }
    return digest(key + ":midi-"+juce::String(midiImportVersion)+":maps-"+juce::String(drumMappingVersion)+":features-" + juce::String(patternFeatureVersion));
}
juce::var runStatus(sql::Database& db, const juce::String& run) {
    sql::Statement query(db, "SELECT id,source_id,state,next_entry,total,midi_files,accepted,ignored,unsupported,metadata_files,error FROM runs WHERE id=?");
    query.text(1, run);
    if (!query.row()) return {};
    return object({{"runId", query.text(0)}, {"sourceId", query.text(1)}, {"state", query.text(2)}, {"scanned", (juce::int64) query.integer(3)},
                   {"total", (juce::int64) query.integer(4)}, {"midiFiles", (juce::int64) query.integer(5)}, {"accepted", (juce::int64) query.integer(6)},
                   {"ignored", (juce::int64) query.integer(7)}, {"unsupported", (juce::int64) query.integer(8)}, {"metadataFiles", (juce::int64) query.integer(9)}, {"error", query.text(10)}});
}
void setRunState(sql::Database& db, const juce::String& run, const juce::String& state, const juce::String& error = {}) {
    sql::Statement update(db, "UPDATE runs SET state=?,error=? WHERE id=?");
    update.text(1, state); update.text(2, error); update.text(3, run); update.done();
}
struct Entry {
    juce::String path;
    int64_t size = 0;
    juce::File file;
    bool regular = true;
};
int64_t zipEntryCount(const juce::File& file, const std::function<bool()>& cancelled) {
    juce::FileInputStream source(file);
    juce::BufferedInputStream stream(source, 262144);
    const int64_t tailAt = std::max(int64_t(0), stream.getTotalLength() - 65557);
    stream.setPosition(tailAt);
    juce::MemoryBlock tail;
    stream.readIntoMemoryBlock(tail);
    const auto* bytes = static_cast<const unsigned char*>(tail.getData());
    for (int64_t i = (int64_t) tail.getSize() - 22; i >= 0; --i) {
        const auto* end = bytes + i;
        if (juce::ByteOrder::littleEndianInt(end) != 0x06054b50 || i + 22 + juce::ByteOrder::littleEndianShort(end + 20) != (int64_t) tail.getSize()) continue;
        if (juce::ByteOrder::littleEndianShort(end + 4) || juce::ByteOrder::littleEndianShort(end + 6)) throw std::runtime_error("Split ZIP archives are not supported");
        int64_t size = juce::ByteOrder::littleEndianInt(end + 12), offset = juce::ByteOrder::littleEndianInt(end + 16);
        if (size == 0xffffffff || offset == 0xffffffff) {
            std::array<unsigned char, 56> record {};
            stream.setPosition(tailAt + i - 20);
            if (stream.read(record.data(), 20) != 20 || juce::ByteOrder::littleEndianInt(record.data()) != 0x07064b50) throw std::runtime_error("Missing ZIP64 directory locator");
            const auto location = (int64_t) juce::ByteOrder::littleEndianInt64(record.data() + 8);
            if (location < 0 || location > stream.getTotalLength() - 56) throw std::runtime_error("Invalid ZIP64 directory offset");
            stream.setPosition(location);
            if (stream.read(record.data(), 56) != 56 || juce::ByteOrder::littleEndianInt(record.data()) != 0x06064b50) throw std::runtime_error("Invalid ZIP64 directory");
            size = (int64_t) juce::ByteOrder::littleEndianInt64(record.data() + 40);
            offset = (int64_t) juce::ByteOrder::littleEndianInt64(record.data() + 48);
        }
        if (size < 0 || offset < 0 || offset > stream.getTotalLength() - size) throw std::runtime_error("Invalid ZIP directory bounds");
        // Some legacy writers wrap the 16-bit file count instead of emitting a
        // ZIP64 count. Count actual central records within the declared bounds.
        int64_t at = offset, total = 0;
        std::array<unsigned char, 46> header {};
        while (at < offset + size) {
            if (cancelled && cancelled()) throw std::runtime_error("Import cancelled during discovery");
            stream.setPosition(at);
            if (stream.read(header.data(), 46) != 46 || juce::ByteOrder::littleEndianInt(header.data()) != 0x02014b50) throw std::runtime_error("Invalid ZIP directory record");
            at += 46 + juce::ByteOrder::littleEndianShort(header.data() + 28) + juce::ByteOrder::littleEndianShort(header.data() + 30) + juce::ByteOrder::littleEndianShort(header.data() + 32);
            if (++total > 20000000 || at > offset + size) throw std::runtime_error("ZIP directory exceeds supported limits");
        }
        return total;
    }
    throw std::runtime_error("Archive has no valid ZIP directory");
}
struct CancellableStream : juce::InputStream {
    juce::FileInputStream stream;
    juce::BufferedInputStream buffer;
    const std::function<bool()>& cancelled;
    CancellableStream(const juce::File& file, const std::function<bool()>& flag) : stream(file), buffer(stream, 262144), cancelled(flag) {}
    juce::int64 getTotalLength() override { return buffer.getTotalLength(); }
    juce::int64 getPosition() override { return buffer.getPosition(); }
    bool setPosition(juce::int64 position) override { return buffer.setPosition(position); }
    bool isExhausted() override { return buffer.isExhausted(); }
    int read(void* destination, int count) override {
        if (cancelled && cancelled()) throw std::runtime_error("Import cancelled during discovery");
        return buffer.read(destination, count);
    }
};
struct Source {
    std::unique_ptr<archive, decltype(&archive_read_free)> zip {nullptr, archive_read_free};
    std::vector<Entry> entries;
    juce::String fingerprint;
    int64_t total = 0, position = -1;
    std::function<bool()> cancelled;
    explicit Source(const juce::File& input, const std::function<bool()>& cancellation) : cancelled(cancellation) {
        if (input.isDirectory()) {
            juce::String inventory;
            for (const auto& item : juce::RangedDirectoryIterator(input, true, "*", juce::File::findFiles, juce::File::FollowSymlinks::no)) {
                if (cancelled && cancelled()) throw std::runtime_error("Import cancelled during discovery");
                const auto child = item.getFile();
                const auto relative = child.getRelativePathFrom(input).replace("\\", "/");
                if (!eligible(relative)) continue;
                entries.push_back({relative, child.getSize(), child});
            }
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
            for (const auto& entry : entries) inventory += entry.path + ":" + juce::String(entry.size) + ":" + juce::String(entry.file.getLastModificationTime().toMilliseconds()) + "\n";
            fingerprint = digest(inventory);
            total = (int64_t) entries.size();
        } else {
            if (!input.existsAsFile() || !input.hasFileExtension("zip")) throw std::runtime_error("Choose a ZIP archive or a library directory");
            CancellableStream stream(input, cancelled);
            if (!stream.stream.openedOk()) throw std::runtime_error("Cannot open pattern archive");
            fingerprint = juce::SHA256(stream).toHexString();
            total = zipEntryCount(input, cancelled);
            zip.reset(archive_read_new());
            archive_read_support_format_zip_streamable(zip.get());
            // Enumerate AppleDouble entries explicitly so checkpoints and the
            // complete archive inventory use the same ordinal/count contract.
            if (archive_read_set_format_option(zip.get(), "zip", "mac-ext", nullptr) != ARCHIVE_OK) throw std::runtime_error("Cannot configure complete ZIP enumeration");
            if (archive_read_open_filename(zip.get(), input.getFullPathName().toRawUTF8(), 65536) != ARCHIVE_OK) throw std::runtime_error("Cannot open ZIP archive");
        }
    }
    Entry entry(int64_t index) {
        if (!zip) return entries.at((size_t) index);
        archive_entry* header = nullptr;
        while (position < index) {
            if (cancelled && cancelled()) throw std::runtime_error("Import cancelled while resuming");
            const int status = archive_read_next_header(zip.get(), &header);
            if (status != ARCHIVE_OK && status != ARCHIVE_WARN) throw std::runtime_error(archive_error_string(zip.get()) ? archive_error_string(zip.get()) : "Archive ended before its declared entry count");
            ++position;
        }
        const char* name = archive_entry_pathname_utf8(header);
        if (!name) name = archive_entry_pathname(header);
        if (!name) throw std::runtime_error("Archive entry has no filename");
        const auto type = archive_entry_filetype(header);
        return {juce::String::createStringFromData(name, (int) std::strlen(name)), archive_entry_size(header), {}, type == AE_IFREG || type == 0};
    }
    void verifyEnd() {
        if (!zip) return;
        archive_entry* entry = nullptr;
        if (archive_read_next_header(zip.get(), &entry) != ARCHIVE_EOF) throw std::runtime_error("ZIP entry inventory does not match its central directory");
    }
    juce::MemoryBlock read(const Entry& entry) {
        if (!entry.regular) throw std::runtime_error("MIDI entry must be a regular file");
        if (entry.size > 32 * 1024 * 1024) throw std::runtime_error("MIDI entry exceeds 32 MB");
        juce::MemoryBlock bytes;
        if (zip) {
            // Streaming ZIPs may put CRC and sizes in a trailing descriptor.
            // Read until the entry ends, enforcing the same bounded MIDI limit.
            juce::MemoryOutputStream output;
            std::array<char, 16384> buffer {};
            for (;;) {
                const auto count = archive_read_data(zip.get(), buffer.data(), buffer.size());
                if (count == 0) break;
                if (count < 0) throw std::runtime_error("Pattern entry checksum or compressed data is invalid");
                if (output.getDataSize() + (size_t) count > 32 * 1024 * 1024) throw std::runtime_error("MIDI entry exceeds 32 MB");
                output.write(buffer.data(), (size_t) count);
            }
            bytes = output.getMemoryBlock();
            if (entry.size > 0 && (int64_t) bytes.getSize() != entry.size) throw std::runtime_error("Pattern entry size is invalid");
        } else {
            if (entry.size <= 0) throw std::runtime_error("MIDI entry is empty");
            bytes.setSize((size_t) entry.size);
            auto input = entry.file.createInputStream();
            if (!input || input->read(bytes.getData(), (int) bytes.getSize()) != (int) bytes.getSize() || !input->isExhausted()) throw std::runtime_error("Pattern entry size changed or is invalid");
        }
        if (bytes.getSize() < 4 || std::memcmp(bytes.getData(), "MThd", 4) != 0) throw std::runtime_error("Entry does not contain a standard MIDI header");
        return bytes;
    }
};
}

PatternLibrary::PatternLibrary(juce::File file) : database(std::move(file)) {
    if (!database.getParentDirectory().createDirectory()) throw std::runtime_error("Cannot create pattern catalog directory");
    sql::Database db(database);
    if(sql::schemaReady(db,"patterns",2)){
        sql::Statement version(db,"SELECT value FROM library_meta WHERE key='schema'");
        if(!version.row()||version.integer(0)!=1)throw std::runtime_error("Unsupported pattern catalog version");
        sql::Statement mode(db,"PRAGMA journal_mode");if(mode.row()&&mode.text(0)=="wal")return;
    }
    db.exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;"
            "CREATE TABLE IF NOT EXISTS library_meta(key TEXT PRIMARY KEY,value INTEGER NOT NULL);"
            "INSERT OR IGNORE INTO library_meta VALUES('schema',1),('revision',0),('patterns',0);"
            "CREATE TABLE IF NOT EXISTS sources(id TEXT PRIMARY KEY,path TEXT NOT NULL,current_run TEXT,last_run TEXT);"
            "CREATE TABLE IF NOT EXISTS runs(id TEXT PRIMARY KEY,source_id TEXT NOT NULL,fingerprint TEXT NOT NULL,mapping TEXT NOT NULL,state TEXT NOT NULL,"
            "next_entry INTEGER NOT NULL DEFAULT 0,total INTEGER NOT NULL,midi_files INTEGER NOT NULL DEFAULT 0,accepted INTEGER NOT NULL DEFAULT 0,"
            "ignored INTEGER NOT NULL DEFAULT 0,unsupported INTEGER NOT NULL DEFAULT 0,metadata_files INTEGER NOT NULL DEFAULT 0,error TEXT NOT NULL DEFAULT '',mapping_version INTEGER NOT NULL DEFAULT 2);"
            "CREATE TABLE IF NOT EXISTS import_leases(source_id TEXT PRIMARY KEY,owner TEXT NOT NULL,expires INTEGER NOT NULL);"
            "CREATE TABLE IF NOT EXISTS payloads(id TEXT PRIMARY KEY,data BLOB NOT NULL);"
            "CREATE TABLE IF NOT EXISTS parse_cache(payload_id TEXT NOT NULL,map_key TEXT NOT NULL,result TEXT NOT NULL,PRIMARY KEY(payload_id,map_key));"
            "CREATE TABLE IF NOT EXISTS patterns(id TEXT PRIMARY KEY,payload_id TEXT NOT NULL,channel INTEGER NOT NULL,track INTEGER NOT NULL,mapping TEXT NOT NULL,"
            "length REAL NOT NULL,numerator INTEGER NOT NULL,denominator INTEGER NOT NULL,density REAL NOT NULL,velocity REAL NOT NULL,coverage REAL NOT NULL,features TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS descriptions(id TEXT PRIMARY KEY,text TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS entries(run_id TEXT NOT NULL,ordinal INTEGER NOT NULL,path TEXT NOT NULL,error TEXT NOT NULL DEFAULT '',PRIMARY KEY(run_id,ordinal));"
            "CREATE TABLE IF NOT EXISTS bindings(id TEXT PRIMARY KEY,run_id TEXT NOT NULL,ordinal INTEGER NOT NULL,pattern_id TEXT NOT NULL,description_id TEXT NOT NULL,family TEXT NOT NULL,role TEXT NOT NULL,payload_id TEXT NOT NULL,channel INTEGER NOT NULL,track INTEGER NOT NULL,mapping TEXT NOT NULL);"
            "CREATE INDEX IF NOT EXISTS binding_run ON bindings(run_id);"
            "CREATE INDEX IF NOT EXISTS runs_source ON runs(source_id);"
            "CREATE INDEX IF NOT EXISTS binding_pattern ON bindings(pattern_id);"
            "CREATE INDEX IF NOT EXISTS binding_description ON bindings(description_id);"
            "CREATE INDEX IF NOT EXISTS pattern_meter ON patterns(numerator,denominator);");
    sql::Statement version(db, "SELECT value FROM library_meta WHERE key='schema'");
    if (!version.row() || version.integer(0) != 1) throw std::runtime_error("Unsupported pattern catalog version");
    version.reset();
    sql::Transaction migration(db);
    bool lastRun = false;
    { sql::Statement columns(db, "PRAGMA table_info(sources)"); while (columns.row()) lastRun = lastRun || columns.text(1) == "last_run"; }
    if (!lastRun) db.exec("ALTER TABLE sources ADD COLUMN last_run TEXT");
    bool mappingVersion=false;
    {sql::Statement columns(db,"PRAGMA table_info(runs)");while(columns.row())mappingVersion=mappingVersion||columns.text(1)=="mapping_version";}
    if(!mappingVersion)db.exec("ALTER TABLE runs ADD COLUMN mapping_version INTEGER NOT NULL DEFAULT 1");
    bool importVersion=false;
    {sql::Statement columns(db,"PRAGMA table_info(runs)");while(columns.row())importVersion=importVersion||columns.text(1)=="import_version";}
    if(!importVersion)db.exec("ALTER TABLE runs ADD COLUMN import_version INTEGER NOT NULL DEFAULT 2");
    sql::sealSchema(db,"patterns",2);
    migration.commit();
}

juce::var PatternLibrary::importSource(const juce::File& input, const LibraryImportOptions& options) const {
    validatePatternMapping(options.mapping);
    if (options.progress) options.progress(object({{"state", "discovering"}, {"path", input.getFullPathName()}}));
    Source source(input, options.cancelled);
    const auto sourceId = digest(input.getFullPathName());
    const auto policy = options.mapping.isObject() ? options.mapping.clone() : object({{"id", "auto"}});
    const auto runId = digest(sourceId + source.fingerprint + serialise(policy) + ":archive-3:midi-" + juce::String(midiImportVersion) + ":maps-"+juce::String(drumMappingVersion)+":" + juce::String(patternFeatureVersion));
    const auto owner = juce::Uuid().toString();
    sql::Database db(database);
    {
        sql::Transaction transaction(db,options.cancelled,[&]{if(options.progress)options.progress(object({{"state","waiting"},{"path",input.getFullPathName()}}));});
        sql::Statement lease(db, "INSERT INTO import_leases VALUES(?,?,?) ON CONFLICT(source_id) DO UPDATE SET owner=excluded.owner,expires=excluded.expires WHERE import_leases.expires<?");
        const auto now = juce::Time::currentTimeMillis();
        lease.text(1, sourceId); lease.text(2, owner); lease.integer(3, now + 120000); lease.integer(4, now); lease.done();
        if (!sqlite3_changes(db.handle)) throw std::runtime_error("This source is being indexed by another instance");
        sql::Statement insertSource(db, "INSERT INTO sources(id,path,last_run) VALUES(?,?,?) ON CONFLICT(id) DO UPDATE SET path=excluded.path,last_run=excluded.last_run");
        insertSource.text(1, sourceId); insertSource.text(2, input.getFullPathName()); insertSource.text(3, runId); insertSource.done();
        sql::Statement insertRun(db, "INSERT OR IGNORE INTO runs(id,source_id,fingerprint,mapping,state,total,mapping_version,import_version) VALUES(?,?,?,?,'indexing',?,?,?)");
        insertRun.text(1, runId); insertRun.text(2, sourceId); insertRun.text(3, source.fingerprint); insertRun.text(4, serialise(policy)); insertRun.integer(5, source.total);insertRun.integer(6,drumMappingVersion);insertRun.integer(7,midiImportVersion); insertRun.done();
        transaction.commit();
    }
    auto releaseLease = [&] {
        sql::Statement release(db, "DELETE FROM import_leases WHERE source_id=? AND owner=?");
        release.text(1, sourceId); release.text(2, owner); release.done();
    };
    try {
        auto state = runStatus(db, runId);
        if (get(state, "state").toString() == "complete") {
            // A corrected map may deliberately return to an earlier cached
            // revision. Re-publish it instead of leaving the newer map active.
            sql::Transaction transaction(db);
            sql::Statement publish(db, "UPDATE sources SET current_run=? WHERE id=? AND (current_run IS NULL OR current_run<>?)");
            publish.text(1, runId); publish.text(2, sourceId); publish.text(3, runId); publish.done();
            if (sqlite3_changes(db.handle)) db.exec("UPDATE library_meta SET value=value+1 WHERE key='revision';UPDATE library_meta SET value=(SELECT COUNT(DISTINCT b.pattern_id) FROM bindings b JOIN sources s ON s.current_run=b.run_id) WHERE key='patterns';");
            transaction.commit(); releaseLease(); if (options.progress) options.progress(state); return state;
        }
        setRunState(db, runId, "indexing");
        int64_t next = (juce::int64) get(state, "scanned"), midiFiles = (juce::int64) get(state, "midiFiles"), accepted = (juce::int64) get(state, "accepted");
        int64_t ignored = (juce::int64) get(state, "ignored"), unsupported = (juce::int64) get(state, "unsupported"), apple = (juce::int64) get(state, "metadataFiles");
        sql::Statement findCache(db, "SELECT result FROM parse_cache WHERE payload_id=? AND map_key=?");
        sql::Statement putCache(db, "INSERT OR IGNORE INTO parse_cache VALUES(?,?,?)");
        sql::Statement putPayload(db, "INSERT OR IGNORE INTO payloads VALUES(?,?)");
        sql::Statement putPattern(db, "INSERT OR IGNORE INTO patterns VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
        sql::Statement putEntry(db, "INSERT OR REPLACE INTO entries VALUES(?,?,?,?)");
        sql::Statement putDescription(db, "INSERT OR IGNORE INTO descriptions VALUES(?,?)");
        sql::Statement putBinding(db, "INSERT OR REPLACE INTO bindings VALUES(?,?,?,?,?,?,?,?,?,?,?)");
        while (next < source.total && !(options.cancelled && options.cancelled())) {
            sql::Transaction transaction(db);
            const auto end = std::min(source.total, next + std::clamp(options.batchSize, 1, 1024));
            for (; next < end; ++next) {
                if (options.cancelled && options.cancelled()) break;
                const auto entry = source.entry(next);
                if (!eligible(entry.path) || metadata(entry.path)) {
                    ++ignored;
                    if (eligible(entry.path) && metadata(entry.path)) ++apple;
                    continue;
                }
                ++midiFiles;
                juce::String error;
                try {
                    const auto bytes = source.read(entry);
                    const auto payloadId = juce::SHA256(bytes).toHexString();
                    const auto mapKey = mapCacheKey(entry.path, policy);
                    findCache.reset(); findCache.text(1, payloadId); findCache.text(2, mapKey);
                    juce::var cached;
                    if (findCache.row()) cached = juce::JSON::parse(findCache.text(0));
                    // Finish this read before any later write/checkpoint on the connection.
                    findCache.reset();
                    if (!cached.isObject()) {
                        putPayload.reset(); putPayload.text(1, payloadId); putPayload.blob(2, bytes); putPayload.done();
                        juce::MemoryInputStream stream(bytes, false);
                        const auto midi = readMidi(stream);
                        const auto info = inspectMidi(midi);
                        juce::Array<juce::var> selections;
                        const auto channels = get(info, "channels");
                        if (auto* list = channels.getArray()) for (const auto& channel : *list) {
                            auto clip = analyzePattern(importMidi(midi, entry.path, (int) channel), entry.path, policy);
                            const auto id = get(clip, "id").toString();
                            const auto measured = get(clip, "features"), meter = get(info, "meter");
                            putPattern.reset(); putPattern.text(1, id); putPattern.text(2, payloadId); putPattern.integer(3, (int) channel); putPattern.integer(4, -1);
                            putPattern.text(5, serialise(policy)); putPattern.real(6, number(clip, "length", 4));
                            putPattern.integer(7, (int) number(meter, "numerator", 4)); putPattern.integer(8, (int) number(meter, "denominator", 4));
                            putPattern.real(9, number(measured, "density", 0)); putPattern.real(10, number(measured, "velocity", 0));
                            putPattern.real(11, number(get(clip, "mapping"), "coverage", 0)); putPattern.text(12, serialise(measured)); putPattern.done();
                            selections.add(object({{"id", id}, {"channel", channel}}));
                        }
                        if (selections.isEmpty()) throw std::runtime_error("MIDI file has no notes");
                        cached = object({{"selections", selections}, {"metadata", info}});
                        putCache.reset(); putCache.text(1, payloadId); putCache.text(2, mapKey); putCache.text(3, serialise(cached)); putCache.done();
                    }
                    const auto description = describePattern(entry.path, get(cached, "metadata")), descriptionId = digest(description);
                    const auto family = patternFamily(entry.path), padded = " " + description + " ";
                    const juce::String role = padded.contains(" fill ") || padded.contains(" fills ") ? "fill" : padded.contains(" intro ") ? "intro" : padded.contains(" ending ") || padded.contains(" outro ") ? "ending" : padded.contains(" sparse ") || padded.contains(" relaxed ") ? "sparse" : "groove";
                    putDescription.reset(); putDescription.text(1, descriptionId); putDescription.text(2, description); putDescription.done();
                    auto selections = get(cached, "selections");
                    for (const auto& selection : *selections.getArray()) {
                        const auto bindingId = digest(runId + ":" + juce::String(next) + ":" + get(selection, "channel").toString());
                        putBinding.reset(); putBinding.text(1, bindingId); putBinding.text(2, runId); putBinding.integer(3, next); putBinding.text(4, get(selection, "id").toString());
                        putBinding.text(5, descriptionId); putBinding.text(6, family); putBinding.text(7, role);
                        putBinding.text(8, payloadId); putBinding.integer(9, (int) get(selection, "channel")); putBinding.integer(10, -1); putBinding.text(11, serialise(policy)); putBinding.done();
                    }
                    ++accepted;
                } catch (const std::exception& problem) {
                    // Storage failures must not be misreported as bad MIDI files.
                    if (sqlite3_errcode(db.handle) != SQLITE_OK && sqlite3_errcode(db.handle) != SQLITE_ROW && sqlite3_errcode(db.handle) != SQLITE_DONE) throw;
                    error = juce::String(problem.what()).substring(0, 2048); ++unsupported;
                }
                putEntry.reset(); putEntry.text(1, runId); putEntry.integer(2, next); putEntry.text(3, entry.path); putEntry.text(4, error); putEntry.done();
            }
            sql::Statement checkpoint(db, "UPDATE runs SET next_entry=?,midi_files=?,accepted=?,ignored=?,unsupported=?,metadata_files=? WHERE id=?");
            checkpoint.integer(1, next); checkpoint.integer(2, midiFiles); checkpoint.integer(3, accepted); checkpoint.integer(4, ignored); checkpoint.integer(5, unsupported); checkpoint.integer(6, apple); checkpoint.text(7, runId); checkpoint.done();
            sql::Statement heartbeat(db, "UPDATE import_leases SET expires=? WHERE source_id=? AND owner=?");
            heartbeat.integer(1, juce::Time::currentTimeMillis() + 120000); heartbeat.text(2, sourceId); heartbeat.text(3, owner); heartbeat.done();
            if (!sqlite3_changes(db.handle)) throw std::runtime_error("Pattern import lease expired");
            transaction.commit();
            if (options.progress) options.progress(runStatus(db, runId));
        }
        if (next == source.total) {
            source.verifyEnd();
            sql::Transaction transaction(db);
            setRunState(db, runId, "complete");
            sql::Statement publish(db, "UPDATE sources SET current_run=? WHERE id=?"); publish.text(1, runId); publish.text(2, sourceId); publish.done();
            db.exec("UPDATE library_meta SET value=value+1 WHERE key='revision';"
                    "UPDATE library_meta SET value=(SELECT COUNT(DISTINCT b.pattern_id) FROM bindings b JOIN sources s ON s.current_run=b.run_id) WHERE key='patterns';");
            transaction.commit();
        } else setRunState(db, runId, "paused");
        state = runStatus(db, runId); releaseLease();
        if (options.progress) options.progress(state);
        return state;
    } catch (const std::exception& error) {
        if (options.cancelled && options.cancelled()) {
            setRunState(db, runId, "paused"); releaseLease();
            auto state = runStatus(db, runId); if (options.progress) options.progress(state); return state;
        }
        setRunState(db, runId, "failed", error.what()); releaseLease();
        throw;
    }
}

juce::var PatternLibrary::status() const {
    sql::Database db(database);
    juce::var result = object({});
    sql::Statement values(db, "SELECT key,value FROM library_meta");
    while (values.row()) result.getDynamicObject()->setProperty(values.text(0), (juce::int64) values.integer(1));
    sql::Statement sources(db, "SELECT s.id,s.path,s.current_run,r.state,r.next_entry,r.total,r.mapping,COALESCE(l.expires,0),r.unsupported,r.id,r.mapping_version FROM sources s LEFT JOIN runs r ON r.id=COALESCE(s.last_run,s.current_run) LEFT JOIN import_leases l ON l.source_id=s.id ORDER BY s.path");
    juce::Array<juce::var> list;
    while (sources.row()) {
        auto state = sources.text(3);
        if (state == "indexing" && sources.integer(7) < juce::Time::currentTimeMillis()) state = "paused";
        list.add(object({{"id", sources.text(0)}, {"path", sources.text(1)}, {"publishedRun", sources.text(2)}, {"state", state}, {"scanned", (juce::int64) sources.integer(4)}, {"total", (juce::int64) sources.integer(5)}, {"mapping", juce::JSON::parse(sources.text(6))}, {"unsupported", (juce::int64) sources.integer(8)}, {"latestRun", sources.text(9)}, {"mappingVersion",(int)sources.integer(10)},{"needsReimport",sources.integer(10)<drumMappingVersion}}));
    }
    set(result, "sources", list); return result;
}

juce::var PatternLibrary::browse(const juce::var& options) const {
    sql::Database db(database);
    const auto needle = describePattern(get(options, "query").toString()).substring(0, 256);
    const int limit = std::clamp((int) number(options, "limit", 50), 1, 200), offset = std::clamp((int) number(options, "offset", 0), 0, 1000000);
    sql::Statement rows(db, "WITH visible AS (SELECT b.pattern_id,MIN(b.id) AS binding_id FROM bindings b JOIN sources s ON s.current_run=b.run_id JOIN descriptions d ON d.id=b.description_id "
                            "WHERE (?='' OR instr(d.text,?)>0) AND (?='' OR b.role=?) GROUP BY b.pattern_id) "
                            "SELECT b.id,b.pattern_id,e.path,d.text,b.family,b.role,p.length,p.numerator,p.denominator,p.density,p.coverage,p.features "
                            "FROM visible v JOIN bindings b ON b.id=v.binding_id JOIN entries e ON e.run_id=b.run_id AND e.ordinal=b.ordinal "
                            "JOIN descriptions d ON d.id=b.description_id JOIN patterns p ON p.id=b.pattern_id ORDER BY e.path,b.pattern_id LIMIT ? OFFSET ?");
    rows.text(1, needle); rows.text(2, needle); rows.text(3, get(options, "role").toString()); rows.text(4, get(options, "role").toString()); rows.integer(5, limit + 1); rows.integer(6, offset);
    juce::Array<juce::var> result;
    while (rows.row()) result.add(object({{"id", rows.text(0)}, {"patternId", rows.text(1)}, {"path", rows.text(2)}, {"name", rows.text(2).fromLastOccurrenceOf("/", false, false)},
                                       {"description", rows.text(3)}, {"family", rows.text(4)}, {"role", rows.text(5)}, {"length", rows.real(6)},
                                       {"numerator", (int) rows.integer(7)}, {"denominator", (int) rows.integer(8)}, {"density", rows.real(9)}, {"coverage", rows.real(10)}, {"features", juce::JSON::parse(rows.text(11))}}));
    const bool more = result.size() > limit;
    if (more) result.removeLast();
    auto state = status();
    return object({{"rows", result}, {"offset", offset}, {"more", more}, {"revision", get(state, "revision")}, {"patterns", get(state, "patterns")}});
}

struct PatternReader::Impl {
    sql::Database db;
    sql::Statement query;
    explicit Impl(const juce::File& file):db(file),query(db,"SELECT p.id,b.channel,b.track,b.mapping,r.data,e.path,b.family,b.role,completed.mapping_version,p.features,completed.import_version FROM bindings b JOIN runs completed ON completed.id=b.run_id AND completed.state='complete' "
                            "JOIN patterns p ON p.id=b.pattern_id JOIN payloads r ON r.id=b.payload_id JOIN entries e ON e.run_id=b.run_id AND e.ordinal=b.ordinal WHERE b.id=?") {db.exec("BEGIN");}
};
PatternReader::PatternReader(const juce::File& file){PatternLibrary migrated(file);impl=std::make_unique<Impl>(file);}
PatternReader::~PatternReader()=default;
juce::var PatternLibrary::pattern(const juce::String& bindingId) const {return PatternReader(database).pattern(bindingId);}
juce::var PatternReader::pattern(const juce::String& bindingId) {
    auto& query=impl->query;query.reset();
    query.text(1, bindingId);
    if (!query.row()) throw std::runtime_error("Pattern is not in a completed library revision");
    const auto bytes = query.blob(4);
    const auto path = query.text(5);
    juce::MemoryInputStream stream(bytes, false);
    const auto midi=readMidi(stream);
    auto result = analyzePattern(importMidi(midi, path, (int) query.integer(1), (int) query.integer(2),(int)query.integer(10)), path, juce::JSON::parse(query.text(3)),(int)query.integer(8),false);
    if (get(result, "id").toString() != query.text(0)) throw std::runtime_error("Cached pattern identity no longer matches its mapping");
    // Preserve historical IDs/measurements while correcting the playable span
    // of an old part padded by another track. No source reimport is required.
    if(query.integer(10)<midiImportVersion){
        const auto current=importMidi(midi,path,(int)query.integer(1),(int)query.integer(2));
        if(number(current,"length",0)<number(result,"length",0))set(result,"loopLength",get(current,"length"));
    }
    set(result, "family", query.text(6)); set(result, "libraryBinding", bindingId);
    // Recall the committed measurements rather than refitting floating-point
    // summaries on a potentially different CPU. Alias role remains explicit.
    auto measured = juce::JSON::parse(query.text(9)); set(measured, "role", query.text(7));set(result,"features",measured);
    return result;
}

juce::var PatternLibrary::errors(const juce::String& runId, int offset, int limit) const {
    sql::Database db(database);
    sql::Statement query(db, "SELECT path,error FROM entries WHERE run_id=? AND error<>'' ORDER BY ordinal LIMIT ? OFFSET ?");
    query.text(1, runId); query.integer(2, std::clamp(limit, 1, 200)); query.integer(3, std::max(0, offset));
    juce::Array<juce::var> result;
    while (query.row()) result.add(object({{"path", query.text(0)}, {"error", query.text(1)}}));
    return result;
}

LibraryService::LibraryService(const juce::File& file) : catalog(file), neural(std::make_unique<NeuralIndex>(file)),families(std::make_unique<PatternFamilies>(file)), snapshot(object({{"state", "idle"}})) {}
std::shared_ptr<LibraryService> LibraryService::acquire(const juce::File& file) {
    static std::mutex guard;
    static std::map<juce::String, std::weak_ptr<LibraryService>> services;
    std::lock_guard lock(guard);
    auto& slot = services[file.getFullPathName()];
    if (auto service = slot.lock()) return service;
    auto service = std::shared_ptr<LibraryService>(new LibraryService(file)); slot = service;
    const auto status=service->neural->status();
    if(get(status,"readyIndexId").toString().isNotEmpty()){
        // An import that finished without its search index (closed or paused
        // mid-preparation) leaves families waiting on search: resume that first.
        if((bool)get(status,"preparationNeeded")||(bool)get(status,"stale"))service->buildNeuralIndex();
        else if((bool)get(service->families->status(),"stale"))service->refreshFamilies();
    }
    return service;
}
LibraryService::~LibraryService() { cancel(); if (worker.joinable()) worker.join(); }
void LibraryService::publish(const juce::var& value) { std::lock_guard lock(mutex); snapshot = value.clone(); }
juce::var LibraryService::progress() const { std::lock_guard lock(mutex); auto value = snapshot.clone(); set(value, "running", running.load()); return value; }
void LibraryService::start(const juce::File& file, juce::var mapping) {
    validatePatternMapping(mapping);
    launch([this, file, mapping = mapping.clone()] {
        LibraryImportOptions options; options.mapping = mapping;
        options.cancelled = [this] { return cancelRequested.load(); };
        options.progress = [this](const auto& value) { publish(value); };
        catalog.importSource(file, options);
        if (!cancelRequested.load())prepareSearch();
    });
}
void LibraryService::buildNeuralIndex() {
    launch([this] {prepareSearch();});
}
void LibraryService::prepareSearch() {
    NeuralBuildOptions options;options.cancelled=[this]{return cancelRequested.load();};options.progress=[this](const auto& value){publish(value);};
    for(;;){
        if(cancelRequested.load()){publish(object({{"state","paused"},{"stage","search-metadata"}}));return;}
        const auto result=neural->build(options);const auto state=get(result,"state").toString();
        if(state!="busy"&&!(state=="complete"&&(bool)get(neural->status(),"preparationNeeded")))break;
        auto waiting=result.clone();set(waiting,"state","busy");set(waiting,"stage","search-metadata");publish(waiting);
        juce::Thread::sleep(250);
    }
    if(!cancelRequested.load())refreshFamilies();
}
void LibraryService::refreshFamilies(){launch({},true);}
void LibraryService::launch(std::function<void()> operation,bool familyRefresh) {
    std::lock_guard lifecycle(workerMutex);
    if(familyRefresh)familiesRequested=true;
    if(running.load()){if(familyRefresh)return;throw std::runtime_error("A library worker is already running");}
    if (worker.joinable()) worker.join();
    running=true;
    cancelRequested = false;
    publish(object({{"state", "queued"}}));
    try {
        worker = std::thread([this, operation = std::move(operation)] {
            bool successful=true;int contended=0;
            try { if(operation)operation(); }
            catch (const std::exception& error) {successful=false;publish(object({{"state", cancelRequested.load() ? "paused" : "failed"}, {"error", error.what()}})); }
            for(;;){
                {std::lock_guard lock(workerMutex);if(!successful||cancelRequested.load()||!familiesRequested){running=false;return;}familiesRequested=false;}
                try{
                    NeuralBuildOptions options;options.cancelled=[this]{return cancelRequested.load();};options.progress=[this](const auto& value){publish(value);};
                    const auto result=families->reconcile(*neural,options);const auto state=get(result,"state").toString();
                    if(state=="obsolete"||state=="busy"){
                        if(state=="busy"){publish(result);juce::Thread::sleep(250);}
                        std::lock_guard lock(workerMutex);familiesRequested=true;
                    }
                }catch(const std::exception& error){
                    // Another instance holding the catalog's write lock past the busy
                    // timeout is transient: wait and match again rather than stop.
                    const juce::String message=error.what();
                    if(!cancelRequested.load()&&(message.contains("database is locked")||message.contains("database table is locked"))&&++contended<=30){
                        publish(object({{"state","busy"},{"stage","families"},{"error",""}}));juce::Thread::sleep(1000);std::lock_guard lock(workerMutex);familiesRequested=true;continue;
                    }
                    successful=false;publish(object({{"state",cancelRequested.load()?"paused":"failed"},{"stage","families"},{"error",error.what()}}));
                }
            }
        });
    } catch (...) { running = false; throw; }
}
}
