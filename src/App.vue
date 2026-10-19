<script setup lang="ts">
import {computed,ref,watch,onMounted,onBeforeUnmount} from 'vue'
import {state,init,attempt,undo,replace,shared,sendMidi,flushEdits,saveSharedPatch} from './store'
import {targets,newRoute,clone,factory} from './model'
import {request,native} from './native/bridge'
import {drag} from './drag'
import {matcher} from './matcher'
import {themeId} from './theme/current'
import {themeById} from './theme/themes'
import {midiNoteName} from './lib/format'
import Seaboard from './components/Seaboard.vue'
import MidiImport from './components/MidiImport.vue'
import SoundPanel from './components/SoundPanel.vue'
import ZoneEditor from './components/ZoneEditor.vue'
import ExpressionPanel from './components/ExpressionPanel.vue'
import KeyControls from './components/KeyControls.vue'
import Knob from './components/Knob.vue'
import MasterEffects from './components/MasterEffects.vue'
import PatchPanel from './components/PatchPanel.vue'
import PatchSelector from './components/PatchSelector.vue'
import PatternBrowser from './components/PatternBrowser.vue'
import SettingsPanel from './components/SettingsPanel.vue'
import PianoRollPanel from './components/PianoRollPanel.vue'
const selected=computed(()=>state.patch.keys.find(k=>k.note===state.selected))
const base=ref(36),patchLoading=ref(false),patchSaving=ref(false)
watch(()=>shared['ui/theme']?.revision,()=>{const value=shared['ui/theme']?.value;if(value)themeId.value=themeById(String(value)).id},{immediate:true})
const patches=computed(()=>Object.values(shared).filter(f=>f.key.startsWith('patch/')&&f.value).sort((a,b)=>a.value.name.localeCompare(b.value.name)||a.key.localeCompare(b.key)))
const patchChoices=computed(()=>{const kit=factory();return [...(patches.value.some(p=>p.value.name===kit.name)?[]:[{id:'builtin:factory',name:kit.name,value:kit}]),...patches.value.map(p=>({id:p.key,name:p.value.name,value:p.value}))]})
const currentPatchId=computed(()=>patchChoices.value.find(p=>state.patch.id?p.value.id===state.patch.id:p.name===state.patch.name)?.id??'')
const currentProgram=computed(()=>{const slots=shared['midi/program-bank']?.value.slots as (string|null)[]|undefined;return slots?.indexOf(state.patch.id??'')??(state.patch.id==='builtin:factory'?0:-1)})
function stepPatch(direction:number){const list=patchChoices.value,index=list.findIndex(p=>p.id===currentPatchId.value);if(!list.length)return;const next=index<0?(direction>0?0:list.length-1):(index+direction+list.length)%list.length;void attempt(()=>selectPatch(list[next].id))}
async function selectPatch(id:string){
 if(patchLoading.value)return
 const patch=patchChoices.value.find(p=>p.id===id);if(!patch)return
 const document=clone(patch.value);patchLoading.value=true
 try{await flushEdits();await replace(document)}finally{patchLoading.value=false}
}

