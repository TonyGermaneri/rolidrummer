<script setup lang="ts">
import {computed} from 'vue'
import {patternGeometry,blackPitch} from '../lib/pianoRoll'
import {midiNoteName} from '../lib/format'
import type {RollNote} from '../performance'
const props=defineProps<{notes:RollNote[];length:number;parts?:number[];phase?:number;playing?:boolean;mini?:boolean}>()
const geometry=computed(()=>patternGeometry(props.notes,props.length,props.parts))
const rows=computed(()=>Array.from({length:geometry.value.high-geometry.value.low+1},(_,i)=>geometry.value.high-i))
const beats=computed(()=>{const step=Math.max(1,Math.ceil(props.length/32));return Array.from({length:Math.min(32,Math.ceil(props.length/step))},(_,i)=>i*step/props.length*1000)})
const marker=computed(()=>props.phase===undefined?undefined:((props.phase%props.length)+props.length)%props.length/props.length*1000)
</script>
<template><div class="pattern-roll" :class="{mini}" role="img" :aria-label="`${geometry.count} drum notes${playing?', playing':''}`">
 <svg class="roll-keyboard" viewBox="0 0 38 100" preserveAspectRatio="none" aria-hidden="true"><g v-for="(pitch,i) in rows" :key="pitch"><rect :x="0" :y="i*geometry.row" width="38" :height="geometry.row" :class="blackPitch(pitch)?'black':'white'"/><text v-if="!mini&&(pitch%12===0||rows.length<=12)" x="35" :y="(i+.72)*geometry.row">{{midiNoteName(pitch)}}</text></g></svg>
 <svg class="roll-notes" viewBox="0 0 1000 100" preserveAspectRatio="none" aria-hidden="true"><path v-for="(_,i) in rows" :key="`r${i}`" :d="`M0 ${i*geometry.row}H1000`" class="row-line"/><path v-for="x in beats" :key="x" :d="`M${x} 0V100`" class="beat-line"/><path v-for="(path,i) in geometry.paths" :key="i" :d="path" :opacity=".35+i*.09" class="pattern-notes"/><line v-if="marker!==undefined" class="pattern-transport" :class="{playing}" :x1="marker" :x2="marker" y1="0" y2="100" vector-effect="non-scaling-stroke"/></svg>
 <span v-if="!geometry.count&&!mini" class="silent">No notes for this keywave</span>
</div></template>
<style scoped>
.pattern-roll{display:flex;position:relative;height:80px;min-width:0;overflow:hidden;background:var(--lcd-bg);border:1px solid var(--line);border-radius:3px}.roll-keyboard{width:29px;flex:none}.roll-notes{flex:1;min-width:0;height:100%}.black{fill:#22262b;stroke:#101216;stroke-width:.2}.white{fill:#a0a6ac;stroke:#4c5158;stroke-width:.2}text{fill:#20262c;font:5px monospace;text-anchor:end}.row-line{stroke:var(--line);stroke-width:.35}.beat-line{stroke:var(--text-faint);stroke-width:1}.pattern-notes{fill:var(--c-secondary)}.pattern-transport{stroke:var(--text-dim);stroke-width:1}.pattern-transport.playing{stroke:var(--c-primary);stroke-width:2}.silent{position:absolute;inset:0;display:grid;place-content:center;font:9px monospace;color:var(--text-dim)}.mini{height:23px;border-radius:1px}.mini .roll-keyboard{width:10px}
</style>
