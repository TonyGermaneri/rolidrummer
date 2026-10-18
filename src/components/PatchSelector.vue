<script setup lang="ts">
import {computed} from 'vue'
const props=defineProps<{name:string;choices:{id:string;name:string}[];busy?:boolean;program?:number;currentId?:string;unassigned?:boolean;slot?:number}>()
const emit=defineEmits<{select:[id:string]}>()
const index=computed(()=>props.choices.findIndex(p=>props.currentId!==undefined?p.id===props.currentId:p.name===props.name))
const number=computed(()=>props.slot!==undefined?(props.slot>=0?`P${String(props.slot).padStart(3,'0')}`:'--'):(index.value<0?'--':String(index.value+1).padStart(2,'0')))
const disabled=computed(()=>props.busy||!props.choices.length||(props.choices.length===1&&index.value===0))
function step(direction:number){if(disabled.value)return;const i=index.value<0?(direction>0?0:props.choices.length-1):(index.value+direction+props.choices.length)%props.choices.length;emit('select',props.choices[i].id)}
</script>
<template><section class="patch-selector" aria-label="Patch selector"><span class="patch-caption">PATCH</span><button class="patch-step" aria-label="Previous patch" title="Previous patch" :disabled="disabled" @click="step(-1)">‹</button><output class="patch-display" :title="`${name} — rename in the Patches tab${program!==undefined&&program>=0?` · Received MIDI program ${program}${unassigned?' (unassigned)':''}`:``}`"><span class="patch-number">{{number}}</span><span class="patch-name">{{name}}</span></output><button class="patch-step" aria-label="Next patch" title="Next patch" :disabled="disabled" @click="step(1)">›</button></section></template>
<style scoped>
.patch-selector{min-width:130px;max-width:370px;flex:1;display:flex;align-items:center;gap:4px;padding:3px 5px;border:1px solid #ffffff12;border-radius:4px;background:#0003}.patch-caption{font:7px 'Orbitron',sans-serif;letter-spacing:.15em;color:var(--text-dim);padding-right:3px}.patch-step{font:20px/20px monospace;padding:0 5px;border:1px solid #000;border-radius:3px;color:var(--text-dim);background:linear-gradient(var(--panel-hi),var(--panel-lo));box-shadow:inset 0 1px #ffffff20,0 2px #0007;flex:none}.patch-step:hover{color:var(--c-primary)}.patch-step:disabled{opacity:.3;cursor:default}.patch-display{display:flex;gap:7px;align-items:center;min-width:0;flex:1;padding:3px 7px;background:var(--lcd-bg);border:1px solid #000;border-radius:3px;box-shadow:inset 0 2px 4px #000c;font:13px 'VT323',monospace;color:var(--c-primary);text-shadow:0 0 4px color-mix(in srgb,var(--c-primary) 25%,transparent)}.patch-name{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.patch-number{font-size:10px;color:var(--c-secondary)}
@media(max-width:1050px){.patch-selector{max-width:265px}.patch-display{font-size:11px;padding:3px 5px}.patch-caption{display:none}}
</style>
