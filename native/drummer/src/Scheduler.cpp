#include <drummer/Scheduler.h>
#include <algorithm>
#include <cmath>
#include <limits>
namespace drummer {
namespace {
double boundary(double beat, double step) { return step > 0 ? std::ceil((beat - 1e-10) / step) * step : beat; }
float source(const Expression& e, int s) { switch(s) { case 0:return e.strike;case 1:return e.glide;case 3:return e.press;case 4:return e.lift;default:return e.slide; } }
int zoneAt(const Key& k, float x) { for(int z=0;z<k.zoneCount-1;++z) if(x<k.zones[(size_t)z].upper) return z;return k.zoneCount-1; }
constexpr double intentTick = 1.0/32;
PatternIntent intentTarget(const Expression& e) {return {{.65f*e.press+.35f*e.strike,e.press,e.glide,e.strike}};}
const PatternGroup* group(const Configuration& cfg,const Zone& zone) {
    return zone.group>=0 && zone.group<(int)cfg.groups.size()?&cfg.groups[(size_t)zone.group]:nullptr;
}
int selectedClip(const Configuration& cfg,const Key& key,const Contact& contact) {
    const auto& zone=key.zones[(size_t)contact.zone];
    if(key.intentEnabled)if(const auto* candidates=group(cfg,zone))
        for(int i=0;i<std::clamp(candidates->count,0,maxCandidates);++i)if(candidates->candidates[(size_t)i].clip==contact.clip)return contact.clip;
    return zone.clip;
}
int chooseClip(const Configuration& cfg,const Key& key,int region,const PatternIntent& intent,int previous) {
    const auto& zone=key.zones[(size_t)region];
    if(!key.intentEnabled)return zone.clip;
    const auto* candidates=group(cfg,zone);if(!candidates || candidates->count<=0)return zone.clip;
    double best=std::numeric_limits<double>::infinity();int result=zone.clip;
    for(int i=0;i<std::clamp(candidates->count,0,maxCandidates);++i){
        const auto& candidate=candidates->candidates[(size_t)i];if(candidate.clip<0 || candidate.clip>=(int)cfg.clips.size())continue;
        double distance=candidate.clip==previous?0:key.intentPenalty;
        for(int d=0;d<intentDimensions;++d){const double delta=candidate.position[(size_t)d]-intent[(size_t)d];distance+=key.intentWeights[(size_t)d]*delta*delta;}
        if(distance<best-1e-10 || (std::abs(distance-best)<=1e-10 && candidate.clip<result)){best=distance;result=candidate.clip;}
    }
    return result;
}
void advanceIntent(const Key& key,Contact& contact,double beat,double bpm) {
    if(!key.intentEnabled)return;
    const auto target=intentTarget(contact.values);
    const double seconds=std::max(0.,beat-contact.intentBeat)*60/bpm;
    const float alpha=key.intentSlewMs<=0?1.f:(float)-std::expm1(-std::min(1.,seconds)/(key.intentSlewMs*.001));
    for(int d=0;d<intentDimensions;++d)contact.intent[(size_t)d]+=alpha*(target[(size_t)d]-contact.intent[(size_t)d]);
    contact.intentBeat=beat;
}
void choose(const Configuration& cfg,const Key& key,Contact& contact,double beat) {
    const float value=source(contact.values,key.zoneSource);
    int next=zoneAt(key,value);
    if(next>contact.zone && value<key.zones[(size_t)contact.zone].upper+cfg.hysteresis)next=contact.zone;
    if(next<contact.zone && value>key.zones[(size_t)(contact.zone-1)].upper-cfg.hysteresis)next=contact.zone;
    const int previous=next==contact.zone?selectedClip(cfg,key,contact):-1;
    const int clip=chooseClip(cfg,key,next,contact.intent,previous);
    if(next!=contact.wanted || clip!=contact.wantedClip){
        contact.wanted=next;contact.wantedClip=clip;
        const double earliest=key.intentEnabled&&next==contact.zone?std::max(beat,contact.changedAt+key.intentDwell):beat;
        contact.switches=boundary(earliest,cfg.switchGrid);
    }
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
void Scheduler::reset() noexcept { fingers = {};sustained = {};pedals = {};latches={};latchGrid=0;wasPlaying = false;expected = 0;pendingOffCount=0;keyboardMode=KeyboardMode::normal; }
bool Scheduler::hasHeldNotes() const noexcept {
    for(const auto& latch:latches)if(latch.active)return true;
    for (const auto& f : fingers) if (f.held) return true;
    for (const auto& f : sustained) if (f.held) return true;
    return false;
}
void Scheduler::reconfigure(const Configuration& previous,const Configuration& next,double beat,Sink sink,void* ctx) noexcept {
    for(auto& latch:latches)if(latch.active&&!next.keys[(size_t)latch.next.key].enabled)latch.active=false;
    auto identity=[](const Configuration& cfg,int clip){return clip>=0&&clip<(int)cfg.clips.size()?cfg.clips[(size_t)clip].id:uint64_t(0);};
    for(int i=0;i<maxContacts+maxSustained;++i){auto& f=contact(i);if((!f.down&&!f.held)||f.suppressed||f.key<0||f.key>=128)continue;
        const auto& old=previous.keys[(size_t)f.key];const auto& key=next.keys[(size_t)f.key];
        const bool wasHeld=f.held;const int oldZone=std::clamp(f.zone,0,std::max(0,old.zoneCount-1));
        auto oldContact=f;oldContact.zone=oldZone;
        const auto oldId=wasHeld&&old.enabled?identity(previous,selectedClip(previous,old,oldContact)):0;
        if(!key.enabled){f.clip=f.wantedClip=-1;continue;} // process emits the owner's release.
        if(!wasHeld&&hasPendingLatch(f.key)&&latches[(size_t)f.key].next.owner==f.owner)continue;
        bool sameLayout=old.enabled&&old.zoneCount==key.zoneCount&&old.zoneSource==key.zoneSource;
        for(int zone=0;sameLayout&&zone<key.zoneCount;++zone)sameLayout=old.zones[(size_t)zone].upper==key.zones[(size_t)zone].upper;
        f.held=wasHeld||f.down;if(!wasHeld){f.opens=beat;f.intent=intentTarget(f.values);f.intentBeat=beat;f.nextIntent=beat+intentTick;}
        auto select=[&](int zone,uint64_t preferred){
            if(key.intentEnabled)if(const auto* choices=group(next,key.zones[(size_t)zone]))
                for(int n=0;preferred&&n<std::clamp(choices->count,0,maxCandidates);++n){const int clip=choices->candidates[(size_t)n].clip;if(identity(next,clip)==preferred)return clip;}
            return chooseClip(next,key,zone,f.intent,-1);
        };
        const bool keepSwitch=sameLayout&&(f.wanted!=oldZone||identity(previous,f.wantedClip)!=oldId)&&f.switches>beat+1e-10;
        const int wanted=keepSwitch?std::clamp(f.wanted,0,key.zoneCount-1):-1;
        const auto wantedId=keepSwitch?identity(previous,f.wantedClip):0;
        f.zone=sameLayout?oldZone:zoneAt(key,source(f.values,key.zoneSource));f.clip=select(f.zone,oldId);
        if(keepSwitch){f.wanted=wanted;f.wantedClip=select(wanted,wantedId);}
        else{f.wanted=f.zone;f.wantedClip=f.clip;f.switches=beat;}
        if(!wasHeld||oldId!=identity(next,f.clip)||oldZone!=f.zone){
            f.changedAt=beat;
            if(wasHeld){Event e;e.type=Event::switchZone;e.owner=f.owner;e.key=f.key;e.channel=f.channel;e.values=f.values;sink(ctx,e);}
        }
    }
    enforceKeyOwnership(next.keyboardMode,0,sink,ctx);
}
void Scheduler::enforceKeyOwnership(KeyboardMode mode,int offset,Sink sink,void* ctx) noexcept {
    std::array<Contact*,128> owners{};
    for(int i=0;i<maxContacts+maxSustained;++i){auto& f=contact(i);if(!f.held)continue;
        auto*& owner=owners[(size_t)f.key];
        if(!owner){owner=&f;f.toggled=mode==KeyboardMode::toggle;continue;}
        auto* old=owner->owner<f.owner?owner:&f;if(old==owner)owner=&f;
        old->suppressed=old->down;release(*old,offset,Event::killContact,sink,ctx);owner->toggled=mode==KeyboardMode::toggle;
    }
}
void Scheduler::release(Contact& f,int offset,Event::Type type,Sink sink,void* ctx) noexcept {
    if(f.held){Event e;e.type=type;e.offset=offset;e.owner=f.owner;e.key=f.key;e.channel=f.channel;e.values=f.values;e.values.lift=f.releaseVelocity;sink(ctx,e);}
    f.held=false;f.toggled=false;
}
bool Scheduler::physicalKeyDown(int key) const noexcept {
    for(const auto& f:fingers)if(f.down&&!f.suppressed&&f.key==key)return true;
    return false;
}
bool Scheduler::queueLatch(const Configuration& cfg,const Midi& m,double beat,Sink sink,void* ctx) noexcept {
    if(keyboardMode==KeyboardMode::normal||cfg.latchGrid<=0||!cfg.keys[m.a].enabled)return false;
    auto& f=fingers[(size_t)(m.status&15)];const auto& key=cfg.keys[m.a];
    auto values=f.values;values.strike=m.b/127.f;values.lift=0;
    const int zone=zoneAt(key,source(values,key.zoneSource));auto& latch=latches[m.a];
    const Contact* owner=nullptr;for(int i=0;i<maxContacts+maxSustained;++i){const auto& old=contact(i);if(old.held&&old.key==m.a&&(!owner||old.owner>owner->owner))owner=&old;}
    const bool playing=latch.active?latch.start:owner!=nullptr;
    const int playingZone=latch.active?latch.next.zone:owner?owner->zone:-1;
    const bool start=keyboardMode!=KeyboardMode::toggle||!playing||playingZone!=zone;
    const bool cancel=latch.active&&((!start&&!owner)||(start&&owner&&owner->zone==zone));
    if(f.held)lift(f,m.offset,sink,ctx);
    f={};f.down=true;f.key=m.a;f.channel=m.status&15;f.owner=++generation;f.values=values;f.zone=f.wanted=zone;
    f.intent=intentTarget(values);f.intentBeat=beat;f.nextIntent=beat+intentTick;f.changedAt=beat;
    f.clip=f.wantedClip=chooseClip(cfg,key,zone,f.intent,-1);
    latch={f,boundary(beat,cfg.latchGrid),!cancel,start,Event::killContact};
    return true;
}
void Scheduler::applyLatch(const Configuration& cfg,Latch& latch,int offset,double beat,Sink sink,void* ctx) noexcept {
    const auto queued=latch.next;latch.active=false;
    for(int i=0;i<maxContacts+maxSustained;++i){auto& old=contact(i);if(old.held&&old.key==queued.key){old.suppressed=old.down;release(old,offset,latch.start?Event::killContact:latch.releaseType,sink,ctx);}}
    Contact* physical=nullptr;for(auto& f:fingers)if(f.owner==queued.owner&&f.down&&!f.suppressed){physical=&f;break;}
    if(!latch.start){if(physical)physical->suppressed=true;return;}
    auto next=physical?*physical:queued;next.down=physical!=nullptr;next.held=true;next.suppressed=false;next.toggled=keyboardMode==KeyboardMode::toggle;
    const auto& key=cfg.keys[(size_t)next.key];next.zone=next.wanted=zoneAt(key,source(next.values,key.zoneSource));next.intent=intentTarget(next.values);
    next.clip=next.wantedClip=chooseClip(cfg,key,next.zone,next.intent,-1);next.opens=next.switches=next.changedAt=next.intentBeat=beat;next.nextIntent=beat+intentTick;
    if(physical){*physical=next;return;}
    auto slot=std::find_if(sustained.begin(),sustained.end(),[](const auto& f){return !f.held;});
    if(slot==sustained.end()){slot=std::min_element(sustained.begin(),sustained.end(),[](const auto& a,const auto& b){return a.owner<b.owner;});release(*slot,offset,Event::releaseContact,sink,ctx);}
    *slot=next;
}
void Scheduler::lift(Contact& f,int offset,Sink sink,void* ctx) noexcept {
    f.down=false;
    if(!f.held)return;
    if(keyboardMode==KeyboardMode::normal && !pedalDown(f.channel)){release(f,offset,Event::releaseContact,sink,ctx);return;}
    auto slot=std::find_if(sustained.begin(),sustained.end(),[](const auto& c){return !c.held;});
    if(slot==sustained.end()){
        slot=std::min_element(sustained.begin(),sustained.end(),[](const auto&a,const auto&b){return a.owner<b.owner;});
        release(*slot,offset,Event::releaseContact,sink,ctx);
    }
    *slot=f;f.held=false;f.toggled=false; // Keep its owner/expression independent when the MPE channel is reused.
}
void Scheduler::input(const Configuration& cfg,const Midi& m,double beat,double bpm,Sink sink,void* ctx) noexcept {
    if(m.a>127 || m.b>127)return;
    const int ch=m.status&15, op=m.status&240;
    auto& f=fingers[(size_t)ch];
    Event e; e.offset=m.offset;e.channel=ch;e.owner=f.owner;e.key=f.key;e.values=f.values;
    if(op==0xb0 && (m.a==120 || m.a==123)) {
        if(ch==cfg.masterChannel){e.type=Event::panic;sink(ctx,e);fingers={};sustained={};pedals={};latches={};pendingOffCount=0;}
        else {const auto type=m.a==120?Event::killContact:Event::releaseContact;
            release(f,m.offset,type,sink,ctx);f.down=false;pedals[(size_t)ch]=false;
            for(auto& held:sustained)if(held.channel==ch)release(held,m.offset,type,sink,ctx);
            for(auto& latch:latches)if(latch.active&&latch.next.channel==ch)latch.active=false;
        }
        return;
    }
    if(op==0xb0 && m.a==pedalCC) {
        pedals[(size_t)ch]=m.b>=64;
        if(m.b<64){
            for(auto& latch:latches)if(latch.active&&latch.start&&(ch==cfg.masterChannel||latch.next.channel==ch)&&!physicalKeyDown(latch.next.key)&&!pedalDown(latch.next.channel))latch.active=false;
            for(auto& held:sustained)if(held.held&&(ch==cfg.masterChannel||held.channel==ch)&&!pedalDown(held.channel)&&!physicalKeyDown(held.key)){
                if(keyboardMode!=KeyboardMode::normal&&cfg.latchGrid>0)latches[(size_t)held.key]={held,boundary(beat,cfg.latchGrid),true,false,Event::releaseContact};
                else release(held,m.offset,Event::releaseContact,sink,ctx);
            }
        }
        return;
    }
    // CC74 is per-channel note expression, never a global controller or a
    // broadcast. A master-channel CC74 cannot move member-channel contacts.
    if(op==0xb0 && ch==cfg.masterChannel && m.a!=74) {
        e.type=Event::globalCC;e.cc=m.a;e.value=m.b/127.f;sink(ctx,e);
        return;
    }
    if(op==0x90 && m.b>0) {
        if(queueLatch(cfg,m,beat,sink,ctx))return;
        const auto& k=cfg.keys[m.a];
        auto values=f.values;values.strike=m.b/127.f;values.lift=0;
        const int zone=zoneAt(k,source(values,k.zoneSource));
        bool stop=false;
        if(k.enabled){
            // Ownership is by key, independent of the member channel allocated to this press.
            const Contact* owner=nullptr;
            for(int i=0;i<maxContacts+maxSustained;++i){const auto& old=contact(i);
                if(old.held&&old.key==m.a&&(!owner||old.owner>owner->owner))owner=&old;
            }
            stop=keyboardMode==KeyboardMode::toggle&&owner&&owner->zone==zone;
            for(int i=0;i<maxContacts+maxSustained;++i){auto& old=contact(i);
                if(old.held&&old.key==m.a){old.suppressed=old.down;release(old,m.offset,Event::killContact,sink,ctx);}
            }
        }
        if(f.held)lift(f,m.offset,sink,ctx);
        f.down=true;f.held=k.enabled&&!stop;f.toggled=f.held&&keyboardMode==KeyboardMode::toggle;f.suppressed=stop;
        f.key=m.a;f.channel=ch;f.owner=++generation;f.values=values;f.releaseVelocity=0;
        f.zone=f.wanted=zone;
        f.intent=intentTarget(f.values);f.intentBeat=beat;f.nextIntent=beat+intentTick;f.changedAt=beat;
        f.clip=f.wantedClip=chooseClip(cfg,k,f.zone,f.intent,k.zones[(size_t)f.zone].clip);
        f.opens=boundary(beat,cfg.launchGrid);f.switches=f.opens;
        return;
    }
    if(op==0x80 || (op==0x90 && m.b==0)) {
        if(f.key==m.a){f.releaseVelocity=m.b/127.f;lift(f,m.offset,sink,ctx);if(latches[m.a].active&&latches[m.a].next.owner==f.owner)latches[m.a].next=f;}
        return;
    }
    if(f.held)advanceIntent(cfg.keys[(size_t)f.key],f,beat,bpm);
    if(op==0xe0){const int raw=(int(m.b)<<7)|m.a;f.values.glide=.5f+.5f*float(raw-8192)/float(raw<8192?8192:8191);}
    else if(op==0xd0) f.values.press=m.a/127.f;
    else if(op==0xa0 && f.key==m.a) f.values.press=m.b/127.f;
    else if(op==0xb0 && m.a==74) f.values.slide=m.b/127.f;
    else return;
    if(!f.held){if(f.key>=0&&latches[(size_t)f.key].active&&latches[(size_t)f.key].next.owner==f.owner)latches[(size_t)f.key].next=f;return;} // Keep queued/pre-note expression.
    const auto& k=cfg.keys[(size_t)f.key];
    if(k.intentSlewMs<=0)advanceIntent(k,f,beat,bpm);
    choose(cfg,k,f,beat);
    e.type=Event::expression;e.values=f.values;sink(ctx,e);
}
void Scheduler::process(const Configuration& cfg,Clock clock,int frames,const Midi* midi,int count,Sink sink,void* ctx) noexcept {
    evaluations=0;overflow=false;dueCount=0;
    if(frames<=0 || !(clock.sampleRate>0) || !(clock.bpm>0) || !std::isfinite(clock.ppq) || !std::isfinite(clock.sampleRate) || !std::isfinite(clock.bpm)) return;
    auto panic=[&]{Event e;e.type=Event::panic;sink(ctx,e);pendingOffCount=0;};
    if(cfg.clips.size()>maxRuntimeClips){overflow=true;panic();return;}
    if(pedalCC!=cfg.sustainCC || pedalMaster!=cfg.masterChannel){
        for(auto& f:sustained)release(f,0,Event::releaseContact,sink,ctx);
        pedals={};pedalCC=cfg.sustainCC;pedalMaster=cfg.masterChannel;
    }
    if(keyboardMode!=cfg.keyboardMode){
        // Turning off a software latch releases only lifted keys; a real pedal still holds its owners.
        for(auto& f:sustained)if(!pedalDown(f.channel))release(f,0,Event::releaseContact,sink,ctx);
        keyboardMode=cfg.keyboardMode;
        for(auto& f:fingers)if(f.down&&!f.held&&!f.suppressed&&hasPendingLatch(f.key)){
            const auto& key=cfg.keys[(size_t)f.key];f.held=key.enabled;f.opens=boundary(clock.ppq,cfg.launchGrid);f.zone=f.wanted=zoneAt(key,source(f.values,key.zoneSource));f.clip=f.wantedClip=chooseClip(cfg,key,f.zone,f.intent,-1);
        }
        latches={};
        for(int i=0;i<maxContacts+maxSustained;++i)contact(i).toggled=false;
        enforceKeyOwnership(keyboardMode,0,sink,ctx);
    }
    const double step=clock.bpm/(60*clock.sampleRate),end=clock.ppq+frames*step;
    const bool discontinuity=clock.discontinuity || (wasPlaying && clock.playing && std::abs(clock.ppq-expected)>step*2);
    if(latchGrid!=cfg.latchGrid||discontinuity){for(auto& latch:latches)if(latch.active)latch.beat=boundary(clock.ppq,cfg.latchGrid);latchGrid=cfg.latchGrid;}
    if((wasPlaying && !clock.playing) || discontinuity){
        panic();
        for(int i=0;i<maxContacts+maxSustained;++i)if(auto& f=contact(i);f.held){f.opens=boundary(clock.ppq,cfg.launchGrid);f.switches=f.opens;f.intentBeat=clock.ppq;f.nextIntent=clock.ppq+intentTick;f.changedAt=clock.ppq;}
    }
    if(!wasPlaying && clock.playing)for(int i=0;i<maxContacts+maxSustained;++i)if(auto& f=contact(i);f.held){f.opens=boundary(clock.ppq,cfg.launchGrid);f.switches=f.opens;f.intentBeat=clock.ppq;f.nextIntent=clock.ppq+intentTick;f.changedAt=clock.ppq;}
    wasPlaying=clock.playing;expected=end;
    std::array<bool,maxRuntimeClips> evaluated{};
    auto offOrder=[](const PendingOff& a,const PendingOff& b){return a.beat!=b.beat?a.beat>b.beat:a.token>b.token;};
    auto offOffset=[&]{if(!pendingOffCount)return frames;const double at=(pendingOffs[0].beat-clock.ppq)/step;return at>=frames-.5-1e-9?frames:at<=0?0:(int)std::floor(at+.5+1e-9);};
    auto sendOff=[&](int offset){
        std::pop_heap(pendingOffs.begin(),pendingOffs.begin()+pendingOffCount,offOrder);
        Event e;e.type=Event::endHit;e.offset=offset;e.token=pendingOffs[(size_t)--pendingOffCount].token;sink(ctx,e);
    };
    int cursor=0,mi=0;
    while(cursor<frames){
        // Transitions precede MIDI at the same sample, just as they precede a
        // coincident onset. No hit is needed to make a quantized switch happen.
        int nextTransition=frames,nextIntent=frames;
        if(clock.playing)for(auto& latch:latches)if(latch.active){
            const double position=(latch.beat-clock.ppq)/step;
            const int at=position>=frames-.5?frames:position<=cursor+.5?cursor:std::max(cursor,(int)std::llround(position));
            if(at==cursor)applyLatch(cfg,latch,cursor,clock.ppq+cursor*step,sink,ctx);
            else nextTransition=std::min(nextTransition,at);
        }
        for(int i=0;i<maxContacts+maxSustained;++i){auto& f=contact(i);if(!f.held)continue;
            const auto& key=cfg.keys[(size_t)f.key];
            if(!key.enabled){release(f,cursor,Event::releaseContact,sink,ctx);continue;}
            f.zone=std::clamp(f.zone,0,key.zoneCount-1);f.wanted=std::clamp(f.wanted,0,key.zoneCount-1);
            if(key.intentEnabled){
                const double tickAt=(f.nextIntent-clock.ppq)/step;
                if(tickAt<=cursor+.5){advanceIntent(key,f,clock.ppq+cursor*step,clock.bpm);choose(cfg,key,f,clock.ppq+cursor*step);f.nextIntent=clock.ppq+cursor*step+intentTick;}
                const double position=(f.nextIntent-clock.ppq)/step;
                nextIntent=std::min(nextIntent,position>=frames-.5?frames:std::max(cursor+1,(int)std::llround(position)));
            }
            if(f.zone==f.wanted && (!key.intentEnabled || f.clip==f.wantedClip))continue;
            const double position=(f.switches-clock.ppq)/step;
            const int at=position>=frames-.5?frames:position<=cursor?cursor:std::max(cursor,(int)std::llround(position));
            if(at==cursor){Event e;e.type=Event::switchZone;e.offset=cursor;e.owner=f.owner;e.key=f.key;e.channel=f.channel;e.values=f.values;sink(ctx,e);f.zone=f.wanted;f.clip=f.wantedClip;f.changedAt=clock.ppq+cursor*step;}
            else nextTransition=std::min(nextTransition,at);
        }
        bool readMidi=false;
        while(mi<count && std::clamp(midi[mi].offset,0,frames-1)<=cursor){auto m=midi[mi++];m.offset=cursor;input(cfg,m,clock.ppq+cursor*step,clock.bpm,sink,ctx);readMidi=true;}
        // A new immediate switch must be applied before collecting this sample.
        if(readMidi)continue;
        const int nextMidi=mi<count?std::clamp(midi[mi].offset,0,frames-1):frames;
        const int finish=std::min({frames,nextMidi,nextTransition,nextIntent});
        if(finish<=cursor)continue;
        dueCount=0;
        if(clock.playing){
            std::array<bool,maxRuntimeClips> active{};
            for(int i=0;i<maxContacts+maxSustained;++i)if(const auto& f=contact(i);f.held){
                const auto& key=cfg.keys[(size_t)f.key];const int clip=selectedClip(cfg,key,f);
                if(clip>=0 && clip<(int)cfg.clips.size())active[(size_t)clip]=true;
            }
            for(int c=0;c<(int)cfg.clips.size();++c){
                if(!active[(size_t)c])continue;
                const auto& clip=cfg.clips[(size_t)c];
                if(!(clip.length>0) || !std::isfinite(clip.length))continue;
                if(!evaluated[(size_t)c]){evaluated[(size_t)c]=true;++evaluations;}
                double from=clock.ppq+(cursor-1.5)*step;const double untilBeat=clock.ppq+(finish-.5)*step;
                while(from<untilBeat-1e-12){
                    const int64_t cycle=(int64_t)std::floor((from-clip.origin)/clip.length);
                    const double origin=clip.origin+cycle*clip.length,phase=std::max(0.,from-origin),until=std::min(untilBeat,origin+clip.length);
                    if(until<=from)break;
                    auto first=std::lower_bound(clip.timeline.begin(),clip.timeline.end(),phase-1e-10,[](const ClipEvent& event,double position){return event.beat<position;});
                    for(auto it=first;it!=clip.timeline.end() && origin+it->beat<until+1e-10;++it){
                        if(it->off)continue;
                        const int offset=(int)std::floor((origin+it->beat-clock.ppq)/step+.5+1e-9);
                        if(offset<cursor || offset>=finish)continue;
                        if(dueCount==capacity){overflow=true;break;}
                        due[(size_t)dueCount++]={offset,c,it->note,cycle-it->cycleShift,false};
                    }
                    if(overflow)break;
                    from=until;
                }
                if(overflow)break;
            }
        }
        if(overflow){panic();return;}
        std::sort(due.begin(),due.begin()+dueCount,[](const Due& a,const Due& b){if(a.offset!=b.offset)return a.offset<b.offset;if(a.clip!=b.clip)return a.clip<b.clip;return a.note<b.note;});
        for(int di=0;di<dueCount;++di){
            const auto& hit=due[(size_t)di];
            while(pendingOffCount && offOffset()<=hit.offset)sendOff(offOffset());
            const auto& clip=cfg.clips[(size_t)hit.clip];const auto& note=clip.notes[(size_t)hit.note];
            const double beat=clock.ppq+hit.offset*step;
            Event e;e.type=Event::hit;e.offset=hit.offset;e.drumNote=note.note;e.velocity=note.velocity;e.duration=note.duration;
            bool fired=false;
            for(int i=0;i<maxContacts+maxSustained;++i){auto& f=contact(i);if(!f.held || beat+step*.51<f.opens)continue;
                const auto& key=cfg.keys[(size_t)f.key];
                if(!key.parts[(size_t)note.note] || selectedClip(cfg,key,f)!=hit.clip)continue;
                if(!fired){e.token=++hitGeneration;if(!e.token)e.token=++hitGeneration;fired=true;}
                e.key=f.key;e.channel=f.channel;e.owner=f.owner;e.values=f.values;sink(ctx,e);
            }
            if(fired){
                if(pendingOffCount==capacity){overflow=true;panic();return;}
                const double onset=clip.origin+hit.cycle*clip.length+note.beat;
                const double off=std::max(onset+note.duration,clock.ppq+(hit.offset+1)*step);
                pendingOffs[(size_t)pendingOffCount++]={off,e.token};
                std::push_heap(pendingOffs.begin(),pendingOffs.begin()+pendingOffCount,offOrder);
            }
        }
        while(pendingOffCount && offOffset()<finish)sendOff(offOffset());
        cursor=finish;
    }
}
}
