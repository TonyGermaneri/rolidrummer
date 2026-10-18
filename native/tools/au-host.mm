#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include <dlfcn.h>
#include <mach-o/loader.h>
#include <uuid/uuid.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

// An editor-free, process-local AU host. This is deliberately independent of
// RoliDrummer's static processor library: all tested code comes from the bundle.
namespace {
AudioComponentFactoryFunction factory = nullptr;
unsigned factoryCalls = 0;
AudioComponentPlugInInterface* instantiate(const AudioComponentDescription* description) {
    ++factoryCalls;
    return factory(description);
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void check(OSStatus status, const char* operation) {
    if (status != noErr) throw std::runtime_error(std::string(operation) + ": " + std::to_string(status));
}
OSType fourCC(NSString* value) {
    require(value.length == 4, "Invalid component code");
    OSType result = 0;
    for (int i = 0; i < 4; ++i) result = (result << 8) | uint8_t(value.UTF8String[i]);
    return result;
}
struct Unit {
    AudioUnit value = nullptr;
    bool initialized = false;
    ~Unit() { if (value) { if (initialized) AudioUnitUninitialize(value); AudioComponentInstanceDispose(value); } }
    void close() {
        if (initialized) { check(AudioUnitUninitialize(value), "uninitialize"); initialized = false; }
        check(AudioComponentInstanceDispose(value), "dispose"); value = nullptr;
    }
};
NSDictionary* state(AudioUnit unit) {
    CFPropertyListRef result = nullptr;
    UInt32 size = sizeof(result);
    check(AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &result, &size), "get state");
    require(result && CFGetTypeID(result) == CFDictionaryGetTypeID(), "Invalid AU state");
    return CFBridgingRelease(result);
}
void restore(AudioUnit unit, NSDictionary* value) {
    CFPropertyListRef property = (__bridge CFPropertyListRef)value;
    check(AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &property, sizeof(property)), "restore state");
}
NSData* processorState(NSDictionary* value) {
    NSData* bytes = value[@"jucePluginState"];
    require([bytes isKindOfClass:[NSData class]], "Missing processor state");
    return bytes;
}
struct Clock {
    double sample = 0, rate = 48000;
    static OSStatus beat(void* context, Float64* beat, Float64* tempo) {
        const auto& clock = *static_cast<Clock*>(context);
        if (beat) *beat = clock.sample / clock.rate * 2;
        if (tempo) *tempo = 120;
        return noErr;
    }
    static OSStatus transport(void* context, Boolean* playing, Boolean* changed, Float64* sample,
                              Boolean* cycling, Float64* start, Float64* end) {
        const auto& clock = *static_cast<Clock*>(context);
        if (playing) *playing = true;
        if (changed) *changed = clock.sample == 0;
        if (sample) *sample = clock.sample;
        if (cycling) *cycling = false;
        if (start) *start = 0;
        if (end) *end = 0;
        return noErr;
    }
};
NSDictionary* render(Unit& owner, double rate, UInt32 block) {
    const auto unit = owner.value;
    AudioStreamBasicDescription format{};
    format.mSampleRate = rate; format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = UInt32(kAudioFormatFlagsNativeFloatPacked) | UInt32(kAudioFormatFlagIsNonInterleaved);
    format.mBytesPerPacket = format.mBytesPerFrame = sizeof(float);
    format.mFramesPerPacket = 1; format.mChannelsPerFrame = 2; format.mBitsPerChannel = 32;
    check(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &format, sizeof(format)), "set stream format");
    check(AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &block, sizeof(block)), "set maximum frames");
    Clock clock{0, rate};
    HostCallbackInfo callbacks{};
    callbacks.hostUserData = &clock; callbacks.beatAndTempoProc = Clock::beat; callbacks.transportStateProc = Clock::transport;
    check(AudioUnitSetProperty(unit, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &callbacks, sizeof(callbacks)), "set host callbacks");
    check(AudioUnitInitialize(unit), "initialize");
    owner.initialized = true;
    std::array<std::vector<float>, 2> samples{std::vector<float>(block), std::vector<float>(block)};
    struct StereoBuffers { UInt32 count; AudioBuffer buffers[2]; } buffers{2, {}};
    double energy = 0, peak = 0, finalPeak = 0;
    const int pitches[]{48, 50, 52};
    const int64_t total = int64_t(16 * rate), lift = int64_t(12 * rate);
    UInt32 blocks = 0;
    for (int64_t frame = 0; frame < total; frame += block) {
        const auto length = UInt32(std::min<int64_t>(block, total - frame));
        clock.sample = double(frame);
        for (int i = 0; i < 3; ++i) {
            const UInt32 channel = UInt32(i + 1); // MIDI channels 2, 3 and 4.
            if (frame < lift) {
                const double seconds = frame / rate;
                const UInt32 slide = seconds < 4 ? 12 : seconds < 8 ? 64 : 123;
                check(MusicDeviceMIDIEvent(unit, 0xb0 | channel, 74, slide, 0), "slide");
                check(MusicDeviceMIDIEvent(unit, 0xd0 | channel, UInt32(64 + 50 * std::sin(seconds + i)), 0, 0), "pressure");
                const UInt32 pitch = UInt32(8192 + 7000 * std::sin(seconds * .7 + i));
                check(MusicDeviceMIDIEvent(unit, 0xe0 | channel, pitch & 127, pitch >> 7, 0), "glide");
                if (frame == 0) check(MusicDeviceMIDIEvent(unit, 0x90 | channel, pitches[i], 90 + i * 10, 0), "strike");
            }
            if (frame <= lift && frame + length > lift)
                check(MusicDeviceMIDIEvent(unit, 0x80 | channel, pitches[i], 50 + i * 30, UInt32(lift - frame)), "lift");
        }
        for (int i = 0; i < 2; ++i) {
            std::fill(samples[i].begin(), samples[i].end(), 0);
            buffers.buffers[i] = {1, UInt32(length * sizeof(float)), samples[i].data()};
        }
        AudioTimeStamp timestamp{}; timestamp.mSampleTime = double(frame); timestamp.mFlags = kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags flags = 0;
        check(AudioUnitRender(unit, &flags, &timestamp, 0, length, reinterpret_cast<AudioBufferList*>(&buffers)), "render");
        for (const auto& channel : samples) for (UInt32 i = 0; i < length; ++i) {
            const double sample = channel[i]; require(std::isfinite(sample), "Nonfinite AU audio");
            energy += sample * sample; peak = std::max(peak, std::abs(sample));
            if (frame + i >= total - int64_t(rate / 2)) finalPeak = std::max(finalPeak, std::abs(sample));
        }
        ++blocks;
    }
    check(AudioUnitUninitialize(unit), "uninitialize render");
    owner.initialized = false;
    require(energy > .001, "Silent AU performance");
    require(finalPeak < 1e-5, "AU notes did not drain after lift");
    return @{@"sampleRate":@(rate), @"blockSize":@(block), @"blocks":@(blocks), @"energy":@(energy), @"peak":@(peak), @"finalHalfSecondPeak":@(finalPeak)};
}
}

