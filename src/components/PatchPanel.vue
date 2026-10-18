<script setup lang="ts">
import {computed} from 'vue'
import {state,shared} from '../store'
import type {Patch} from '../model'
import AssetBrowser from './AssetBrowser.vue'
import Knob from './Knob.vue'
const props=defineProps<{choices:{id:string;name:string;value:Patch}[];busy?:boolean}>()
const emit=defineEmits<{load:[id:string];save:[];copy:[];open:[];export:[];factory:[]}>()
const rows=computed(()=>props.choices.map(p=>({id:p.id,path:p.id,name:p.name,folder:false,size:0,keyCount:p.value.keys.length,patternCount:p.value.clips.length})))
const currentId=computed(()=>props.choices.find(p=>state.patch.id?p.value.id===state.patch.id:p.name===state.patch.name)?.id)
const programLabel=computed(()=>{
 const slots=shared['midi/program-bank']?.value.slots as (string|null)[]|undefined
 if(state.patch.id==='builtin:factory'&&!slots)return '0'
 const slot=slots?.indexOf(state.patch.id??'')??-1
 return slot>=0?String(slot):currentId.value?'UNASSIGNED':'SAVE TO ASSIGN'
})
const grids=[{name:'IMMEDIATE',value:0},{name:'1/32',value:.125},{name:'1/16',value:.25},{name:'1/8',value:.5},{name:'1/4',value:1},{name:'BAR',value:4}]
</script>
<template><div class="patch-workspace">
 <aside class="patch-console ssb-chassis">
  <section class="ssb-module"><h4>PATCH</h4><label class="patch-name-label">NAME<input v-model="state.patch.name" aria-label="Saved patch name" placeholder="Name this instrument"/></label>
   <div class="patch-summary"><span>{{state.patch.keys.length}} KEYS</span><span>{{state.patch.clips.length}} PATTERNS</span><span>MIDI {{programLabel}}</span></div>
   <button class="ssb-button patch-save" :disabled="busy||state.saving||!state.patch.name.trim()" @click="emit('save')">{{state.saving?'SAVING…':'SAVE PATCH'}}</button>
   <button class="ssb-button patch-save" :disabled="busy||state.saving||!state.patch.name.trim()" @click="emit('copy')">SAVE A COPY</button>
  </section>
  <section class="ssb-module patch-playback"><h4>PLAYBACK</h4><div class="patch-performance-controls"><Knob v-model="state.patch.volume" label="KIT LEVEL" :max="1.5" :default=".8" :size="36" :format="v=>`${Math.round(v*100)}%`"/><Knob v-if="state.standalone" v-model="state.patch.bpm" label="BPM" :min="20" :max="400" :default="120" :size="36" :format="v=>v.toFixed(1)"/><div class="patch-quantize"><label>LAUNCH<select v-model.number="state.patch.launchGrid" class="ssb-enum" aria-label="Pattern launch quantization"><option v-for="g in grids" :key="g.value" :value="g.value">{{g.name}}</option></select></label><label>ZONE CHANGE<select v-model.number="state.patch.switchGrid" class="ssb-enum" aria-label="Zone change quantization"><option v-for="g in grids" :key="g.value" :value="g.value">{{g.name}}</option></select></label></div></div>
   <button class="ssb-chip release-mode" :class="{on:state.patch.hardRelease}" :aria-pressed="state.patch.hardRelease" title="Click to choose whether voices ring out or stop when a key is released" @click="state.patch.hardRelease=!state.patch.hardRelease">RELEASE · {{state.patch.hardRelease?'STOP VOICES':'LET TAILS RING'}} ▸</button>
  </section>
  <section class="ssb-module patch-files"><h4>FILES</h4><button class="ssb-button" :disabled="busy" @click="emit('open')">OPEN PATCH…</button><button class="ssb-button" :disabled="busy" @click="emit('export')">EXPORT PORTABLE…</button><button class="ssb-button" :disabled="busy" @click="emit('factory')">LOAD FACTORY KIT</button><p>Portable patches include sounds, patterns, expression and bus settings.</p></section>
 </aside>
 <AssetBrowser :patches="rows" :current-id="currentId" :disabled="busy" @load-patch="emit('load',$event)"/>
