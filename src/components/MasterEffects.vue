<script setup lang="ts">
import {state} from '../store'
import {busTargets,busLabels,surfaceLabels,busValue,fxRanges,type BusTarget} from '../masterFx'
import Knob from './Knob.vue'
function value(target:BusTarget){const source=state.patch.fx.bindings[target];return source<0?state.patch.fx[target]:busValue(target,state.telemetry.macros[source]??0)}
function turn(target:BusTarget,n:number){state.patch.fx.bindings[target]=-1;state.patch.fx[target]=n}
const format=(target:BusTarget,n:number)=>target==='cutoff'?(n>=1000?`${(n/1000).toFixed(1)}k`:`${Math.round(n)}`):target==='resonance'?n.toFixed(1):`${Math.round(n*100)}%`
</script>
<template><section class="master-effects" aria-label="Main bus effects">
 <button class="bus-menu" aria-label="Main bus settings and controller bindings" @click="state.settingsSection='playback';state.tab='Settings'">BUS<br>FX ▸</button>
 <div v-for="target in busTargets" :key="target" class="bus-dial" :class="{mapped:state.patch.fx.bindings[target]>=0}" :title="`${busLabels[target]} · ${state.patch.fx.bindings[target]<0?'Knob':surfaceLabels[state.patch.fx.bindings[target]]}`"><Knob :label="busLabels[target]" :model-value="value(target)" :min="fxRanges[target][0]" :max="fxRanges[target][1]" :default="fxRanges[target][2]" :curve="target==='cutoff'?'log':'lin'" :size="26" :format="v=>format(target,v)" @update:model-value="v=>turn(target,v)"/></div>
</section></template>
<style scoped>
.master-effects{display:flex;flex:none;gap:5px;align-items:center;padding:2px 5px;border:1px solid #ffffff12;border-radius:4px;background:#0003;height:36px}.bus-menu{font:7px/1.5 'Orbitron',sans-serif!important;padding:0 3px!important;letter-spacing:.08em;background:transparent!important;border:0!important;color:var(--text-dim)}.bus-dial{position:relative}.bus-dial.mapped:after{content:'';position:absolute;width:3px;height:3px;border-radius:50%;background:var(--c-secondary);top:0;right:0}.bus-dial :deep(.knob){display:grid;grid-template-columns:26px 39px;grid-template-rows:16px 10px;column-gap:2px;width:67px;align-items:center}.bus-dial :deep(.knob-body){grid-row:1/3}.bus-dial :deep(.readout){font-size:10px;margin:0;text-align:center}.bus-dial :deep(.label){font-size:6px;text-align:center}
</style>
