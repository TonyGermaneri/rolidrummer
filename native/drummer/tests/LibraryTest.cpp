#include <drummer/PatternLibrary.h>
#include <drummer/PatternAnalysis.h>
#include <drummer/MidiImport.h>
#include <drummer/Json.h>
#include <juce_cryptography/juce_cryptography.h>
#include "../src/LibrarySqlite.h"
#include <iostream>
#include <future>
#include <stdexcept>

using namespace drummer;
using namespace drummer::json;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
juce::MemoryBlock midiBytes(int note = 42, int ppq = 960, int numerator = 4, int denominator = 4) {
    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::timeSignatureMetaEvent(numerator, denominator), 0);
    track.addEvent(juce::MidiMessage::tempoMetaEvent(500000), 0);
    track.addEvent(juce::MidiMessage::textMetaEvent(3, "Human hats"), 0);
    track.addEvent(juce::MidiMessage::noteOn(10, note, (juce::uint8) 97), ppq * .0125);
    track.addEvent(juce::MidiMessage::noteOff(10, note, (juce::uint8) 64), ppq * .1125);
    track.addEvent(juce::MidiMessage::endOfTrack(), ppq * numerator * 8.0 / denominator);
    juce::MidiFile midi; midi.setTicksPerQuarterNote(ppq); midi.addTrack(track);
    juce::MemoryOutputStream out;
    check(midi.writeTo(out), "Cannot create MIDI fixture");
    return out.getMemoryBlock();
}
juce::var parse(const juce::MemoryBlock& bytes, const juce::String& path = "Hard Rock/Human hats.mid") {
    juce::MemoryInputStream stream(bytes, false);
    return importMidi(readMidi(stream), path);
}
struct Fixture {
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("rd-library-test", "");
    Fixture() { check(root.createDirectory().wasOk(), "Cannot create test directory"); }
    ~Fixture() { root.deleteRecursively(); }
    juce::File archive(const char* name, const std::vector<std::pair<juce::String, juce::MemoryBlock>>& entries) {
        auto file = root.getChildFile(name);
        juce::ZipFile::Builder builder;
        for (const auto& [path, bytes] : entries) builder.addEntry(std::make_unique<juce::MemoryInputStream>(bytes, true), 6, path, juce::Time::getCurrentTime());
        juce::FileOutputStream stream(file);
        check(builder.writeToStream(stream, nullptr), "Cannot write archive fixture"); stream.flush();
        return file;
    }
};
}

