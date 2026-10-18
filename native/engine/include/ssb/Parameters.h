#pragma once
#include "Model.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <string_view>
namespace ssb {
enum class LegacyGrain { none,size,width,rate,shape,jitter,reverse,spread,streams,scatter,drift };
struct Parameter { const char* name; float Settings::*member; float min, max; int Settings::*integer = nullptr; LegacyGrain legacy=LegacyGrain::none; };
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
    {"grainFreq", &Settings::grainFreq, 1.0f/3600, 150.0f},
    {"grainFreqKey", &Settings::grainFreqKey, 0.0f, 1.0f},
    {"grainFreqRnd", &Settings::grainFreqRnd, 0.0f, 1.0f},
    {"grainStereo", &Settings::grainStereo, 0.0f, 1.0f},
    {"grainPos", &Settings::grainPos, 0.0f, 1.0f},
    {"grainPosKey", &Settings::grainPosKey, -1.0f, 1.0f},
    {"grainSpray", &Settings::grainSpray, 0.0f, 20000.0f},
    {"grainSpraySlope", &Settings::grainSpraySlope, 1.0f, 10.0f},
    {"grainWindow", &Settings::grainWindow, 0.0f, 1.0f},
    {"grainTuneKey", &Settings::grainTuneKey, 0.0f, 1.0f},
    {"grainTuneRnd", &Settings::grainTuneRnd, 0.0f, 1.0f},
    {"grainAmp", &Settings::grainAmp, 0.0f, 1.0f},
    {"grainVoid", &Settings::grainVoid, 0.0f, 1.0f},
    {"grainFmFreq", &Settings::grainFmFreq, 2.0f, 20000.0f},
    {"grainFmAmount", &Settings::grainFmAmount, 0.0f, 250.0f},
    {"grainFmKey", &Settings::grainFmKey, 0.0f, 2.0f},
    {"grainScanTime", &Settings::grainScanTime, 25.0f, 10000.0f},
    {"grainScanDist", &Settings::grainScanDist, 0.0001f, 1.0f},
    {"grainScanCurve", &Settings::grainScanCurve, 0.5f, 2.0f},
    // Saved mappings retain their original names/units; approximate the former
    // cloud controls in the new model after smoothing, without dropping a route.
    {"grainSize", nullptr, 0.02f, 10000.0f, nullptr, LegacyGrain::size},
    {"grainWidth", nullptr, 0.0f, 1.0f, nullptr, LegacyGrain::width},
    {"grainRate", nullptr, 0.1f, 200.0f, nullptr, LegacyGrain::rate},
    {"grainShape", nullptr, 0.0f, 1.0f, nullptr, LegacyGrain::shape},
    {"grainJitter", nullptr, 0.0f, 12.0f, nullptr, LegacyGrain::jitter},
    {"grainReverse", nullptr, 0.0f, 1.0f, nullptr, LegacyGrain::reverse},
    {"grainSpread", nullptr, 0.0f, 1.0f, nullptr, LegacyGrain::spread},
    {"grainStreams", nullptr, 1.0f, 8.0f, nullptr, LegacyGrain::streams},
    {"grainScatter", nullptr, 0.0f, 1.0f, nullptr, LegacyGrain::scatter},
    {"grainDrift", nullptr, 0.0f, 1.0f, nullptr, LegacyGrain::drift},
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
inline void setParameter(Settings& s,const Parameter& p,float value,double clipSeconds) noexcept {
    const float x=std::clamp(value,p.min,p.max);
    if(p.integer){s.*(p.integer)=(int)std::lround(x);return;}
    if(p.member){s.*(p.member)=x;return;}
    switch(p.legacy){
        case LegacyGrain::size:s.grainFreq=std::clamp(1000.f/x,1.f/3600,150.f);break;
        case LegacyGrain::width:s.grainSpray=std::min(20000.f,float(clipSeconds*500)*x);break;
        case LegacyGrain::rate:s.grainFreq=std::clamp(x*.5f,1.f/3600,150.f);break;
        case LegacyGrain::shape:s.grainWindow=1-x;break;
        case LegacyGrain::jitter:s.grainTuneRnd=std::min(1.f,std::sqrt(2*(std::pow(2.f,x/12)-1)));break;
        case LegacyGrain::reverse:s.reverse=x>=.5f;break;
        case LegacyGrain::spread:s.grainStereo=x;break;
        case LegacyGrain::streams:s.grainStereo=(x-1)/7;break;
        case LegacyGrain::scatter:s.grainFreqRnd=x;break;
        case LegacyGrain::drift:s.grainScanOn=x>0;s.grainScanTime=x>0?std::clamp(100.f/x,25.f,10000.f):100;break;
        default:break;
    }
}
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
    to.grainFreq=from.grainFreq;
    to.grainFreqKey=from.grainFreqKey;
    to.grainFreqRnd=from.grainFreqRnd;
    to.grainStereo=from.grainStereo;
    to.grainPos=from.grainPos;
    to.grainPosKey=from.grainPosKey;
    to.grainSpray=from.grainSpray;
    to.grainSpraySlope=from.grainSpraySlope;
    to.grainWindow=from.grainWindow;
    to.grainTuneKey=from.grainTuneKey;
    to.grainTuneRnd=from.grainTuneRnd;
    to.grainAmp=from.grainAmp;
    to.grainVoid=from.grainVoid;
    to.grainFmFreq=from.grainFmFreq;
    to.grainFmAmount=from.grainFmAmount;
    to.grainFmKey=from.grainFmKey;
    to.grainScanTime=from.grainScanTime;
    to.grainScanDist=from.grainScanDist;
    to.grainScanCurve=from.grainScanCurve;
    to.grainSymmetry=from.grainSymmetry;
    to.grainSpraySign=from.grainSpraySign;
    to.grainAmpMode=from.grainAmpMode;
    to.grainFm=from.grainFm;
    to.grainScanOn=from.grainScanOn;
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
