#include <drummer/Assets.h>
#include <drummer/State.h>
#include <set>
#include <stdexcept>

namespace drummer {
namespace {
constexpr int64_t maxAssetBytes = 512LL * 1024 * 1024;
constexpr int64_t maxDocumentBytes = 64LL * 1024 * 1024;
bool validId(const juce::String& id) {
    auto dot = id.indexOfChar('.');
    return dot == 64 && id.substring(0, dot).containsOnly("0123456789abcdef")
        && juce::StringArray{"wav", "aif", "aiff", "flac", "mp3", "ogg"}.contains(id.substring(dot + 1));
}
std::set<juce::String> assetIds(const juce::var& document) {
    std::set<juce::String> ids;
    auto keys = document.getProperty("keys", {});
    if (auto* list = keys.getArray()) for (const auto& key : *list) {
        auto sound = key.getProperty("sound", {});
        auto id = sound.getProperty("asset", {}).toString();
        if (id.isNotEmpty()) { if (!validId(id)) throw std::runtime_error("Invalid sound asset ID"); ids.insert(id); }
    }
    return ids;
}
void install(const juce::String& id, const juce::MemoryBlock& bytes) {
    auto file = assetFile(id);
    if (bytes.getSize() > (size_t) maxAssetBytes || juce::SHA256(bytes).toHexString() != id.substring(0, 64))
        throw std::runtime_error("Sound asset checksum mismatch: " + id.toStdString());
    if (file.existsAsFile()) return;
    if (!file.getParentDirectory().createDirectory()) throw std::runtime_error("Cannot create sound cache");
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithData(bytes.getData(), bytes.getSize()) || !temporary.overwriteTargetFileWithTemporary())
        throw std::runtime_error("Cannot store sound asset");
}
juce::MemoryBlock readAsset(const juce::String& id) {
    auto file = assetFile(id);
    juce::MemoryBlock bytes;
    if (!file.existsAsFile() || file.getSize() > maxAssetBytes || !file.loadFileAsData(bytes))
        throw std::runtime_error("Sound asset is unavailable: " + id.toStdString());
    return bytes;
}
juce::MemoryBlock readEntry(juce::ZipFile& zip, const juce::ZipFile::ZipEntry& entry, int64_t limit) {
    if (entry.uncompressedSize < 0 || entry.uncompressedSize > limit) throw std::runtime_error("Patch entry is too large");
    std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(entry));
    juce::MemoryBlock data;
    if (!stream || stream->readIntoMemoryBlock(data, (ssize_t) entry.uncompressedSize + 1) != entry.uncompressedSize)
        throw std::runtime_error("Incomplete patch archive");
    return data;
}
}

juce::File assetFile(const juce::String& id) {
    if (!validId(id)) throw std::runtime_error("Invalid sound asset ID");
    return dataDirectory().getChildFile("assets").getChildFile(id);
}

juce::File cacheSound(juce::var& sound) {
    auto original = sound.getProperty("path", {}).toString();
    auto id = sound.getProperty("asset", {}).toString();
    if (id.isNotEmpty()) {
        auto cached = assetFile(id);
        if (cached.existsAsFile()) return cached;
    }
    juce::File source(original);
    if (!source.existsAsFile()) throw std::runtime_error("Missing sound: " + original.toStdString());
    if (source.getSize() > maxAssetBytes) throw std::runtime_error("Sound file exceeds 512 MB");
    auto actual = juce::SHA256(source).toHexString() + source.getFileExtension().toLowerCase();
    if (!validId(actual)) throw std::runtime_error("Unsupported sound format");
    if (id.isNotEmpty() && actual != id) throw std::runtime_error("Original sound changed; locate the saved sound or drop a replacement");
    auto cached = assetFile(actual);
    if (!cached.existsAsFile()) {
        if (!cached.getParentDirectory().createDirectory()) throw std::runtime_error("Cannot create sound cache");
        juce::TemporaryFile temporary(cached);
        if (!source.copyFileTo(temporary.getFile()) || juce::SHA256(temporary.getFile()).toHexString() != actual.substring(0, 64)
            || !temporary.overwriteTargetFileWithTemporary()) throw std::runtime_error("Cannot cache sound");
    }
    if (auto* object = sound.getDynamicObject()) object->setProperty("asset", actual);
    return cached;
}

void appendSessionAssets(juce::ValueTree& state, const juce::var& document) {
    juce::ValueTree assets("Assets");
    for (const auto& id : assetIds(document)) {
        // An already-missing sound must not prevent saving the rest of a kit.
        if (!assetFile(id).existsAsFile()) continue;
        juce::ValueTree asset("Asset");
        asset.setProperty("id", id, nullptr);
        asset.setProperty("data", juce::var(readAsset(id)), nullptr);
        assets.addChild(asset, -1, nullptr);
    }
    state.addChild(assets, -1, nullptr);
}

void restoreSessionAssets(const juce::ValueTree& state) {
    for (auto asset : state.getChildWithName("Assets")) {
        auto data = asset.getProperty("data");
        if (auto* bytes = data.getBinaryData()) install(asset.getProperty("id").toString(), *bytes);
    }
}

void savePortablePatch(const juce::File& target, const juce::var& document) {
    auto runtime = compileDocument(document);
    auto canonical = juce::JSON::parse(runtime->json);
    auto keys = canonical.getProperty("keys", {});
    if (auto* list = keys.getArray()) for (const auto& key : *list) {
        auto sound = key.getProperty("sound", {});
        if (sound.getProperty("missing", {}).toString().isNotEmpty()) throw std::runtime_error("Locate missing sounds before exporting a portable patch");
    }
    juce::ZipFile::Builder zip;
    zip.addEntry(std::make_unique<juce::MemoryInputStream>(runtime->json.toRawUTF8(), runtime->json.getNumBytesAsUTF8(), true), 6, "patch.json", juce::Time::getCurrentTime());
    for (const auto& id : assetIds(canonical)) zip.addFile(assetFile(id), 0, "assets/" + id);
    juce::TemporaryFile temporary(target);
    {
        juce::FileOutputStream output(temporary.getFile());
        if (!output.openedOk() || !zip.writeToStream(output, nullptr)) throw std::runtime_error("Could not write portable patch");
        output.flush();
        if (output.getStatus().failed()) throw std::runtime_error("Portable patch write failed");
    }
    if (!temporary.overwriteTargetFileWithTemporary()) throw std::runtime_error("Could not replace patch file");
}

juce::var loadPatch(const juce::File& file) {
    juce::ZipFile zip(file);
    auto* manifest = zip.getEntry("patch.json");
    if (!manifest) {
        if (file.getSize() > maxDocumentBytes) throw std::runtime_error("Patch document is too large");
        auto doc = juce::JSON::parse(file.loadFileAsString());
        if (!doc.isObject()) throw std::runtime_error("Invalid patch file");
        return doc;
    }
    auto bytes = readEntry(zip, *manifest, maxDocumentBytes);
    auto document = juce::JSON::parse(juce::String::fromUTF8((const char*) bytes.getData(), (int) bytes.getSize()));
    if (!document.isObject()) throw std::runtime_error("Invalid patch manifest");
    int64_t total = 0;
    for (const auto& id : assetIds(document)) {
        auto* entry = zip.getEntry("assets/" + id);
        if (!entry) throw std::runtime_error("Patch archive is missing a sound");
        total += entry->uncompressedSize;
        if (total > 1024LL * 1024 * 1024) throw std::runtime_error("Patch sounds exceed 1 GB");
        install(id, readEntry(zip, *entry, maxAssetBytes));
    }
    return document;
}
}
