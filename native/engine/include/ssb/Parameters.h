#pragma once
#include "Model.h"
#include <array>
#include <string_view>
namespace ssb {
struct Parameter { const char* name; float Settings::*member; float min, max; int Settings::*integer = nullptr; };
inline constexpr Parameter parameters[] {
    {"volume", &Settings::volume, 0.0f, 1.5f},
    {"pan", &Settings::pan, -1.0f, 1.0f},
    {"pitch", &Settings::pitch, -24.0f, 24.0f},
    {"fine", &Settings::fine, -100.0f, 100.0f},
    {"speed", &Settings::speed, 0.25f, 4.0f},
    {"clipIn", &Settings::clipIn, 0.0f, 1.0f},
    {"clipOut", &Settings::clipOut, 0.0f, 1.0f},
    {"attack", &Settings::attack, 0.001f, 5.0f},
    {"decay", &Settings::decay, 0.001f, 5.0f},
    {"sustain", &Settings::sustain, 0.0f, 1.0f},
    {"release", &Settings::release, 0.001f, 10.0f},
    {"cutoff", &Settings::cutoff, 20.0f, 20000.0f},
    {"resonance", &Settings::resonance, 0.1f, 20.0f},
    {"fEnvAmount", &Settings::fEnvAmount, -5.0f, 5.0f},
    {"fAttack", &Settings::fAttack, 0.001f, 5.0f},
    {"fDecay", &Settings::fDecay, 0.001f, 5.0f},
    {"fSustain", &Settings::fSustain, 0.0f, 1.0f},
    {"fRelease", &Settings::fRelease, 0.001f, 10.0f},
    {"grainSize", &Settings::grainSize, 0.02f, 10000.0f},
    {"grainPos", &Settings::grainPos, 0.0f, 1.0f},
    {"grainWidth", &Settings::grainWidth, 0.0f, 1.0f},
    {"grainRate", &Settings::grainRate, 0.1f, 200.0f},
    {"grainShape", &Settings::grainShape, 0.0f, 1.0f},
    {"grainJitter", &Settings::grainJitter, 0.0f, 12.0f},
    {"grainReverse", &Settings::grainReverse, 0.0f, 1.0f},
    {"grainSpread", &Settings::grainSpread, 0.0f, 1.0f},
    {"grainStreams", &Settings::grainStreams, 1.0f, 8.0f},
    {"grainScatter", &Settings::grainScatter, 0.0f, 1.0f},
    {"grainDrift", &Settings::grainDrift, 0.0f, 1.0f},
    {"delayTime", &Settings::delayTime, 0.01f, 2.0f},
    {"delayFeedback", &Settings::delayFeedback, 0.0f, 0.9f},
    {"delayMix", &Settings::delayMix, 0.0f, 1.0f},
    {"reverbSize", &Settings::reverbSize, 0.1f, 6.0f},
    {"reverbDecay", &Settings::reverbDecay, 0.5f, 10.0f},
    {"reverbMix", &Settings::reverbMix, 0.0f, 1.0f},
    {"eqLow", &Settings::eqLow, -12.0f, 12.0f},
    {"eqMid", &Settings::eqMid, -12.0f, 12.0f},
    {"eqHigh", &Settings::eqHigh, -12.0f, 12.0f},
    {"rootNote", &Settings::rootNote, 0.0f, 127.0f},
    {"velAmount", &Settings::velAmount, 0.0f, 1.0f},
    {"bendRange", &Settings::bendRange, 0.0f, 48.0f},
    {"repeat", nullptr, 0.0f, 16.0f, &Settings::repeat},
    {"choke", nullptr, 0.0f, 8.0f, &Settings::choke},
};
inline int parameterIndex(std::string_view name) { if(name=="reverse")return -2; for(int i=0;i<(int)std::size(parameters);++i)if(name==parameters[i].name)return i;return -1; }
inline void copyScalars(Settings& to,const Settings& from) {
    to.volume=from.volume;
    to.pan=from.pan;
    to.pitch=from.pitch;
    to.fine=from.fine;
    to.speed=from.speed;
    to.clipIn=from.clipIn;
    to.clipOut=from.clipOut;
    to.attack=from.attack;
    to.decay=from.decay;
    to.sustain=from.sustain;
    to.release=from.release;
    to.cutoff=from.cutoff;
    to.resonance=from.resonance;
    to.fEnvAmount=from.fEnvAmount;
    to.fAttack=from.fAttack;
    to.fDecay=from.fDecay;
    to.fSustain=from.fSustain;
    to.fRelease=from.fRelease;
    to.grainSize=from.grainSize;
    to.grainPos=from.grainPos;
    to.grainWidth=from.grainWidth;
    to.grainRate=from.grainRate;
    to.grainShape=from.grainShape;
    to.grainJitter=from.grainJitter;
    to.grainReverse=from.grainReverse;
    to.grainSpread=from.grainSpread;
    to.grainStreams=from.grainStreams;
    to.grainScatter=from.grainScatter;
    to.grainDrift=from.grainDrift;
    to.delayTime=from.delayTime;
    to.delayFeedback=from.delayFeedback;
    to.delayMix=from.delayMix;
    to.reverbSize=from.reverbSize;
    to.reverbDecay=from.reverbDecay;
    to.reverbMix=from.reverbMix;
    to.eqLow=from.eqLow;
    to.eqMid=from.eqMid;
    to.eqHigh=from.eqHigh;
    to.rootNote=from.rootNote;
    to.velAmount=from.velAmount;
    to.bendRange=from.bendRange;
    to.repeat=from.repeat;
    to.choke=from.choke;
    to.mode=from.mode;
    to.filterType=from.filterType;
    to.grain=from.grain;
    to.stretch=from.stretch;
    to.reverse=from.reverse;
}
}
