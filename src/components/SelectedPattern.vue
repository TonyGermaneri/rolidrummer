<script setup lang="ts">
import {computed,watch} from 'vue'
import {state,attempt} from '../store'
import {native,request} from '../native/bridge'
import {performanceView,useRollClock,elapsedBeats} from '../performance'
import PatternRoll from './PatternRoll.vue'
const props=defineProps<{note:number}>()
const time=useRollClock()
watch(()=>[props.note,state.ready],()=>{if(native()&&state.ready)void attempt(()=>request('monitorKey',props.note))},{immediate:true})
const key=computed(()=>state.patch.keys.find(k=>k.note===props.note))
const actual=computed(()=>performanceView.patterns[props.note])
const known=computed(()=>actual.value?.pattern.known)
const assigned=computed(()=>state.patch.clips.find(c=>c.id===key.value?.zones.find(z=>z.clipId)?.clipId))
const pattern=computed(()=>known.value?actual.value!.pattern:assigned.value)
const phase=computed(()=>known.value?actual.value!.position.phase+(actual.value!.position.playing?elapsedBeats(time.value,actual.value!.received,performanceView.bpm):0):undefined)
</script>
<template><div class="selected-pattern"><PatternRoll :notes="pattern?.notes??[]" :length="pattern?.length??4" :parts="known?undefined:key?.parts" :phase="phase" :playing="known&&actual?.position.playing"/><div class="pattern-caption" :title="pattern?.name"><b>{{known?(actual?.position.playing?'PLAYING':'LAST'):'ASSIGNED'}}</b><span>{{pattern?.name??'Silent zone'}}</span><small v-if="actual?.pattern.totalNotes&&actual.pattern.totalNotes>8192">8192 / {{actual.pattern.totalNotes}} notes</small></div></div></template>
<style scoped>
.selected-pattern{min-width:0}.pattern-caption{display:flex;gap:6px;align-items:center;height:18px;font:9px monospace;color:var(--text-dim);min-width:0}.pattern-caption b{font-size:8px;color:var(--c-secondary);font-weight:500}.pattern-caption span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.pattern-caption small{white-space:nowrap}
</style>
