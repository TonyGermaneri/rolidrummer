#include <ssb/Engine.h>
#include <ssb/Parameters.h>
#include <juce_events/juce_events.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <sys/resource.h>

int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;
 try {
  const int selected=argc==2?std::stoi(argv[1]):-1;
  if(argc>2||selected< -1||selected>5)throw std::runtime_error("Usage: drummer-benchmark [PROFILE_INDEX_0_TO_5]");
  std::cout<<"rate,block,profile,voices,audio_seconds,render_seconds,realtime_percent,p99_block_ms,max_block_ms,peak_rss_mib\n";
  for(double rate:{48000.,96000.})for(int block:{64,256})for(int profile=0;profile<6;++profile){
   if(selected>=0&&profile!=selected)continue;
   auto sample=std::make_shared<ssb::Sample>();sample->rate=48000;sample->channels.resize(1);sample->channels[0].resize(24000);
   for(int i=0;i<24000;++i)sample->channels[0][size_t(i)]=float(.1*std::sin(i*.033)+.05*std::sin(i*.317));
   auto performance=std::make_shared<ssb::Performance>();performance->volume=.1f;performance->contactEffectsOnly=true;
   std::array<std::shared_ptr<ssb::Sound>,16> sounds;
   for(int contact=0;contact<16;++contact){auto sound=std::make_shared<ssb::Sound>();sound->id="contact-"+std::to_string(contact);sound->sample=sample;sound->s.mode=ssb::TriggerMode::hold;sound->s.repeat=0;sound->s.pan=contact%2?-.4f:.4f;
    if(profile){sound->s.delayMix=.4f;sound->s.reverbMix=.3f;sound->expressionRouteCount=2;sound->expressionRoutes[0]={3,ssb::parameterIndex("reverbSize"),.1f,6,1,10};sound->expressionRoutes[1]={2,ssb::parameterIndex("delayTime"),.02f,.8f,1,10};}
    if(profile>=2){sound->s.grain=true;sound->s.grainFreq=60;sound->s.grainStereo=.7f;sound->s.grainSpray=40;}
    if(profile==3){sound->s.grainFm=true;sound->s.grainFmAmount=80;sound->s.grainScanOn=true;sound->expressionRouteCount=3;sound->expressionRoutes[2]={2,ssb::parameterIndex("grainWindow"),0,1,1,10};}
    if(profile>=4){sound->s.grainMode=profile==4?ssb::GrainMode::loop:ssb::GrainMode::cloud;sound->s.grainDensity=20;
     sound->s.grainVariation=.5f;sound->s.grainVarSize=12;sound->s.grainVarPitch=7;sound->s.svfType=3;sound->s.cutoff=4000;
     sound->s.mod.lfo1.ratioMode=true;sound->s.mod.lfo1.ratio=.25f;sound->s.mod.lfo1.stereo=180;
     sound->s.mod.routes.push_back({ssb::ModSource::lfo1,ssb::ModDest::grainPos,.02f});}
    sounds[size_t(contact)]=sound;performance->byId[sound->id]=sound;
   }
   ssb::Engine engine;engine.prepare(rate,block);engine.setPerformance(performance);std::vector<float> left(size_t(block),0),right(size_t(block),0);engine.process(left.data(),right.data(),0,nullptr,0);
   for(int voice=0;voice<64;++voice)engine.drumHit(sounds[size_t(voice%16)],uint64_t(voice%16+1),uint64_t(voice+1),.8f,{1,.5f,.4f,.6f,0});
   // Warm the sources and effect buffers before measuring callback cost.
   for(int i=0;i<100;++i)engine.process(left.data(),right.data(),block,nullptr,0);
   using Clock=std::chrono::steady_clock;std::vector<double> times;const int frames=int(rate*3);times.reserve(size_t(frames/block+1));double total=0;
   for(int position=0;position<frames;position+=block){const int n=std::min(block,frames-position);const auto begin=Clock::now();
    for(int contact=0;contact<16;++contact){const float phase=float((position/97+contact)%128)/127;engine.drumExpression(uint64_t(contact+1),{1,.5f,phase,1-phase,0});}
    engine.process(left.data(),right.data(),n,nullptr,0);const auto seconds=std::chrono::duration<double>(Clock::now()-begin).count();total+=seconds;times.push_back(seconds);
    for(int i=0;i<n;++i)if(!std::isfinite(left[size_t(i)])||!std::isfinite(right[size_t(i)]))throw std::runtime_error("Nonfinite benchmark output");
   }
   if(engine.activeVoices()!=64)throw std::runtime_error("Benchmark did not sustain full polyphony");
   std::sort(times.begin(),times.end());rusage usage{};getrusage(RUSAGE_SELF,&usage);
   const char* profiles[]{"sample","sample+effects","stereo-grains+effects","grain-window-mpe+fm+scan+effects","g3-loop+svf+ratio-lfo+effects","g3-cloud20+svf+ratio-lfo+effects"};
   std::cout<<int(rate)<<','<<block<<','<<profiles[profile]<<",64,3,"<<std::fixed<<std::setprecision(4)<<total<<','<<total/3*100<<','<<times[size_t(times.size()*.99)]*1000<<','<<times.back()*1000<<','<<double(usage.ru_maxrss)/1048576<<std::endl;
  }
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
