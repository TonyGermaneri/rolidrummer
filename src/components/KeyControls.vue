<script setup lang="ts">
import {ref,watch} from 'vue'
import {state} from '../store'
import {drumParts,partName,type Key} from '../model'
import {midiNoteName} from '../lib/format'
const props=defineProps<{binding:Key}>()
const details=ref(false)
watch(()=>props.binding.note,()=>{details.value=false})
function setPart(e:Event){const part=drumParts.find(p=>p.name===(e.target as HTMLSelectElement).value);if(part)props.binding.parts=[...part.notes];else details.value=true}
</script>
<template><div class="sampler-key-controls">
 <div class="key-control-row"><label class="key-note" :title="`Selected key: MIDI ${binding.note}`" for="selected-key-name">KEY {{midiNoteName(binding.note)}}</label><input id="selected-key-name" v-model="binding.name" aria-label="Key name"/><button class="ssb-chip track-keywave" :class="{on:state.trackKeywave}" :aria-pressed="state.trackKeywave" aria-label="Track last touched keywave" title="Follow the last keywave touched" @click="state.trackKeywave=!state.trackKeywave">TRACK</button></div>
 <div class="key-control-row"><label for="selected-drum-part">PART</label><select id="selected-drum-part" class="ssb-enum" :value="partName(binding.parts)" @change="setPart"><option v-for="part in drumParts" :key="part.name" :value="part.name">{{part.name}}</option><option value="Custom">Custom…</option></select><button class="ssb-chip" :aria-expanded="details" aria-label="Edit MIDI drum note filter" @click="details=!details">NOTES</button></div>
 <div class="key-control-row"><label for="selected-playback">PLAY</label><select id="selected-playback" v-model="binding.sound.s.mode" class="ssb-enum"><option value="stack">One shot</option><option value="hold">Follow note duration</option></select></div>
 <div v-if="details" class="key-note-details" @keydown.esc="details=false"><div><label>MIDI DRUM NOTES<input :value="binding.parts.join(', ')" aria-label="MIDI drum note filter" @change="binding.parts=($event.target as HTMLInputElement).value.split(',').map(Number).filter(n=>Number.isInteger(n)&&n>=0&&n<=127)"/></label><button class="ssb-chip" aria-label="Close note filter" @click="details=false">×</button></div><p>The drum notes to play from the pattern, not the Seaboard key you hold.</p></div>
</div></template>
<style scoped>
.sampler-key-controls{position:relative;flex:0 0 260px;min-width:0;display:flex;flex-direction:column;gap:5px;padding:2px 0}
.key-control-row{display:flex;align-items:center;gap:5px;min-width:0}.key-control-row>label{width:48px;flex:none;font:8px 'Orbitron',sans-serif;letter-spacing:.07em;color:var(--text-dim)}.key-control-row>input,.key-control-row>select{width:0;flex:1;min-width:0;min-height:21px}
.key-control-row>input{font:13px monospace;color:var(--c-primary);padding:2px 5px;background:var(--lcd-bg);border:1px solid #000;border-radius:2px}.key-control-row button{flex:none;font-size:9px}
.track-keywave.on{color:var(--lcd-bg);background:var(--c-primary)}
.key-note-details{position:absolute;top:100%;right:0;width:290px;max-width:calc(100vw - 60px);z-index:20;padding:9px;background:var(--panel-lo);border:1px solid var(--line);border-radius:4px;box-shadow:0 6px 20px #0009}.key-note-details>div{display:flex;gap:8px;align-items:flex-start}.key-note-details label{flex:1;flex-direction:column;align-items:stretch;font-size:9px}.key-note-details input{width:100%}.key-note-details p{margin:7px 0 0;font-size:10px;color:var(--text-dim)}
@media(max-width:1050px){.sampler-key-controls{flex-basis:235px}}
</style>
