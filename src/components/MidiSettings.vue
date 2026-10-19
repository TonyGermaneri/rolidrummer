<script setup lang="ts">
import {state} from '../store'
import {targets,targetLabel,newRoute} from '../model'
function route(i:number){return state.patch.globalRoutes.find(r=>r.source===i)?.target||''}
function map(i:number,event:Event){
 const target=(event.target as HTMLSelectElement).value,routes=state.patch.globalRoutes.filter(r=>r.source!==i)
 if(target)routes.push(newRoute(i,target))
 if(state.patch.keys.some(key=>key.routes.length+routes.length>16)){state.error='A key already uses all 16 mappings. Remove a key mapping before adding this global controller.';(event.target as HTMLSelectElement).value=route(i);return}
 state.patch.globalRoutes=routes
}
</script>
<template><div class="midi-settings"><section><h3>Sustain pedal</h3><div class="setting-row"><label for="sustain-cc">Controller number</label><select id="sustain-cc" v-model.number="state.patch.sustainCC" aria-label="Sustain pedal CC"><option v-for="cc in Array.from({length:120},(_,i)=>i).filter(cc=>cc!==74)" :key="cc" :value="cc">{{cc}}</option></select><button @click="state.learn=state.learn===5?-1:5">{{state.learn===5?'Press pedal…':'Learn pedal'}}</button></div></section><section><h3>Global controllers</h3><div v-for="(name,i) in ['Fader 1','Fader 2','Fader 3','Pad X','Pad Y']" :key="i" class="setting-row"><label>{{name}}</label><button @click="state.learn=state.learn===i?-1:i">{{state.learn===i?'Move control…':`CC ${state.patch.globalCC[i]}`}}</button><select :aria-label="`${name} sound binding`" :value="route(i)" @change="map(i,$event)"><option value="">Unbound</option><option v-for="target in targets" :key="target[0]" :value="target[0]">{{targetLabel(target[0])}}</option></select></div><p class="settings-hint">These mappings affect each key’s sound. Main bus controls are available in BUS FX.</p></section></div></template>
<style scoped>.midi-settings{display:grid;gap:22px}.setting-row{display:flex;gap:10px;align-items:center;margin:9px 0}.setting-row label{min-width:130px}.setting-row select{max-width:240px}.setting-row button{min-width:100px}</style>