int main() { try {
    const auto bytes = midiBytes();
    const auto clip = parse(bytes);
    auto notes = get(clip, "notes");
    check(notes.getArray()->size() == 1, "MIDI note count changed");
    check(std::abs(number((*notes.getArray())[0], "beat", 0) - .0125) < 1e-12, "Human timing changed");
    check(number(clip, "length", 0) == 8, "Trailing silence lost");
    {
        juce::MidiFile midi;midi.setTicksPerQuarterNote(960);
        juce::MidiMessageSequence shortPart,longPart,conductor;
        shortPart.addEvent(juce::MidiMessage::noteOn(10,36,(juce::uint8)100),0);shortPart.addEvent(juce::MidiMessage::noteOff(10,36),120);shortPart.addEvent(juce::MidiMessage::endOfTrack(),3840);
        longPart.addEvent(juce::MidiMessage::noteOn(11,42,(juce::uint8)100),0);longPart.addEvent(juce::MidiMessage::noteOff(11,42),120);longPart.addEvent(juce::MidiMessage::endOfTrack(),7680);
        conductor.addEvent(juce::MidiMessage::tempoMetaEvent(500000),0);conductor.addEvent(juce::MidiMessage::endOfTrack(),15360);
        midi.addTrack(shortPart);midi.addTrack(longPart);midi.addTrack(conductor);
        check(number(importMidi(midi,"Mixed.mid",10),"length",0)==4,"Short part inherited another track's loop length");
        check(number(importMidi(midi,"Mixed.mid",11),"length",0)==8,"Long part lost its own loop length");
        check(number(importMidi(midi,"Mixed.mid",0,0),"length",0)==4,"Selected track inherited unrelated empty bars");
        check(number(importMidi(midi,"Mixed.mid",0),"length",0)==8,"Conductor track padded the combined pattern");
        check(number(importMidi(midi,"Mixed.mid",10,-1,2),"length",0)==16,"Historical MIDI import semantics changed");
        // A historical catalog pins its old parser; importing again publishes
        // corrected lengths without breaking a saved historical binding.
        Fixture f;const auto source=f.root.getChildFile("parts");source.createDirectory();const auto path=source.getChildFile("Mixed.mid");
        juce::MemoryOutputStream output;check(midi.writeTo(output),"Cannot create mixed-loop fixture");const auto payload=output.getMemoryBlock();path.replaceWithData(payload.getData(),payload.getSize());
        const auto database=f.root.getChildFile("historical.sqlite");PatternLibrary originalCatalog(database);
        const auto old=analyzePattern(importMidi(midi,path.getFullPathName(),10,-1,2),path.getFullPathName());
        const auto oldId=get(old,"id").toString(),payloadId=juce::SHA256(payload).toHexString();
        {
            sql::Database writer(database);
            writer.exec("INSERT INTO runs(id,source_id,fingerprint,mapping,state,total,mapping_version,import_version) VALUES('old-run','old-source','old','{}','complete',1,2,2)");
            sql::Statement bytes(writer,"INSERT INTO payloads VALUES(?,?)");bytes.text(1,payloadId);bytes.blob(2,payload);bytes.done();
            sql::Statement pattern(writer,"INSERT INTO patterns VALUES(?,?,10,-1,'{}',16,4,4,0,0,1,?)");pattern.text(1,oldId);pattern.text(2,payloadId);pattern.text(3,juce::JSON::toString(get(old,"features"),true));pattern.done();
            sql::Statement entry(writer,"INSERT INTO entries VALUES('old-run',0,?,'')");entry.text(1,path.getFullPathName());entry.done();
            sql::Statement binding(writer,"INSERT INTO bindings VALUES('old-binding','old-run',0,?,'old-description','','groove',?,10,-1,'{}')");binding.text(1,oldId);binding.text(2,payloadId);binding.done();
            writer.exec("ALTER TABLE runs DROP COLUMN import_version;UPDATE catalog_schema SET version=1 WHERE component='patterns'");
        }
        PatternLibrary catalog(database);
        const auto recalled=catalog.pattern("old-binding");
        check(number(recalled,"length",0)==16&&number(recalled,"loopLength",0)==4,"Historical identity or corrected playable loop was lost");
        catalog.importSource(source);
        bool shortFound=false;const auto currentRows=get(catalog.browse(),"rows");for(const auto& row:*currentRows.getArray()){
            const auto current=catalog.pattern(get(row,"id").toString());
            if((int)get(current,"channel")==10){check(number(current,"length",0)==4,"New catalog import kept the old padded loop");shortFound=true;}
        }
        check(shortFound&&number(catalog.pattern("old-binding"),"length",0)==16,"Reimport invalidated historical bindings or lost the short part");
    }
    auto padded = bytes;
    padded.append("vendor trailer", 14);
    check(get(parse(padded), "notes").getArray()->size() == 1, "Vendor trailer caused a valid SMF track to be rejected");
    auto truncated = bytes; truncated.setSize(bytes.getSize() - 2);
    bool badTrack = false; try { parse(truncated); } catch (...) { badTrack = true; }
    check(badTrack, "Trimming vendor data accepted a truncated MIDI track");
    auto info = get(clip, "metadata");
    check(get(info, "text").getArray()->size() == 1, "Track metadata lost");
    check(number((*get(info, "tempos").getArray())[0], "bpm", 0) == 120, "Tempo metadata lost");
    const auto odd = parse(midiBytes(42, 960, 7, 8));
    check(number(get(get(odd, "metadata"), "meter"), "numerator", 0) == 7 && number(odd, "length", 0) == 7, "Odd meter or loop lost");
    const auto mapped = analyzePattern(clip, "Hard Rock/Hats.mid");
    check(get(mapped, "id") == get(analyzePattern(parse(midiBytes(42, 480)), "Another export/Hats.mid"), "id"), "PPQ or source aliases change musical identity");
    const auto custom = analyzePattern(parse(midiBytes(96)), "Percussion.mid", juce::JSON::parse(R"({"id":"custom","notes":{"96":"timbaleHigh"}})"));
    check(number((*get(custom, "notes").getArray())[0], "note", 0) == 65, "Custom articulation map ignored");
    const auto rules = juce::JSON::parse(R"({"id":"auto","rules":[{"prefix":"World","id":"custom","notes":{"96":"timbaleHigh"}},{"prefix":"World/Timbale","id":"custom","notes":{"96":"cowbell"}}]})");
    validatePatternMapping(rules);
    const auto specific = analyzePattern(parse(midiBytes(96)), "Archive/World/Timbale/Groove.mid", rules);
    check(number((*get(specific, "notes").getArray())[0], "note", 0) == 56, "More specific source map did not override its parent");
    bool badMap = false; try { validatePatternMapping(juce::JSON::parse(R"({"notes":{"96":"unknown-instrument"}})")); } catch (...) { badMap = true; }
    check(badMap, "Invalid role silently accepted in source mapping");
    const auto unknown = analyzePattern(clip, "Africa/Shakere/G11.mid");
    check(get(unknown, "notes").getArray()->isEmpty() && get(unknown, "unmappedNotes").getArray()->size() == 1, "Instrument pack silently guessed a drum map");
    const auto vendor=analyzePattern(parse(midiBytes(63)),"GM Pack/Rock DFH/Hard Rock.mid");
    check(get(vendor,"notes").getArray()->isEmpty()&&get(vendor,"unmappedNotes").getArray()->size()==1,"A vendor articulation was silently treated as a GM conga");
    check(get(analyzePattern(parse(midiBytes(63)),"Rock DFH/Groove.mid",object({{"id","gm"}})),"notes").getArray()->size()==1,"An explicit GM source map was ignored");
    const auto corrected=analyzePattern(parse(midiBytes(63)),"Rock DFH/Groove.mid",juce::JSON::parse(R"({"id":"auto","notes":{"63":"hatClosed"}})"));
    check(number((*get(corrected,"notes").getArray())[0],"note",0)==42,"An explicit articulation correction did not override vendor uncertainty");
    check(get(analyzePattern(parse(midiBytes(63)),"Rock DFH/Groove.mid",{},1),"notes").getArray()->size()==1,"Legacy mapping semantics were changed during recall");
    // Reusing committed measurements must not bypass source normalization or
    // change fingerprints, unknown articulations, timing or mapping metadata.
    for(int version:{1,drumMappingVersion})for(int note:{42,63,96})for(const char* path:{"Hard Rock/Fills/Hats.mid","Rock DFH/Groove.mid","Africa/Shakere/G11.mid"}){
        for(const auto& policy:{juce::var(),juce::JSON::parse(R"({"id":"custom","notes":{"96":"timbaleHigh"}})")}){
            auto raw=parse(midiBytes(note));set(raw,"features",object({{"stale",true}}));
            auto complete=analyzePattern(raw,path,policy,version),recalled=analyzePattern(raw,path,policy,version,false);
            check(get(recalled,"features").isVoid(),"Unmeasured source retained stale features");complete.getDynamicObject()->removeProperty("features");recalled.getDynamicObject()->removeProperty("features");
            check(juce::JSON::toString(complete,true)==juce::JSON::toString(recalled,true),"Committed-feature recall changed source identity or note data");
        }
    }
    const auto description = describePattern("Archive/00071@FullKit/0991@4#4_STRAIGHT_HiHat/EighthNote/Theme-20/Variation-0/Beat-112-0.mid");
    check(description.contains("4 4 straight hihat eighth note") && !description.contains("112") && !description.contains("theme"), "Description normalization lost musical context or retained IDs");
    // A session reuses its query and pins a read view while another connection
    // completes an import. New sessions see the new revision; existing source
    // bindings stay usable, and payload tampering still fails identity checks.
    {
        Fixture f;const auto file=f.root.getChildFile("reader.sqlite"),source=f.root.getChildFile("source");source.createDirectory();
        const auto first=midiBytes(42);source.getChildFile("Groove.mid").replaceWithData(first.getData(),first.getSize());
        PatternLibrary catalog(file);catalog.importSource(source);
        const auto oldRows=get(catalog.browse(),"rows");const auto oldId=get(oldRows[0],"id").toString();
        PatternReader reader(file);const auto original=reader.pattern(oldId);
        const auto path=get(oldRows[0],"path").toString();auto previous=analyzePattern(parse(first,path),path);
        set(previous,"family",get(original,"family"));set(previous,"libraryBinding",oldId);set(previous,"features",get(original,"features"));
        check(juce::JSON::toString(original,true)==juce::JSON::toString(previous,true),"Prepared reader changed serialized pattern bytes");
        check(juce::JSON::toString(original,true)==juce::JSON::toString(catalog.pattern(oldId),true),"Prepared reader changed a complete pattern");
        const auto added=midiBytes(38);source.getChildFile("New.mid").replaceWithData(added.getData(),added.getSize());catalog.importSource(source);
        const auto newRows=get(catalog.browse(),"rows");juce::String newId;
        for(const auto& row:*newRows.getArray())if(get(row,"id").toString()!=oldId)newId=get(row,"id").toString();
        check(newId.isNotEmpty(),"Reader fixture did not publish a new pattern");bool hidden=false;try{reader.pattern(newId);}catch(const std::exception&){hidden=true;}
        check(hidden,"An open read session changed its catalog snapshot");
        check(juce::JSON::toString(original,true)==juce::JSON::toString(reader.pattern(oldId),true),"Import changed a session's historical binding");
        check(get(catalog.pattern(newId),"notes").size()==1,"A fresh reader did not see the completed import");
        {sql::Database writer(file);sql::Statement corrupt(writer,"UPDATE payloads SET data=? WHERE id=(SELECT payload_id FROM bindings WHERE id=?)");corrupt.blob(1,midiBytes(36));corrupt.text(2,oldId);corrupt.done();}
        bool rejected=false;try{catalog.pattern(oldId);}catch(const std::exception&){rejected=true;}
        check(rejected,"Reusing committed features bypassed the source identity check");
    }
    Fixture fixture;
    const auto zip = fixture.archive("library.zip", {{"Root/Hard Rock/Groove.mid", bytes}, {"Root/Hard Rock/Fills/Fill 1.mid", bytes},
        {"__MACOSX/Root/._Groove.mid", bytes}, {"Root/._Groove.mid", bytes}, {"Root/bad.mid", juce::MemoryBlock("CAKEWALK", 8)},
        {"Root/cover.png", bytes}, {"Root/Odd/7-8.mid", midiBytes(36, 960, 7, 8)}});
    const auto db = fixture.root.getChildFile("library.sqlite");
    PatternLibrary library(db);
    LibraryImportOptions options;
    options.batchSize = 2;
    bool cancel = false;
    options.progress = [&](const auto& state) { if (number(state, "scanned", 0) >= 2) cancel = true; };
    options.cancelled = [&] { return cancel; };
    auto paused = library.importSource(zip, options);
    check(get(paused, "state").toString() == "paused" && number(paused, "scanned", 0) == 2, "Import did not checkpoint cancellation");
    check(get(library.browse(), "rows").getArray()->isEmpty(), "Incomplete source leaked into published catalog");
    auto completed = library.importSource(zip);
    check(get(completed, "state").toString() == "complete" && number(completed, "scanned", 0) == 7, "Resume did not finish the source");
    check(number(completed, "midiFiles", 0) == 4 && number(completed, "accepted", 0) == 3 && number(completed, "unsupported", 0) == 1 && number(completed, "metadataFiles", 0) == 2, "Archive accounting is incorrect");
    check(number(library.status(), "patterns", 0) == 2, "Duplicate performances inflate catalog size");
    auto page = library.browse(object({{"limit", 1}}));
    check(get(page, "rows").getArray()->size() == 1 && (bool) get(page, "more"), "Catalog pagination failed");
    auto found = library.browse(object({{"query", "hard rock"}}));
    check(get(found, "rows").getArray()->size() == 1, "Duplicate aliases crowd search results");
    auto fill = library.browse(object({{"role", "fill"}}));
    check(get(fill, "rows").getArray()->size() == 1, "Alias fill role lost in parse cache");
    auto loaded = library.pattern(get((*get(fill, "rows").getArray())[0], "id").toString());
    check(get(get(loaded, "features"), "role").toString() == "fill", "Cached alias restores another role");
    const auto runId = get(completed, "runId").toString();
    check(library.errors(runId).getArray()->size() == 1, "Unsupported entry report missing");
    const auto revision = number(library.status(), "revision", 0);
    library.importSource(zip);
    check(number(library.status(), "revision", 0) == revision, "Repeated identical import changes revision");
    // Completed cached data survives moving/removing its original archive.
    check(zip.deleteFile(), "Cannot remove archive fixture");
    check(get(library.pattern(get((*get(found, "rows").getArray())[0], "id").toString()), "notes").getArray()->size() == 1, "Library depends on original archive for playback");
    const auto directory = fixture.root.getChildFile("directory"); directory.createDirectory();
    directory.getChildFile("Groove.mid").replaceWithData(bytes.getData(), bytes.getSize());
    library.importSource(directory);
    const auto directoryRevision = number(library.status(), "revision", 0);
    directory.getChildFile("New.mid").replaceWithData(midiBytes(38).getData(), midiBytes(38).getSize());
    cancel = false;
    options.batchSize = 1;
    options.progress = [&](const auto& state) { if (number(state, "scanned", 0) >= 1) cancel = true; };
    library.importSource(directory, options);
    check(number(library.status(), "revision", 0) == directoryRevision && number(library.status(), "patterns", 0) == 2, "Partial replacement changed published revision");
    library.importSource(directory);
    check(number(library.status(), "patterns", 0) == 3, "Completed source replacement was not published");
    LibraryImportOptions remap; remap.mapping = juce::JSON::parse(R"({"id":"custom","notes":{"42":"cowbell","38":"cowbell"}})");
    library.importSource(directory, remap);
    const auto remappedRevision = number(library.status(), "revision", 0);
    library.importSource(directory);
    check(number(library.status(), "patterns", 0) == 3 && number(library.status(), "revision", 0) == remappedRevision + 1, "Returning to a cached source map did not republish its revision");
    // The personal archive's legacy writer wraps its 16-bit entry count.
    // Count central records rather than trusting that footer field.
    auto wrapped = fixture.archive("wrapped.zip", {{"one.mid", bytes}, {"two.mid", bytes}, {"three.mid", bytes}});
    juce::MemoryBlock archiveBytes; wrapped.loadFileAsData(archiveBytes);
    auto* end = static_cast<unsigned char*>(archiveBytes.getData()) + archiveBytes.getSize() - 22;
    end[8] = end[10] = 1; end[9] = end[11] = 0;
    wrapped.replaceWithData(archiveBytes.getData(), archiveBytes.getSize());
    auto wrappedResult = library.importSource(wrapped);
    check(number(wrappedResult, "total", 0) == 3 && number(wrappedResult, "accepted", 0) == 3, "Wrapped archive count truncated the import");
    // A ZIP64 footer with 64-bit directory offsets/sizes follows the same path.
    auto zip64 = fixture.archive("zip64.zip", {{"one.mid", bytes}});
    archiveBytes.reset(); zip64.loadFileAsData(archiveBytes);
    end = static_cast<unsigned char*>(archiveBytes.getData()) + archiveBytes.getSize() - 22;
    const auto directorySize = juce::ByteOrder::littleEndianInt(end + 12), directoryOffset = juce::ByteOrder::littleEndianInt(end + 16);
    const auto recordOffset = archiveBytes.getSize() - 22;
    juce::MemoryOutputStream zip64Bytes;
    zip64Bytes.write(archiveBytes.getData(), recordOffset);
    zip64Bytes.writeInt(0x06064b50); zip64Bytes.writeInt64(44); zip64Bytes.writeShort(45); zip64Bytes.writeShort(45);
    zip64Bytes.writeInt(0); zip64Bytes.writeInt(0); zip64Bytes.writeInt64(1); zip64Bytes.writeInt64(1); zip64Bytes.writeInt64(directorySize); zip64Bytes.writeInt64(directoryOffset);
    zip64Bytes.writeInt(0x07064b50); zip64Bytes.writeInt(0); zip64Bytes.writeInt64((juce::int64) recordOffset); zip64Bytes.writeInt(1);
    end[8] = end[9] = end[10] = end[11] = 255;
    for (int i = 12; i < 20; ++i) end[i] = 255;
    zip64Bytes.write(end, 22);
    zip64.replaceWithData(zip64Bytes.getData(), zip64Bytes.getDataSize());
    check(number(library.importSource(zip64), "accepted", 0) == 1, "ZIP64 directory was not read");
    auto descriptor = fixture.archive("descriptor.zip", {{"streamed.mid", bytes}});
    archiveBytes.reset(); descriptor.loadFileAsData(archiveBytes);
    auto* local = static_cast<unsigned char*>(archiveBytes.getData());
    end = local + archiveBytes.getSize() - 22;
    const auto crc = juce::ByteOrder::littleEndianInt(local + 14), compressed = juce::ByteOrder::littleEndianInt(local + 18), uncompressed = juce::ByteOrder::littleEndianInt(local + 22);
    const auto centralAt = juce::ByteOrder::littleEndianInt(end + 16);
    local[6] |= 8; local[centralAt + 8] |= 8;
    std::memset(local + 14, 0, 12);
    const auto newCentralAt = centralAt + 16;
    for (int i = 0; i < 4; ++i) end[16 + i] = (unsigned char) (newCentralAt >> (8 * i));
    juce::MemoryOutputStream descriptorBytes;
    descriptorBytes.write(local, centralAt);
    descriptorBytes.writeInt(0x08074b50); descriptorBytes.writeInt((int) crc); descriptorBytes.writeInt((int) compressed); descriptorBytes.writeInt((int) uncompressed);
    descriptorBytes.write(local + centralAt, archiveBytes.getSize() - centralAt);
    descriptor.replaceWithData(descriptorBytes.getData(), descriptorBytes.getDataSize());
    check(number(library.importSource(descriptor), "accepted", 0) == 1, "ZIP entries with trailing sizes were discarded");
    // A catalog written before conservative vendor inference must retain its
    // own map, even when current inference would leave this articulation open.
    const auto legacySource=fixture.archive("legacy.zip",{{"General MIDI/Conga.mid",midiBytes(63)}});
    library.importSource(legacySource);
    auto legacyRows=get(library.browse(object({{"query","conga"}})),"rows");
    const auto legacyBinding=get((*legacyRows.getArray())[0],"id").toString();
    const auto legacyPattern=get((*legacyRows.getArray())[0],"patternId").toString();
    {
        sql::Database store(db);
        sql::Statement source(store,"SELECT run_id,ordinal FROM bindings WHERE id=?");source.text(1,legacyBinding);check(source.row(),"Legacy map fixture is absent");
        const auto run=source.text(0);const auto ordinal=source.integer(1);source.reset();
        sql::Statement version(store,"UPDATE runs SET mapping_version=1 WHERE id=?");version.text(1,run);version.done();
        sql::Statement rename(store,"UPDATE entries SET path='Rock DFH/Old conga.mid' WHERE run_id=? AND ordinal=?");rename.text(1,run);rename.integer(2,ordinal);rename.done();
    }
    const auto recalled=library.pattern(legacyBinding);
    check(get(recalled,"id").toString()==legacyPattern&&get(recalled,"notes").getArray()->size()==1&&number(get(recalled,"mapping"),"version",0)==1,"New inference rewrote an older completed catalog revision");
    const auto queuedSource=fixture.archive("queued.zip",{{"Rock/Waiting.mid",midiBytes(49)}});
    const auto waitingRevision=number(library.status(),"revision",0);
    std::atomic<bool> waiting{false},cancelWaiting{false};std::future<juce::var> queued;
    {
        sql::Database writer(db);sql::Transaction preparation(writer);
        LibraryImportOptions pending;pending.cancelled=[&]{return cancelWaiting.load();};pending.progress=[&](const auto& value){if(get(value,"state").toString()=="waiting")waiting=true;};
        queued=std::async(std::launch::async,[&,pending]{return library.importSource(queuedSource,pending);});
        for(int tries=0;tries<400&&!waiting.load();++tries)juce::Thread::sleep(5);
        check(waiting.load(),"Import did not expose its catalog-admission wait");
        check(number(library.status(),"revision",0)==waitingRevision,"Waiting for catalog preparation changed published sources");
        juce::Thread::sleep(5500);preparation.commit();
    }
    check(get(queued.get(),"state").toString()=="complete","A one-time preparation longer than SQLite's old timeout failed a waiting import");
    const auto cancelledSource=fixture.archive("cancelled-wait.zip",{{"Rock/Cancelled.mid",midiBytes(51)}});
    waiting=false;std::future<bool> cancelledAdmission;
    {
        sql::Database writer(db);sql::Transaction preparation(writer);
        LibraryImportOptions pending;pending.cancelled=[&]{return cancelWaiting.load();};pending.progress=[&](const auto& value){if(get(value,"state").toString()=="waiting")waiting=true;};
        cancelledAdmission=std::async(std::launch::async,[&,pending]{try{library.importSource(cancelledSource,pending);return false;}catch(const std::exception& error){return cancelWaiting.load()&&juce::String(error.what()).contains("paused");}});
        for(int tries=0;tries<400&&!waiting.load();++tries)juce::Thread::sleep(5);
        check(waiting.load(),"Cancellation fixture never reached catalog admission");cancelWaiting=true;
        check(cancelledAdmission.get(),"An import could not cancel while another worker held catalog preparation");
        sql::Statement leases(writer,"SELECT COUNT(*) FROM import_leases");leases.row();check(leases.integer(0)==0,"Cancelled catalog admission left an import lease");
    }
    auto service = LibraryService::acquire(db);
    check(service == LibraryService::acquire(db), "Host instances do not share their catalog worker");
    std::cout << "library: exact timing and metadata, source maps, ZIP import, deduplication, checkpoint/resume, atomic publication, cached playback and paging passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