function panic(){sendMidi([0xb0,123,0]);attempt(()=>request('stopPreview'))}
function output(value:number){state.telemetry.output=value;if(native())void attempt(()=>request('parameter',{id:'volume',value}))}
function addRoute(source=3,target='volume'){if(!selected.value)return;if(selected.value.routes.length+state.patch.globalRoutes.length>=16){state.error='Each key supports 16 mappings, including global controllers. Remove a mapping before adding another.';return}selected.value.routes.push(newRoute(source,target))}
function bind(target:string){if(state.bindSource<0||!selected.value)return;if(!targets.some(t=>t[0]===target)){state.error='This control is edited in the sound panel; choose a supported expression destination.';return}addRoute(state.bindSource,target);state.bindSource=-1}
async function savePatch(copy=false){
 if(patchSaving.value)return
 patchSaving.value=true
 try{await flushEdits();const result=await saveSharedPatch(copy);if(!result.ok)throw Error('Another instance saved this patch first. Review its latest version before saving again.');await flushEdits();state.status=`Saved “${result.name}” · ${result.program>=0?`MIDI program ${result.program}`:'No free MIDI program slot'}`}
 finally{patchSaving.value=false}
}
function applyTemplate(){for(const key of state.patch.keys){key.zones=clone(state.patch.templateZones);if(state.patch.neural)key.neuralLock=true}}
async function exportPatch(){await flushEdits();await request('savePatch',clone(state.patch))}
async function openPatch(){const p=await request('openPatch');if(p)await replace(p)}
function keydown(e:KeyboardEvent){if((e.metaKey||e.ctrlKey)&&e.key.toLowerCase()==='z'){e.preventDefault();undo(e.shiftKey)}}
onMounted(()=>{void attempt(init);window.addEventListener('keydown',keydown)});onBeforeUnmount(()=>{window.removeEventListener('keydown',keydown)})
</script>
<template><v-app><div class="instrument" :class="{'workspace-open':['Patterns','Piano Roll','Settings'].includes(state.tab)}">
<header class="masthead"><button class="project-tile" aria-label="Project settings" :title="`Roli Drummer · ${themeById(themeId).name}`" @click="state.settingsSection='appearance';state.tab='Settings'"><span class="project-logo">ROLI DRUMMER</span><span class="project-theme"><i :style="{background:themeById(themeId).primary}"/><i :style="{background:themeById(themeId).secondary}"/>{{themeById(themeId).name}}</span></button><PatchSelector :name="state.patch.name" :choices="patchChoices" :current-id="currentPatchId" :slot="currentProgram" :program="state.telemetry.program" :unassigned="state.telemetry.programUnassigned" :busy="patchLoading||patchSaving||state.saving" @select="id=>attempt(()=>selectPatch(id))"/><MasterEffects/><div class="transport"><button v-if="state.standalone" @click="attempt(()=>request('transport',!state.telemetry.playing))">{{state.telemetry.playing?'■ Stop':'▶ Play'}}</button><span :class="{lit:state.telemetry.playing||state.telemetry.freePlaying}">●</span><b class="tempo-readout">{{state.telemetry.bpm.toFixed(1)}}</b><small>BPM · {{state.telemetry.freePlaying?'KEYS':state.standalone?'INTERNAL':'HOST'}}</small><span class="position lcd-readout">{{Math.floor(state.telemetry.ppq/4)+1}} : {{(state.telemetry.ppq%4+1).toFixed(1)}}</span><div class="meter"><i :style="{width:`${Math.min(1,state.telemetry.peak)*100}%`}"/></div><Knob label="OUT" :model-value="state.telemetry.output" :max="1.5" :default=".8" :size="28" :format="v=>`${Math.round(v*100)}%`" @update:model-value="output"/><button class="ssb-button panic-button" @click="panic">PANIC</button></div></header>
<nav class="tabs"><button v-for="tab in ['Key','Patterns','Piano Roll','Patches','Settings']" :key="tab" :class="{active:state.tab===tab}" @click="state.tab=tab">{{tab}}</button><span class="tab-spacer"/><button :disabled="!state.history.length" @click="undo()">↶ Undo</button><button :disabled="!state.future.length" @click="undo(true)">↷ Redo</button></nav>
<section class="upper">
<div v-if="state.tab==='Key'&&selected" class="key-editor"><div class="key-editor-body"><SoundPanel :sound="selected.sound" :note="selected.note" @bind="bind"><template #key-controls><KeyControls :binding="selected"/></template><ExpressionPanel :binding="selected"/></SoundPanel></div></div>
<div v-else-if="state.tab==='Key'" class="empty">Drop a sound on this key to assign it.</div>
<PatternBrowser v-else-if="state.tab==='Patterns'"/>
<PianoRollPanel v-else-if="state.tab==='Piano Roll'"/>
<PatchPanel v-else-if="state.tab==='Patches'" :choices="patchChoices" :busy="patchLoading||patchSaving" @load="id=>attempt(()=>selectPatch(id))" @save="attempt(()=>savePatch())" @copy="attempt(()=>savePatch(true))" @open="attempt(openPatch)" @export="attempt(exportPatch)"/>
<SettingsPanel v-else/>
</section>
<section class="performance ssb-chassis"><div class="performance-body"><Seaboard :base="base" @patch="stepPatch"/><div v-if="state.tab!=='Patterns'||!matcher.autoMode" class="template-zones"><div class="zone-tools"><label>FIRST NOTE<select class="ssb-enum" v-model.number="base" aria-label="First note"><option v-for="note in [0,12,24,36,48,60,72]" :key="note" :value="note">{{midiNoteName(note)}}</option></select></label><button class="ssb-button" @click="applyTemplate">APPLY TO ALL</button></div><ZoneEditor :zones="state.patch.templateZones" scope="template"/></div></div></section>
<footer class="status"><span>{{state.error||state.status||(state.bindSource>=0?'Click a knob to bind expression':'Hold a key to hear its part. Slide to change patterns.')}}</span><button v-if="state.error" @click="state.error=''">Dismiss</button><span class="tab-spacer"/><span class="mpe-monitor" :title="'Latest incoming slide message; per-channel values drive the finger markers'">{{state.telemetry.slideChannel>=0?`Slide ${Math.round(state.telemetry.slideValue/127*100)}% · Ch ${state.telemetry.slideChannel+1}`:'Slide: waiting for CC74'}}</span><span>{{state.telemetry.voices}} voices · {{state.telemetry.clips}} clip buses</span><span v-if="state.telemetry.overflow" class="error">Event limit reached</span><span>{{native()?'NATIVE AUDIO':'EDITOR PREVIEW'}}</span></footer>
<div v-if="drag.active" class="drag-ghost" :style="{left:`${drag.x+14}px`,top:`${drag.y+14}px`}">{{drag.asset?.name}}</div>
<MidiImport/></div></v-app></template>
