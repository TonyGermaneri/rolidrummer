#include <drummer/Scheduler.h>
#include <algorithm>
#include <cmath>
#include <limits>
namespace drummer {
namespace {
double boundary(double beat, double step) { return step > 0 ? std::ceil((beat - 1e-10) / step) * step : beat; }
float source(const Expression& e, int s) { switch(s) { case 0:return e.strike;case 1:return e.glide;case 3:return e.press;case 4:return e.lift;default:return e.slide; } }
int zoneAt(const Key& k, float x) { for(int z=0;z<k.zoneCount-1;++z) if(x<k.zones[(size_t)z].upper) return z;return k.zoneCount-1; }
uint64_t token(uint32_t clip,int note,int64_t cycle) {
    // Stable across block sizes; the off for a note extending past wrap names its original onset.
    uint64_t h = uint64_t(cycle) * 0x9e3779b97f4a7c15ULL;
    h ^= uint64_t(clip) * 0xbf58476d1ce4e5b9ULL; h ^= uint64_t(note+1) * 0x94d049bb133111ebULL;
    return h == 0 ? 1 : h;
}
}
void prepareClip(Clip& clip) {
    clip.timeline.clear();clip.timeline.reserve(clip.notes.size()*2);
    if(!(clip.length>0))return;
    for(int n=0;n<(int)clip.notes.size();++n)for(int off=0;off<2;++off){
        const double beat=clip.notes[(size_t)n].beat+(off?clip.notes[(size_t)n].duration:0);
        const int64_t shift=(int64_t)std::floor(beat/clip.length);
        clip.timeline.push_back({beat-shift*clip.length,n,shift,off!=0});
    }
    std::sort(clip.timeline.begin(),clip.timeline.end(),[](const auto&a,const auto&b){if(a.beat!=b.beat)return a.beat<b.beat;return a.off>b.off;});
}
void Scheduler::reset() noexcept { fingers = {};wasPlaying = false;expected = 0; }
void Scheduler::input(const Configuration& cfg,const Midi& m,double beat,Sink sink,void* ctx) noexcept {
    if(m.a>127 || m.b>127)return;
    const int ch=m.status&15, op=m.status&240;
    auto& f=fingers[(size_t)ch];
    Event e; e.offset=m.offset;e.channel=ch;e.owner=f.owner;e.key=f.key;e.values=f.values;
    if(op==0xb0 && (m.a==120 || m.a==123)) {
        if(ch==cfg.masterChannel){e.type=Event::panic;sink(ctx,e);fingers={};}
        else {if(f.held){e.type=m.a==120?Event::killContact:Event::releaseContact;sink(ctx,e);}f.held=false;f.down=false;}
        return;
    }
    // CC74 is per-channel note expression, never a global controller or a
    // broadcast. A master-channel CC74 cannot move member-channel contacts.
    if(op==0xb0 && ch==cfg.masterChannel && m.a!=74) {
        e.type=Event::globalCC;e.cc=m.a;e.value=m.b/127.f;sink(ctx,e);
        return;
    }
    if(op==0x90 && m.b>0) {
        if(f.held) { e.type=Event::releaseContact;sink(ctx,e); }
        const auto& k=cfg.keys[m.a];
        f.down=true;f.held=k.enabled;f.key=m.a;f.owner=++generation;
        f.values.strike=m.b/127.f;f.values.lift=0;
        f.zone=f.wanted=zoneAt(k,source(f.values,k.zoneSource));
        f.opens=boundary(beat,cfg.launchGrid);f.switches=f.opens;
        return;
    }
    if(op==0x80 || (op==0x90 && m.b==0)) {
        if(f.key==m.a)f.down=false;
        if(f.held && f.key==m.a) { f.values.lift=m.b/127.f;e.values=f.values;e.type=Event::releaseContact;sink(ctx,e);f.held=false; }
        return;
    }
    if(op==0xe0){const int raw=(int(m.b)<<7)|m.a;f.values.glide=.5f+.5f*float(raw-8192)/float(raw<8192?8192:8191);}
    else if(op==0xd0) f.values.press=m.a/127.f;
    else if(op==0xa0 && f.key==m.a) f.values.press=m.b/127.f;
    else if(op==0xb0 && m.a==74) f.values.slide=m.b/127.f;
    else return;
    if(!f.held) return; // Pre-note expression remains available for the next contact.
    const auto& k=cfg.keys[(size_t)f.key];
    const float x=source(f.values,k.zoneSource);
    int next=zoneAt(k,x);
    if(next>f.zone && x<k.zones[(size_t)f.zone].upper+cfg.hysteresis) next=f.zone;
    if(next<f.zone && x>k.zones[(size_t)(f.zone-1)].upper-cfg.hysteresis) next=f.zone;
    if(next!=f.wanted) { f.wanted=next;f.switches=boundary(beat,cfg.switchGrid); }
    e.type=Event::expression;e.values=f.values;sink(ctx,e);
}
void Scheduler::process(const Configuration& cfg,Clock clock,int frames,const Midi* midi,int count,Sink sink,void* ctx) noexcept {
    evaluations=0;overflow=false;dueCount=0;
    if(frames<=0 || !(clock.sampleRate>0) || !(clock.bpm>0) || !std::isfinite(clock.ppq)) return;
    const double step=clock.bpm/(60*clock.sampleRate), end=clock.ppq+frames*step;
    const bool discontinuity=clock.discontinuity || (wasPlaying && clock.playing && std::abs(clock.ppq-expected)>step*2);
    if((wasPlaying && !clock.playing) || discontinuity) {
        Event e;e.type=Event::panic;sink(ctx,e);
        for(auto& f:fingers) if(f.held) { f.opens=boundary(clock.ppq,cfg.launchGrid);f.switches=f.opens; }
    }
    if(!wasPlaying && clock.playing) for(auto& f:fingers) if(f.held) { f.opens=boundary(clock.ppq,cfg.launchGrid);f.switches=f.opens; }
    wasPlaying=clock.playing;expected=end;
    if(clock.playing) for(int c=0;c<(int)cfg.clips.size();++c) {
        const auto& clip=cfg.clips[(size_t)c];
        if(!(clip.length>0) || !std::isfinite(clip.length)) continue;
        ++evaluations;
        // A rounded sample at the right edge belongs to the next block.
        // Look back another sample for sub-sample notes whose off is extended
        // to one sample after their onset; this also avoids early edge clamping.
        double from=clock.ppq-step*1.5;const double finish=end-step*.5;
        while(from<finish-1e-12) {
            const int64_t cycle=(int64_t)std::floor((from-clip.origin)/clip.length);
            const double origin=clip.origin+cycle*clip.length,phase=std::max(0.,from-origin);
            const double until=std::min(finish,origin+clip.length);
            if(until<=from)break;
            auto first=std::lower_bound(clip.timeline.begin(),clip.timeline.end(),phase-1e-10,[](const ClipEvent& e,double p){return e.beat<p;});
            for(auto it=first;it!=clip.timeline.end() && origin+it->beat<until+1e-10;++it) {
                int offset=(int)std::floor((origin+it->beat-clock.ppq)/step+.5+1e-9);
                if(it->off){
                    const double onset=clip.origin+(cycle-it->cycleShift)*clip.length+clip.notes[(size_t)it->note].beat;
                    offset=std::max(offset,(int)std::floor((onset-clock.ppq)/step+.5+1e-9)+1);
                }
                if(offset<0 || offset>=frames)continue;
                if(dueCount==capacity){overflow=true;break;}
                due[(size_t)dueCount++]={offset,c,it->note,cycle-it->cycleShift,it->off};
            }
            if(overflow)break;
            from=until;
        }
        if(overflow) break;
    }
    if(overflow) { Event e;e.type=Event::panic;sink(ctx,e);return; }
    std::sort(due.begin(),due.begin()+dueCount,[](const Due& a,const Due& b){
        if(a.offset!=b.offset)return a.offset<b.offset;if(a.off!=b.off)return a.off; if(a.clip!=b.clip)return a.clip<b.clip;return a.note<b.note;
    });
    int mi=0,di=0;
    // A zone transition is itself an event, even if neither clip has a hit there.
    for(;;) {
        int transition=frames,contact=-1;
        for(int ch=0;ch<maxContacts;++ch){auto& f=fingers[(size_t)ch];if(!f.held)continue;
            const auto& key=cfg.keys[(size_t)f.key];
            if(!key.enabled){Event e;e.type=Event::releaseContact;e.owner=f.owner;e.key=f.key;e.values=f.values;sink(ctx,e);f.held=false;continue;}
            f.zone=std::clamp(f.zone,0,key.zoneCount-1);f.wanted=std::clamp(f.wanted,0,key.zoneCount-1);
            if(f.zone!=f.wanted){int at=std::max(0,(int)std::llround((f.switches-clock.ppq)/step));if(at<transition){transition=at;contact=ch;}}
        }
        const int nextMidi=mi<count?midi[mi].offset:frames,nextDue=di<dueCount?due[(size_t)di].offset:frames;
        if(contact>=0 && transition<=nextMidi && transition<=nextDue){auto& f=fingers[(size_t)contact];Event e;e.type=Event::switchZone;e.offset=transition;e.owner=f.owner;e.key=f.key;e.values=f.values;sink(ctx,e);f.zone=f.wanted;continue;}
        if(mi==count && di==dueCount)break;
        if(mi<count && (di==dueCount || midi[mi].offset<=due[(size_t)di].offset)) {
            auto m=midi[mi++];m.offset=std::clamp(m.offset,0,frames-1);
            input(cfg,m,clock.ppq+m.offset*step,sink,ctx);continue;
        }
        const auto& d=due[(size_t)di++];const auto& clip=cfg.clips[(size_t)d.clip];const auto& note=clip.notes[(size_t)d.note];
        Event e;e.offset=d.offset;e.token=token(clip.id,d.note,d.cycle);
        if(d.off) { e.type=Event::endHit;sink(ctx,e);continue; }
        const double beat=clock.ppq+d.offset*step;
        for(int ch=0;ch<maxContacts;++ch) {
            auto& f=fingers[(size_t)ch];if(!f.held || beat+step*.51<f.opens)continue;
            if(beat+step*.51>=f.switches) f.zone=f.wanted;
            const auto& k=cfg.keys[(size_t)f.key];
            if(!k.parts[(size_t)note.note] || k.zones[(size_t)f.zone].clip!=d.clip)continue;
            e.type=Event::hit;e.key=f.key;e.channel=ch;e.owner=f.owner;e.drumNote=note.note;e.velocity=note.velocity;e.values=f.values;sink(ctx,e);
        }
    }
}
}
