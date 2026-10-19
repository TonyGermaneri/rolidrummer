<script setup lang="ts">
import {computed,ref,onMounted,useId} from 'vue'
import {state} from '../store'
import {performanceView,useRollClock,elapsedBeats,clearPerformance} from '../performance'
import {blackPitch,liveRectangle} from '../lib/pianoRoll'
import {midiNoteName} from '../lib/format'
const time=useRollClock(),bars=ref(4),scroll=ref<HTMLElement>()
const clipId=`live-roll-${useId().replace(/[^a-z0-9-]/gi,'')}`
const meter=computed(()=>(state.patch.neural?.numerator??4)*4/(state.patch.neural?.denominator??4))
const windowBeats=computed(()=>bars.value*meter.value)
const beat=computed(()=>performanceView.beat+elapsedBeats(time.value,performanceView.received,performanceView.bpm))
const origin=computed(()=>Math.floor(performanceView.beat/1024)*1024)
const scale=computed(()=>900/windowBeats.value)
const shift=computed(()=>900-(beat.value-origin.value)*scale.value)
const paths=computed(()=>{
 const groups=new Map<string,string>()
 for(const note of performanceView.notes){
  const r=liveRectangle(note,origin.value,scale.value),hue=((note.key%12)*30+190)%360,bucket=Math.min(3,Math.floor(note.velocity*4)),color=`hsl(${hue} 55% ${48+bucket*9}%)`
  groups.set(color,(groups.get(color)??'')+`M${r.x} ${r.y}h${r.width}v${r.height}h${-r.width}Z`)
 }
 return [...groups].map(([color,path])=>({color,path}))
})
const grid=computed(()=>{
 const step=Math.max(.25,2**Math.ceil(Math.log2(windowBeats.value/80))),start=Math.floor((beat.value-windowBeats.value)/step)*step
 return Array.from({length:Math.ceil(windowBeats.value/step)+10},(_,i)=>{const b=start+i*step;return{beat:b,x:900+(b-beat.value)*scale.value,bar:Math.abs(b/meter.value-Math.round(b/meter.value))<.0001}}).filter(g=>g.x>=0&&g.x<=1000)
})
const pitches=Array.from({length:128},(_,i)=>127-i)
onMounted(()=>{if(scroll.value)scroll.value.scrollTop=(127-58)*12})
</script>
<template><section class="live-piano-roll workspace-ui" aria-label="Live piano roll">
 <header class="roll-toolbar"><div><b>PIANO ROLL</b><span>Live drum output</span></div><label>HISTORY<select v-model.number="bars" aria-label="Piano roll history"><option v-for="n in [1,2,4,8]" :key="n" :value="n">{{n}} {{n===1?'bar':'bars'}}</option></select></label><button @click="clearPerformance">Clear</button></header>
 <div ref="scroll" class="roll-scroll"><div class="ruler"><span>MIDI</span><svg viewBox="0 0 1000 22" preserveAspectRatio="none" aria-hidden="true"><g v-for="line in grid.filter(g=>g.bar)" :key="line.beat"><text :x="line.x+3" y="15">{{Math.floor(line.beat/meter)+1}}</text></g><path d="M895 0h10l-5 7Z" class="transport"/></svg></div>
 <div class="roll-stage">
  <svg class="piano-keys" viewBox="0 0 48 1536" preserveAspectRatio="none" aria-label="MIDI note keyboard"><g v-for="(pitch,i) in pitches" :key="pitch"><title>{{midiNoteName(pitch)}} · MIDI {{pitch}}</title><rect x="0" :y="i*12" width="48" height="12" :class="blackPitch(pitch)?'black-key':'white-key'"/><text x="44" :y="i*12+9" :class="{dark:blackPitch(pitch)}">{{midiNoteName(pitch)}}</text></g></svg>
  <svg class="live-notes" viewBox="0 0 1000 1536" preserveAspectRatio="none" role="img" :aria-label="`${performanceView.notes.length} played drum notes`"><defs><clipPath :id="clipId"><rect x="0" y="0" width="900" height="1536"/></clipPath></defs>
   <rect v-for="(pitch,i) in pitches" :key="pitch" x="0" :y="i*12" width="1000" height="12" :class="blackPitch(pitch)?'black-row':'white-row'"/>
   <line v-for="line in grid" :key="line.beat" :x1="line.x" :x2="line.x" y1="0" y2="1536" :class="line.bar?'bar-line':'beat-line'" vector-effect="non-scaling-stroke"/>
   <g :clip-path="`url(#${clipId})`"><g :transform="`translate(${shift} 0)`"><path v-for="group in paths" :key="group.color" :d="group.path" :fill="group.color" class="played-notes"/></g></g>
   <rect x="900" y="0" width="100" height="1536" class="future"/><line x1="900" x2="900" y1="0" y2="1536" class="transport" vector-effect="non-scaling-stroke"/>
  </svg>
 </div></div>
 <footer><span>Notes grow from the playhead · color follows the source keywave</span><span v-if="performanceView.dropped">{{performanceView.dropped}} display events skipped</span><span v-else>{{performanceView.notes.length}} notes</span></footer>
</section></template>
<style scoped>
.live-piano-roll{height:100%;min-height:0;display:flex;flex-direction:column;border:1px solid var(--line);background:var(--lcd-bg);border-radius:3px;overflow:hidden}.roll-toolbar{display:flex;align-items:center;gap:10px;padding:8px 10px;background:var(--panel-lo);border-bottom:1px solid var(--line);flex:none}.roll-toolbar>div{display:flex;align-items:center;gap:12px;flex:1}.roll-toolbar b{font:10px monospace;letter-spacing:.12em}.roll-toolbar span,.roll-toolbar label{font:10px monospace;color:var(--text-dim)}.roll-toolbar label{display:flex;align-items:center;gap:8px}.roll-toolbar select,.roll-toolbar button{font:11px monospace;padding:3px 7px}.ruler{position:sticky;top:0;z-index:2;background:var(--lcd-bg);display:flex;flex:none;height:22px;border-bottom:1px solid var(--line)}.ruler>span{width:48px;flex:none;text-align:center;font:8px/22px monospace;color:var(--text-dim)}.ruler svg{flex:1;min-width:0;margin-right:0}.ruler text{fill:var(--text-dim);font:10px monospace}.roll-scroll{overflow-y:scroll;flex:1;min-height:0;scrollbar-gutter:stable}.roll-stage{display:flex;height:1536px}.piano-keys{width:48px;flex:none;height:1536px}.live-notes{height:1536px;flex:1;min-width:0}.white-key{fill:#a7adb3;stroke:#5a6066;stroke-width:.5}.black-key{fill:#24292f;stroke:#11151a;stroke-width:.5}.piano-keys text{fill:#272c32;font:8px monospace;text-anchor:end}.piano-keys text.dark{fill:#aab2bc}.black-row{fill:#ffffff02;stroke:var(--line);stroke-width:.4}.white-row{fill:#ffffff07;stroke:var(--line);stroke-width:.4}.bar-line{stroke:var(--text-faint);stroke-width:1}.beat-line{stroke:var(--line);stroke-width:1}.future{fill:#0003}.transport{fill:var(--c-primary);stroke:var(--c-primary);stroke-width:1.5}footer{display:flex;justify-content:space-between;padding:5px 9px;font:9px monospace;color:var(--text-dim);border-top:1px solid var(--line);flex:none}
</style>
