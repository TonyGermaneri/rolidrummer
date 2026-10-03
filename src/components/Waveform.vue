<script setup lang="ts">
import { computed } from 'vue'
import { state } from '../store'
const props=defineProps<{peaks:number[];duration:number;note?:number;clipIn:number;clipOut:number}>()
const points=computed(()=>props.peaks.map((p,i)=>`${i*1000/Math.max(1,props.peaks.length-1)},${40-p*36}`).join(' '))
const reflected=computed(()=>props.peaks.map((p,i)=>`${i*1000/Math.max(1,props.peaks.length-1)},${40+p*36}`).join(' '))
const voices=computed(()=>state.telemetry.playheads?.filter(v=>v.note===props.note)??[])
const marks=computed(()=>state.telemetry.grainMarks?.filter(g=>voices.value.some(v=>v.id===g.voice))??[])
const x=(seconds:number)=>Math.max(0,Math.min(1000,seconds/Math.max(.00001,props.duration)*1000))
</script>
<template><svg class="waveform" viewBox="0 0 1000 80" preserveAspectRatio="none" aria-label="Sound waveform and live playback positions">
 <line x1="0" y1="40" x2="1000" y2="40" class="wave-center"/>
 <polyline :points="points" class="wave-shape"/><polyline :points="reflected" class="wave-shape"/>
 <rect x="0" y="0" :width="clipIn*1000" height="80" class="wave-outside"/><rect :x="clipOut*1000" y="0" :width="(1-clipOut)*1000" height="80" class="wave-outside"/>
 <path :d="`M ${clipIn*1000} 80 V 0 h 7 l -7 7 M ${clipOut*1000} 80 V 0 h -7 l 7 7`" class="wave-bounds"/>
 <g v-for="(g,i) in marks" :key="`${g.voice}-${g.when}-${i}`" :opacity="Math.max(0,Math.min(.6,(g.when+g.dur+.35-state.telemetry.audioTime)/.35))">
  <rect :x="x(g.pos)" :y="Math.max(0,35+g.pan*25-g.gain*20)" :width="Math.max(2,x(g.pos+g.len)-x(g.pos))" :height="Math.max(3,g.gain*40)" class="grain-mark"/>
 </g>
 <g v-for="v in voices.filter(v=>!v.grains)" :key="v.id" class="wave-playhead" :data-voice="v.id" :opacity=".55+.45*v.velocity">
  <line :x1="x(v.pos)" y1="0" :x2="x(v.pos)" y2="80"/><path :d="`M ${x(v.pos)-5} 0 h 10 l -5 7 Z`"/>
 </g>
</svg></template>
<style scoped>
.wave-center{stroke:var(--text-faint)}.wave-shape{fill:none;stroke:var(--c-secondary);stroke-width:1.3}.wave-outside{fill:var(--lcd-bg);opacity:.75}.wave-bounds{fill:none;stroke:var(--c-primary);stroke-width:1}.wave-playhead{stroke:var(--c-primary);fill:var(--c-primary);stroke-width:2;filter:drop-shadow(0 0 3px var(--c-primary))}.grain-mark{fill:var(--c-secondary)}
</style>