int main(int argc, char** argv) { @autoreleasepool { try {
    require(argc == 5, "Usage: drummer-au-host AU_BUNDLE PROCESSOR_STATE_BINARY REPORT_JSON PRIVATE_DATA_DIRECTORY");
    require(setenv("RD_DATA_DIR", argv[4], 1) == 0, "Cannot isolate application data");
    NSBundle* bundle = [NSBundle bundleWithPath:[[NSString stringWithUTF8String:argv[1]] stringByStandardizingPath]];
    NSDictionary* metadata = [bundle.infoDictionary[@"AudioComponents"] firstObject];
    require(metadata != nil, "Missing AU metadata");
    void* module = dlopen(bundle.executablePath.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
    require(module != nullptr, "Cannot load AU image");
    factory = reinterpret_cast<AudioComponentFactoryFunction>(dlsym(module, [metadata[@"factoryFunction"] UTF8String]));
    require(factory != nullptr, "Missing AU factory");
    Dl_info identity{}; require(dladdr(reinterpret_cast<void*>(factory), &identity) != 0, "Cannot identify AU image");
    NSString* loaded = [[NSString stringWithUTF8String:identity.dli_fname] stringByStandardizingPath];
    require([loaded isEqualToString:bundle.executablePath.stringByStandardizingPath], "Loaded wrong AU image");
    NSString* uuid = @"";
    const auto* header = static_cast<const mach_header_64*>(identity.dli_fbase);
    require(header->magic == MH_MAGIC_64, "Expected a 64-bit AU image");
    const auto* command = reinterpret_cast<const load_command*>(header + 1);
    for (UInt32 i = 0; i < header->ncmds; ++i) {
        if (command->cmd == LC_UUID) { char value[37]{}; uuid_unparse_upper(reinterpret_cast<const uuid_command*>(command)->uuid, value); uuid = [NSString stringWithUTF8String:value]; }
        command = reinterpret_cast<const load_command*>(reinterpret_cast<const char*>(command) + command->cmdsize);
    }
    require(uuid.length > 0, "Missing AU UUID");
    AudioComponentDescription description{};
    description.componentType = fourCC(metadata[@"type"]); description.componentSubType = fourCC(metadata[@"subtype"]);
    description.componentManufacturer = fourCC(metadata[@"manufacturer"]);
    AudioComponent component = AudioComponentRegister(&description, (__bridge CFStringRef)metadata[@"name"], [metadata[@"version"] unsignedIntValue], instantiate);
    require(component != nullptr, "Cannot register process-local AU");
    NSData* expected = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[2]]];
    require(expected.length > 0, "Missing processor state");
    NSMutableArray* renders = [NSMutableArray array];
    NSDictionary* saved = nil;
    UInt32 parameterCount = 0;
    const double rates[]{48000, 96000, 48000}; const UInt32 sizes[]{64, 257, 2048};
    for (int pass = 0; pass < 3; ++pass) {
        Unit unit; check(AudioComponentInstanceNew(component, &unit.value), "instantiate exact factory");
        if (pass == 0) {
            NSMutableDictionary* initial = [state(unit.value) mutableCopy];
            initial[@"jucePluginState"] = expected;
            restore(unit.value, initial);
        } else restore(unit.value, saved);
        saved = state(unit.value);
        require([processorState(saved) isEqualToData:expected], "AU changed processor bytes on state recall");
        UInt32 bytes = 0; Boolean writable = false;
        check(AudioUnitGetPropertyInfo(unit.value, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &bytes, &writable), "parameter list size");
        std::vector<AudioUnitParameterID> parameters(bytes / sizeof(AudioUnitParameterID));
        if (bytes) check(AudioUnitGetProperty(unit.value, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, parameters.data(), &bytes), "parameter list");
        parameterCount = UInt32(parameters.size()); require(parameterCount >= 5, "Missing macro parameters");
        for (const auto id : parameters) {
            AudioUnitParameterValue before = 0, after = 0; AudioUnitParameterInfo info{}; UInt32 size = sizeof(info);
            check(AudioUnitGetProperty(unit.value, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &size), "parameter info");
            check(AudioUnitGetParameter(unit.value, id, kAudioUnitScope_Global, 0, &before), "get parameter");
            check(AudioUnitSetParameter(unit.value, id, kAudioUnitScope_Global, 0, info.maxValue, 0), "set parameter");
            restore(unit.value, saved);
            check(AudioUnitGetParameter(unit.value, id, kAudioUnitScope_Global, 0, &after), "restored parameter");
            require(before == after, "AU state failed to restore parameter");
            if (info.flags & kAudioUnitParameterFlag_HasCFNameString) { if (info.flags & kAudioUnitParameterFlag_CFNameRelease) CFRelease(info.cfNameString); }
        }
        [renders addObject:render(unit, rates[pass], sizes[pass])];
        require([processorState(state(unit.value)) isEqualToData:expected], "AU performance changed saved state");
        unit.close();
    }
    require(factoryCalls == 3, "System registry selected an unexpected AU factory");
    NSDictionary* report = @{@"state":@"complete", @"loadedImage":loaded, @"loadedUUID":uuid, @"factoryCalls":@(factoryCalls),
        @"parameterCount":@(parameterCount), @"resolvedDocumentRecalled":@YES, @"lifecycleCompleted":@YES, @"renders":renders,
        @"scope":@"Exact development AU factory, process-local registration, state/parameter round trips and independent-channel five-dimension MIDI rendering. No GUI, physical controller, system registration, deadline or Apple auval claim."};
    NSError* error = nil;
    NSData* json = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:&error];
    require(json && [json writeToFile:[NSString stringWithUTF8String:argv[3]] options:NSDataWritingAtomic error:&error], "Cannot write host report");
    std::cout << "AU host passed: " << uuid.UTF8String << '\n';
    // The bundle stays loaded until process exit, as in a conventional host.
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } } }
