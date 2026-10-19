<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { colors, type Zone } from '../model'
import { state } from '../store'
import { htmlDrop } from '../drag'
import { maxZones, minZoneWidth, setBoundary, splitZone, removeZone } from '../lib/zones'
import {performanceView,useRollClock,elapsedBeats} from '../performance'
import PatternRoll from './PatternRoll.vue'
const props = defineProps<{ zones: Zone[]; scope: string; compact?: boolean }>()
const selected = ref(0)
const key=computed(()=>props.scope==='template'?undefined:state.patch.keys.find(k=>k.note===Number(props.scope)))
const time=useRollClock()
function actual(zone:number){const view=key.value&&performanceView.patterns[key.value.note];return view?.pattern.known&&view.position.zone===zone?view:undefined}
function preview(zone:number){const view=actual(zone);if(view)return view.pattern;const candidate=liveCandidate(zone);return state.patch.clips.find(clip=>clip.id===(props.zones[zone].candidates?.[candidate]?.clipId??props.zones[zone].clipId))}
function phase(zone:number){const view=actual(zone);return view?view.position.phase+(view.position.playing?elapsedBeats(time.value,view.received,performanceView.bpm):0):undefined}
function manual(){if(state.patch.neural&&key.value)key.value.neuralLock=true}
function liveCandidate(zone:number){return state.telemetry.fingers?.find(f=>f.note===Number(props.scope)&&f.zone===zone&&(f.candidate??-1)>=0)?.candidate??-1}
watch(() => props.zones.length, count => { selected.value = Math.min(selected.value, count - 1) })
function resize(e: PointerEvent, index: number) {
 e.preventDefault()
 const handle = e.currentTarget as HTMLElement, box = handle.parentElement!.parentElement!, bounds = box.getBoundingClientRect()
 handle.setPointerCapture(e.pointerId)
 const move = (event: PointerEvent) => {manual();setBoundary(props.zones, index, 1 - (event.clientY - bounds.top) / bounds.height)}
 const up = () => { handle.removeEventListener('pointermove', move); handle.removeEventListener('pointerup', up); handle.removeEventListener('pointercancel', up) }
 handle.addEventListener('pointermove', move); handle.addEventListener('pointerup', up); handle.addEventListener('pointercancel', up)
}
function keyboard(e: KeyboardEvent, index: number) {
 if (e.key !== 'ArrowUp' && e.key !== 'ArrowDown') return
 e.preventDefault();manual(); setBoundary(props.zones, index, props.zones[index].upper + (e.key === 'ArrowUp' ? 1 : -1) * (e.shiftKey ? .05 : .01))
}
</script>
<template><div class="zone-control" :class="{compact}">
 <div class="zone-tools"><select class="ssb-enum" v-model.number="selected" aria-label="Zone to edit"><option v-for="(_,i) in zones" :key="i" :value="i">Zone {{zones.length-i}}</option></select><button class="ssb-button" :disabled="zones.length>=maxZones||zones[selected].upper-(selected?zones[selected-1].upper:0)<minZoneWidth*2" title="Split selected zone" @click="manual();splitZone(zones,selected)">＋ Split</button><button class="ssb-button" :disabled="zones.length<=1" title="Remove selected zone" @click="manual();removeZone(zones,selected)">− Remove</button><button v-if="state.patch.neural&&key" class="ssb-button" :aria-pressed="Boolean(key.neuralLock)" title="Keep these zones when resolving a patch name" @click="key.neuralLock=!key.neuralLock">{{key.neuralLock?'LOCKED':'AUTO'}}</button></div>
 <details v-if="key?.patternIntent&&key.zones.some(zone=>zone.candidates?.length)" class="intent-controls"><summary>GESTURE VARIATIONS</summary><label><input type="checkbox" v-model="key.patternIntent.enabled" aria-label="Enable gesture pattern selection"/>Enabled</label><label v-for="(name,i) in ['Press activity','Press accents','Glide variation','Strike intensity']" :key="name">{{name}}<input type="range" v-model.number="key.patternIntent.weights[i]" min="0" max="1" step=".05" :aria-label="name+' influence'"/></label><label>Slew<input type="number" min="0" max="1000" step="5" v-model.number="key.patternIntent.smoothingMs" aria-label="Pattern intent smoothing milliseconds"/>ms</label><label>Minimum dwell<select v-model.number="key.patternIntent.minimumDwell" aria-label="Pattern variation minimum dwell"><option :value="0">Immediate</option><option :value=".125">1/32</option><option :value=".25">1/16</option><option :value=".5">1/8</option><option :value="1">1/4</option></select></label></details>
 <div class="zone-editor"><div v-for="(zone,i) in zones" :key="i" class="zone" :class="{editing:selected===i}" :style="{bottom:`${(i?zones[i-1].upper:0)*100}%`,height:`${(zone.upper-(i?zones[i-1].upper:0))*100}%`,'--zone':colors[zones.length-1-i]}" :data-drop="`zone:${scope}:${i}`" @pointerdown="selected=i" @dragover.prevent @drop="htmlDrop($event,`zone:${scope}:${i}`)"><span class="zone-number">{{zones.length-i}}</span><select v-model="zone.clipId" :title="zone.role?`${zone.role}${zone.candidates?.length?` · ${zone.candidates.length} gesture variations`:``}`:undefined" @change="manual();delete zone.role;delete zone.candidates" :aria-label="`Zone ${zones.length-i} pattern`"><option value="">Silent zone</option><option v-for="clip in state.patch.clips" :key="clip.id" :value="clip.id">{{clip.name}}</option></select><PatternRoll class="zone-pattern" mini :notes="preview(i)?.notes??[]" :length="preview(i)?.length??4" :parts="actual(i)?undefined:key?.parts" :phase="phase(i)" :playing="actual(i)?.position.playing"/><span class="zone-range">{{Math.round((i?zones[i-1].upper:0)*100)}}–{{Math.round(zone.upper*100)}}%<small v-if="zone.candidates?.length">{{liveCandidate(i)>=0?`VAR ${liveCandidate(i)+1}/${zone.candidates.length}`:`${zone.candidates.length} VARIATIONS`}}</small></span><div v-if="i<zones.length-1" class="zone-boundary" tabindex="0" role="separator" aria-orientation="horizontal" :aria-label="`Resize zone ${zones.length-i}`" :aria-valuenow="Math.round(zone.upper*100)" :aria-valuemin="Math.round((i?zones[i-1].upper:0)*100+1)" :aria-valuemax="Math.round(zones[i+1].upper*100-1)" @keydown="keyboard($event,i)" @pointerdown.stop="resize($event,i)"/></div></div>
</div></template>
<style scoped>
.intent-controls{flex:none;font:10px monospace;max-height:110px;overflow:auto}.intent-controls summary{cursor:pointer;color:var(--text-dim);padding:4px}.intent-controls label{display:inline-flex;align-items:center;gap:5px;margin:4px}.intent-controls input[type=range]{width:70px}.intent-controls input[type=number]{width:45px}.zone-range small{display:block;font:8px monospace;margin-top:3px}
.zone-pattern{flex:0 1 35%;min-width:36px;max-width:150px;max-height:calc(100% - 4px);pointer-events:none}.zone{gap:5px;padding-inline:6px}.zone select{flex:1 1 40%;width:0}
.zone-control{height:100%;display:flex;flex-direction:column;gap:4px;min-height:0}.zone-control.compact{height:165px;flex-shrink:0}.zone-tools{display:flex;gap:4px;flex:0 0 23px}.zone-tools select{flex:1;min-width:0}.zone-tools button,.zone-tools select{font-size:10px;padding:2px 5px}.zone-editor{flex:1;min-height:0;height:auto}.zone.editing{box-shadow:inset 3px 0 var(--zone)}.zone select{max-height:100%}.zone-boundary:focus-visible{outline:1px solid #f4d394;background:#f4d39466}.zone-range{pointer-events:none}
</style>
