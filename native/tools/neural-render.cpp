#include "PluginProcessor.h"
#include <drummer/PatchResolver.h>
#include <drummer/Assets.h>
#include <drummer/Json.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
struct PlayHead : juce::AudioPlayHead {
    double beat=0;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo result;result.setBpm(120);result.setPpqPosition(beat);result.setIsPlaying(true);return result; }
};
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
    using namespace drummer;
    using namespace drummer::json;
    if(argc<4||argc>7)throw std::runtime_error("Usage: drummer-neural-render CATALOG QUERY OUTPUT_DIRECTORY [VARIATIONS_PER_REGION [SEED [cpu|gpu]]] (use RD_DATA_DIR for isolated validation)");
    PatternLibrary catalog(juce::File(juce::String::fromUTF8(argv[1])));
    NeuralIndex index(catalog.file());
    const auto query=juce::String::fromUTF8(argv[2]);
    const juce::File output(juce::String::fromUTF8(argv[3]));
    if(!output.createDirectory())throw std::runtime_error("Cannot create render output directory");
    const double begin=juce::Time::getMillisecondCounterHiRes();
    auto options=object({{"query",query},{"variationCount",argc>4?juce::String::fromUTF8(argv[4]).getIntValue():0},{"seed",argc>5?juce::String::fromUTF8(argv[5]):"0"}});
    if(argc>6){const auto provider=juce::String::fromUTF8(argv[6]);if(provider!="cpu"&&provider!="gpu")throw std::runtime_error("Expected cpu or gpu retrieval");set(options,"cpu",provider=="cpu");}
    const auto document=resolveNeuralPatch(catalog,index,defaultDocument(),options);
    auto runtime=compileDocument(document);
    const double resolveMs=juce::Time::getMillisecondCounterHiRes()-begin;
    size_t notes=0;for(const auto& clip:runtime->config.clips)notes+=clip.notes.size();
    auto report=object({{"query",query},{"indexId",get(get(document,"neural"),"indexId")},{"resolveMilliseconds",resolveMs},
        {"compiledClips",(int)runtime->config.clips.size()},{"candidateGroups",(int)runtime->config.groups.size()},{"compiledNotes",(juce::int64)notes},
        {"resolution",get(document,"neural")}});
    const auto jsonFile=output.getNonexistentChildFile("resolved-patch",".json");
    if(!jsonFile.replaceWithText(juce::JSON::toString(document)))throw std::runtime_error("Cannot write resolved document");
    const auto patchFile=output.getNonexistentChildFile("resolved-kit",".rolidrummer");savePortablePatch(patchFile,document);
    const auto audioFile=output.getNonexistentChildFile("mpe-performance",".wav");
    std::unique_ptr<juce::OutputStream> stream=audioFile.createOutputStream();
    juce::WavAudioFormat wav;
    auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
    if(!writer)throw std::runtime_error("Cannot create performance WAV");
    DrummerProcessor processor;processor.apply(std::move(runtime));processor.prepareToPlay(48000,256);PlayHead head;processor.setPlayHead(&head);
    juce::MemoryBlock state;processor.getStateInformation(state);
    const auto stateFile=output.getNonexistentChildFile("processor-state",".bin");
    if(!stateFile.replaceWithData(state.getData(),state.getSize()))throw std::runtime_error("Cannot write processor state");
    set(report,"processorState",stateFile.getFullPathName());
    juce::AudioBuffer<float> buffer(2,256);juce::MidiBuffer midi;midi.ensureSize(4096);
    double energy=0,peak=0;int maximumBuses=0,maximumVoices=0;
    const int pitches[]={48,50,52};
    for(int frame=0;frame<16*48000;frame+=256){
        midi.clear();head.beat=frame/24000.;const double seconds=frame/48000.;
        if(frame<12*48000)for(int i=0;i<3;++i){const int channel=i+2;
            const double slide=i==0?(seconds<4?.1:seconds<8?.5:.96):i==1?(seconds<6?.5:.96):(seconds<9?.5:.96);
            midi.addEvent(juce::MidiMessage::controllerEvent(channel,74,(int)(slide*127)),0);
            midi.addEvent(juce::MidiMessage::channelPressureChange(channel,(int)((.5+.45*std::sin(seconds*1.4+i))*127)),0);
            midi.addEvent(juce::MidiMessage::pitchWheel(channel,(int)((.5+.45*std::sin(seconds*.9+i))*16383)),0);
            if(frame==0)midi.addEvent(juce::MidiMessage::noteOn(channel,pitches[i],(juce::uint8)(90+i*10)),0);
        }
        if(frame==12*48000)for(int i=0;i<3;++i)midi.addEvent(juce::MidiMessage::noteOff(i+2,pitches[i],(juce::uint8)(50+i*30)),0);
        processor.processBlock(buffer,midi);
        if(processor.overflow.load())throw std::runtime_error("The full-corpus resolved performance overflowed its scheduler budget");
        maximumBuses=std::max(maximumBuses,processor.clipEvaluations.load());maximumVoices=std::max(maximumVoices,processor.engine.activeVoices());
        for(int channel=0;channel<2;++channel)for(int sample=0;sample<256;++sample){const double value=buffer.getSample(channel,sample);if(!std::isfinite(value))throw std::runtime_error("Nonfinite rendered sample");energy+=value*value;peak=std::max(peak,std::abs(value));}
        if(!writer->writeFromAudioSampleBuffer(buffer,0,256))throw std::runtime_error("Cannot write performance samples");
    }
    writer.reset();
    if(energy<.001)throw std::runtime_error("The named query produced a silent performance");
    set(report,"energy",energy);set(report,"peak",peak);set(report,"maximumClipEvaluationsPerBlock",maximumBuses);set(report,"maximumVoices",maximumVoices);
    set(report,"document",jsonFile.getFullPathName());set(report,"portablePatch",patchFile.getFullPathName());set(report,"audio",audioFile.getFullPathName());
    output.getNonexistentChildFile("render-report",".json").replaceWithText(juce::JSON::toString(report));
    std::cout<<juce::JSON::toString(report,true)<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
