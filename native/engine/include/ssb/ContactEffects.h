#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ssb {
// Drum expression needs continuously variable effects, independent for every
// contact. No IR loading, locks, allocation or buffer clearing occurs on audio.
class ContactEffects {
    struct Line {
        std::vector<float> data;
        size_t write = 0, valid = 0;
        void prepare(size_t size) { data.assign(size, 0); reset(); }
        void reset() noexcept { write = valid = 0; }
        float read(double delay) const noexcept {
            delay = std::clamp(delay, 1., double(data.size()-2));
            const auto whole = size_t(delay);const auto fraction = float(delay-double(whole));
            const auto size = data.size();
            const size_t recent=write>=whole?write-whole:write+size-whole;
            const size_t older=recent?recent-1:size-1;
            const float a = valid >= whole ? data[recent] : 0;
            const float b = valid > whole ? data[older] : 0;
            return a+(b-a)*fraction;
        }
        void push(float value) noexcept { data[write]=value;if(++write==data.size())write=0;valid=std::min(valid+1,data.size()); }
    };
public:
    static constexpr int capacity = 32, quantum = 32;
    struct Controls { float time=.2f,feedback=0,size=2,decay=3; };
private:
    struct Bus {
        uint64_t owner=0,age=0;uint32_t controller=0;
        bool released=false,sleeping=false;
        double quiet=0;
        Controls controls;
        std::array<Line,2> delay;
        std::array<Line,8> room;
        std::array<float,8> damp{},gain{};
        std::array<double,8> lengths{};
        std::array<float,quantum> delayL{},delayR{},roomL{},roomR{};
        std::array<Controls,quantum> changes{};
        std::array<bool,quantum> changed{};
        float lastSize=-1,lastDecay=-1;
        void reset() noexcept {
            for(auto& line:delay)line.reset();for(auto& line:room)line.reset();
            damp.fill(0);quiet=0;sleeping=false;lastSize=lastDecay=-1;
        }
        void clearInput() noexcept { delayL.fill(0);delayR.fill(0);roomL.fill(0);roomR.fill(0);changed.fill(false); }
    };
    std::array<Bus,capacity> buses;
    double rate=48000;
    float damping=.6f;
    uint64_t serial=0;
    static constexpr std::array<double,8> lengths { .0113,.0137,.0179,.0197,.0233,.0293,.0317,.0371 };
    static constexpr std::array<float,8> signs { 1,-1,-1,1,-1,1,1,-1 };
public:
    void prepare(double sampleRate) {
        rate=sampleRate;damping=float(1-std::exp(-2*3.141592653589793*7000/rate));
        for(auto& bus:buses){
            for(auto& line:bus.delay)line.prepare(size_t(std::ceil(rate*2.01))+4);
            for(auto& line:bus.room)line.prepare(size_t(std::ceil(rate*.27))+4);
            bus.owner=0;bus.reset();bus.clearInput();
        }
    }
    void reset() noexcept { for(auto& bus:buses){bus.owner=0;bus.reset();bus.clearInput();} }
    int acquire(uint64_t owner) noexcept {
        for(int i=0;i<capacity;++i)if(buses[size_t(i)].owner==owner)return i;
        int chosen=-1;
        for(int i=0;i<capacity;++i){const auto& b=buses[size_t(i)];if(!b.owner){chosen=i;break;}if(b.released&&(chosen<0||b.age<buses[size_t(chosen)].age))chosen=i;}
        // Sixteen MPE contacts plus sixteen released tails fit simultaneously.
        // Under heavier direct-engine use, preserve active contacts.
        if(chosen<0)return -1;
        auto& b=buses[size_t(chosen)];b.reset();b.clearInput();b.owner=owner;b.controller=0;b.age=++serial;b.released=false;b.controls={};
        return chosen;
    }
    void active(uint64_t owner) noexcept { for(auto& b:buses)if(b.owner==owner){b.released=false;b.age=++serial;} }
    void release(uint64_t owner) noexcept { for(auto& b:buses)if(b.owner==owner)b.released=true; }
    void begin() noexcept { for(auto& b:buses)b.clearInput(); }
    void send(int slot,uint64_t owner,uint32_t order,int offset,float dl,float dr,float rl,float rr,Controls controls,bool released=false) noexcept {
        if(slot<0)return;auto& b=buses[size_t(slot)];if(b.owner!=owner)return;
        const auto i=size_t(offset);b.delayL[i]+=dl;b.delayR[i]+=dr;b.roomL[i]+=rl;b.roomR[i]+=rr;
        if(order>=b.controller){b.controller=order;b.released=released;b.changes[i]=controls;b.changed[i]=true;}
    }
    void process(float* left,float* right,int count) noexcept {
        for(auto& b:buses){
            if(!b.owner)continue;
            for(int i=0;i<count;++i){
                const auto index=size_t(i);
                if(b.changed[index])b.controls=b.changes[index];
                const float input=std::max({std::abs(b.delayL[index]),std::abs(b.delayR[index]),std::abs(b.roomL[index]),std::abs(b.roomR[index])});
                if(b.sleeping){if(input<=1e-7f)continue;b.reset();}
                auto& c=b.controls;
                if(c.size!=b.lastSize||c.decay!=b.lastDecay){
                    const double scale=1+std::clamp(c.size,.1f,6.f);
                    for(size_t j=0;j<8;++j){const double seconds=lengths[j]*scale;b.lengths[j]=seconds*rate;b.gain[j]=float(std::pow(10.,-3*seconds/std::clamp(c.decay,.5f,10.f)));}
                    b.lastSize=c.size;b.lastDecay=c.decay;
                }
                const double delay=std::clamp(c.time,.01f,2.f)*rate;const float feedback=std::clamp(c.feedback,0.f,.9f);
                const float dl=b.delay[0].read(delay),dr=b.delay[1].read(delay);
                b.delay[0].push(b.delayL[index]+dl*feedback);b.delay[1].push(b.delayR[index]+dr*feedback);
                std::array<float,8> wet{},mixed{};float rl=0,rr=0;
                for(size_t j=0;j<8;++j){wet[j]=b.room[j].read(b.lengths[j]);b.damp[j]+=damping*(wet[j]-b.damp[j]);mixed[j]=b.damp[j]*b.gain[j];rl+=wet[j]*.25f;rr+=wet[j]*signs[j]*.25f;}
                // Orthogonal Hadamard feedback keeps the room stable at all sizes.
                for(size_t step=1;step<8;step*=2)for(size_t j=0;j<8;j+=step*2)for(size_t k=0;k<step;++k){const float a=mixed[j+k],v=mixed[j+k+step];mixed[j+k]=a+v;mixed[j+k+step]=a-v;}
                for(size_t j=0;j<8;++j)b.room[j].push(mixed[j]*.353553390593f+(b.roomL[index]+b.roomR[index]*signs[j])*.25f);
                left[i]+=dl+rl;right[i]+=dr+rr;
                const float peak=std::max({input,std::abs(dl),std::abs(dr),std::abs(rl),std::abs(rr)});
                b.quiet=peak>1e-6f?0:b.quiet+1;
                if(b.quiet>rate*3){if(b.released){b.owner=0;break;}b.sleeping=true;}
            }
        }
    }
};
}
