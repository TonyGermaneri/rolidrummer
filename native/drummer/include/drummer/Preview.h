#pragma once
#include "State.h"
#include <ssb/Engine.h>

namespace drummer {
// Created/prepared on the worker, rendered on audio, retired on the message
// thread. Auditioning never changes the kit, host transport or held contacts.
class Preview {
public:
 Preview(std::unique_ptr<Runtime>, bool pattern, double sampleRate, int blockSize);
 bool add(float* left,float* right,int frames,double bpm) noexcept;
 double sampleRate()const{return rate;}
 void collect(){engine.collectGarbage();}
private:
 static void emit(void*,const Event&);
 void renderTo(int);
 std::unique_ptr<Runtime> runtime;
 ssb::Engine engine;
 Scheduler scheduler;
 Configuration audition;
 std::vector<float> left,right;
 double rate=48000,beat=0,elapsed=0,bpm=120;
 int rendered=0;
 bool pattern=false,started=false;
};
}