</div></template>
<style scoped>
.patch-workspace{height:100%;min-height:300px;display:grid;grid-template-columns:270px minmax(0,1fr);gap:8px;padding:2px 0}.patch-console{padding:6px;display:flex;flex-direction:column;gap:5px;min-height:0;overflow:auto;scrollbar-width:thin}.patch-console .ssb-module{padding:8px}.patch-console h4{margin-bottom:8px}.patch-name-label{display:flex;flex-direction:column;align-items:stretch;gap:4px;font:7px 'Orbitron',sans-serif;letter-spacing:.15em}.patch-console .patch-name-label input{width:100%;min-width:0;padding:6px 8px;background:var(--lcd-bg);border:1px solid #000;border-radius:3px;box-shadow:inset 0 2px 5px #0008;color:var(--c-primary);font:16px 'VT323',monospace}.patch-summary{display:flex;gap:12px;color:var(--text-mute);font:8px monospace;margin:8px 0}.patch-save{width:100%;color:var(--c-primary)!important}.patch-performance-controls{display:flex;align-items:center;gap:8px}.patch-quantize{display:flex;flex:1;flex-direction:column;gap:7px;min-width:0}.patch-quantize label{font:7px 'Orbitron',sans-serif;display:flex;justify-content:space-between;gap:5px}.patch-quantize select{width:87px;min-width:0;font-size:10px!important;padding:2px 3px!important}.release-mode{width:100%;margin-top:9px;font-size:10px!important}.patch-files{display:grid;gap:5px}.patch-files h4{margin-bottom:1px}.patch-files p{color:var(--text-mute);font:9px/1.5 sans-serif;margin:3px 0 0}
.patch-workspace :deep(.patch-browser){min-height:0;padding:7px;border:1px solid #0009;border-radius:5px;background:repeating-linear-gradient(90deg,#ffffff03 0 1px,transparent 1px 3px),linear-gradient(var(--panel-hi),var(--panel-lo));box-shadow:inset 0 1px #ffffff20}.patch-workspace :deep(.browser-tools){gap:6px;margin-bottom:7px;flex-wrap:wrap}.patch-workspace :deep(.patch-library-title){font:8px 'Orbitron',sans-serif;letter-spacing:.14em;color:var(--text-head)}.patch-workspace :deep(.browser-tools input){flex:1;min-width:110px;width:0;background:var(--lcd-bg);color:var(--c-primary);border:1px solid #000;border-radius:3px;padding:5px 8px;font:12px 'VT323',monospace;box-shadow:inset 0 2px 4px #0008}.patch-workspace :deep(.segmented button){padding:5px 10px;font:8px 'Orbitron',sans-serif;letter-spacing:.08em;text-transform:uppercase;background:linear-gradient(var(--panel-hi),var(--panel-lo));border:1px solid #0009;border-radius:3px;box-shadow:inset 0 1px #ffffff15,0 1px #0008;color:var(--text-dim)}.patch-workspace :deep(.segmented .active){color:var(--c-primary);box-shadow:inset 0 2px var(--c-primary)}.patch-workspace :deep(.browser-body){border:1px solid #0009;box-shadow:inset 0 2px 5px #0007}.patch-workspace :deep(.browser-footer){height:auto;min-height:30px;gap:8px;flex-wrap:wrap;font:9px monospace;color:var(--text-mute);padding-top:6px}.patch-workspace :deep(.browser-footer button){max-width:55%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.patch-workspace :deep(.graph-label){background:var(--lcd-bg);border:1px solid #0009;border-radius:3px;color:var(--c-secondary);font:12px 'VT323',monospace;box-shadow:0 2px 5px #0007}.patch-workspace :deep(.graph-label.patch-chosen){color:var(--c-primary);border-color:var(--c-primary)}
@media(max-width:850px){.patch-workspace{grid-template-columns:235px minmax(0,1fr);gap:5px}.patch-workspace :deep(.patch-library-title){display:none}.patch-console{padding:4px}.patch-console .ssb-module{padding:6px}.patch-performance-controls{gap:3px}}
</style>
