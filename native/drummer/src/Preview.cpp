#include <drummer/Preview.h>
#include <algorithm>
namespace drummer {
Preview::Preview(std::unique_ptr<Runtime> r,bool isPattern,double sr,int block):runtime(std::move(r)),rate(sr),pattern(isPattern){
 const int size=std::max(1,block);left.resize((size_t)size);right.resize((size_t)size);
 engine.prepare(rate,size);engine.setPerformance(runtime->performance);
 if(pattern){audition.clips=runtime->config.clips;audition.launchGrid=0;audition.switchGrid=0;auto&key=audition.keys[0];key.enabled=true;key.parts.fill(true);key.zoneCount=1;key.zones[0]={1,0};}
}
void Preview::renderTo(int end){if(end>rendered){engine.process(left.data()+rendered,right.data()+rendered,end-rendered,nullptr,0,{bpm,beat+rendered*bpm/(60*rate),true});rendered=end;}}
void Preview::emit(void* context,const Event&e){auto&p=*static_cast<Preview*>(context);p.renderTo(e.offset);
 if(e.type==Event::hit){for(int key=0;key<128;++key)if(p.runtime->sounds[(size_t)key]&&e.drumNote>=0&&p.runtime->config.keys[(size_t)key].parts[(size_t)e.drumNote])p.engine.drumHit(p.runtime->sounds[(size_t)key],(uint64_t)key+1,e.token,e.velocity,{1,.5f,.5f,0,0});}
 else if(e.type==Event::endHit)p.engine.drumEnd(e.token);
 else if(e.type==Event::panic)p.engine.drumPanic();
}
bool Preview::add(float*l,float*r,int frames,double tempo)noexcept{
 bpm=std::clamp(tempo,20.,400.);
 for(int offset=0;offset<frames;){const int n=std::min(frames-offset,(int)left.size());rendered=0;engine.process(left.data(),right.data(),0,nullptr,0,{bpm,beat,true});
  if(pattern){Midi gate{0,0x91,0,127};scheduler.process(audition,{beat,bpm,rate,true},n,&gate,started?0:1,emit,this);}
  else if(!started){for(int key=0;key<128;++key)if(runtime->sounds[(size_t)key]){engine.drumHit(runtime->sounds[(size_t)key],1,1,1,{1,.5f,.5f,0,0});break;}}
  started=true;renderTo(n);for(int i=0;i<n;++i){l[offset+i]+=left[(size_t)i];r[offset+i]+=right[(size_t)i];}
  offset+=n;beat+=n*bpm/(60*rate);elapsed+=n/rate;
 }
 return pattern||elapsed<.05||engine.activeVoices()>0;
}
}
