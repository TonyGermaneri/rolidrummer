#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <drummer/State.h>
#include <drummer/SharedStore.h>
#include <drummer/Preview.h>
#include <drummer/PatternLibrary.h>
#include <drummer/PerformanceMonitor.h>
#include <ssb/Engine.h>
#include <ssb/MidiQueue.h>
#include <mutex>
class DrummerProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
 DrummerProcessor();~DrummerProcessor() override;
 void prepareToPlay(double,int) override;void releaseResources() override {}
 bool isBusesLayoutSupported(const BusesLayout&)const override;
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
 using juce::AudioProcessor::processBlock;
 juce::AudioProcessorEditor* createEditor()override;bool hasEditor()const override{return true;}
 const juce::String getName()const override{return JucePlugin_Name;}
 bool acceptsMidi()const override{return true;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}bool supportsMPE()const override{return true;}
 double getTailLengthSeconds()const override{return 20.;}
 int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}
 const juce::String getProgramName(int)override{return "Current kit";}void changeProgramName(int,const juce::String&)override{}
 void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;
 bool apply(std::unique_ptr<drummer::Runtime>, std::optional<uint64_t> expected = {},double activationGrid=0,bool preserveActivation=false);
 bool activationPending() const noexcept{return boundaryRevision.load(std::memory_order_acquire)>activeRevision.load(std::memory_order_acquire);}
 juce::String document();
 // Kit auditions replace only the live sounds, leaving the saved document intact.
 uint64_t beginKitPreview();
 bool previewKit(const juce::var& kit,uint64_t expected,uint64_t ticket);
 void cancelKitPreview();
 void publishPreview(std::unique_ptr<drummer::Preview>,uint64_t request);
 void stopPreview();
 std::atomic<uint64_t> previewRequest{0};
 std::atomic<bool> previewPlaying{false};
 std::atomic<double> previewRate{48000};std::atomic<int> previewBlock{512};
 juce::AudioProcessorValueTreeState parameters;
 ssb::Engine engine;
 ssb::MidiQueue screenMidi,midiToPage;
 std::atomic<double> hostBpm{120},hostPpq{0};std::atomic<bool> hostPlaying{false},standalonePlaying{false},freePlaying{false};
 std::array<std::atomic<int>,16> contacts{};
 std::array<std::atomic<float>,16> contactSlide{},contactGlide{},contactPressure{};
 struct FingerView { uint64_t owner=0;int channel=0,note=0,zone=0;float slide=0,glide=.5f,pressure=0;bool sustained=false,toggled=false,playing=false;int candidate=-1;bool queued=false;int origin=-1; };
 struct KeyPattern { int clip=-1,zone=0,candidate=-1;double phase=0;bool known=false,playing=false;double beat=0; };
 struct FingerSnapshot {
  struct PendingLatch {int action=0,note=-1;};
  std::array<PendingLatch,128> latches{};
  std::array<FingerView,drummer::maxContacts+drummer::Scheduler::maxSustained> fingers{};int count=0;
  std::array<KeyPattern,128> patterns{};
  const drummer::Runtime* runtime=nullptr;
  std::array<uint8_t,32> patternHash{};
  double beat=0,bpm=120;uint64_t epoch=0,touchSerial=0;int touchNote=-1;
 };
 // Runtime is immutable and protected from message-thread GC by this snapshot.
 // Dereference it only synchronously on the message thread.
 bool readFingers(FingerSnapshot&) noexcept;
 drummer::PerformanceMonitor performanceMonitor;
 std::atomic<int> lastSlideChannel{-1},lastSlideValue{0};
 std::atomic<uint64_t> stateRevision{0};
 // Program requests cross to the worker without loading samples on the audio thread.
 void requestProgram(int program) noexcept;
 std::atomic<int> lastProgram{-1};
 std::atomic<bool> programFailed{false};
 std::atomic<bool> programUnassigned{false};
 std::atomic<int> clipEvaluations{0};std::atomic<bool> overflow{false};
 std::unique_ptr<drummer::SharedStore> shared;
 std::shared_ptr<drummer::LibraryService> library;
 juce::ThreadPool jobs{1};
 juce::ThreadPool neuralJobs{1};
 std::shared_ptr<int> life=std::make_shared<int>(0);
 int editorWidth=1500,editorHeight=880;
private:
 // Both sides try once; audio never waits for the editor. Copy only fixed-size
 // data under the flag, then build JS objects after releasing it.
 std::atomic_flag fingerBusy=ATOMIC_FLAG_INIT;
 FingerSnapshot fingerSnapshot;
 std::array<KeyPattern,128> lastPatterns{};
 void rememberPattern(const drummer::Contact&,double,bool) noexcept;
 void publishFingers() noexcept;
 void timerCallback()override;
 std::atomic<uint64_t> programRequest{0};
 std::atomic<uint64_t> servicedProgram{0},lastProgramRevision{0};
 static void emit(void*,const drummer::Event&);
 void renderTo(int);
 drummer::Scheduler scheduler;
 drummer::Runtime* active=nullptr;
 drummer::Runtime* pendingRuntime=nullptr;
 std::atomic<uint64_t> activeRevision{0},boundaryRevision{0};
 double queuedActivationGrid=0,activationBeat=0,activationExpected=0;
 bool activationScheduled=false,activationClockKnown=false;
 std::atomic<drummer::Runtime*> incoming{nullptr};
 std::array<drummer::Runtime*,256> retired{};std::atomic<unsigned> retiredWrite{0},retiredRead{0};
 std::array<drummer::Midi,4096> input{};
 std::vector<float> rightScratch;
 float* outputL=nullptr;float* outputR=nullptr;int rendered=0,eventBase=0;double rate=48000,standalonePpq=0;
 drummer::Clock clock;
 double monitorBeat=0,monitorSegment=0;uint64_t monitorEpoch=0;
 bool wasHostPlaying=false;
 std::array<juce::RangedAudioParameter*,5> macroParameters {};
 std::atomic<float>* outputVolume=nullptr;
 std::mutex stateMutex;juce::String saved;
 uint64_t kitPreviewRequest=0;
 std::unique_ptr<drummer::Runtime> kitPreviewRestore;
 std::atomic<drummer::Preview*> previewIncoming{nullptr};drummer::Preview* previewActive=nullptr;
 std::array<drummer::Preview*,16> previewRetired{};std::atomic<unsigned> previewRead{0},previewWrite{0};
 std::atomic<bool> previewStop{false};
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrummerProcessor)
};
