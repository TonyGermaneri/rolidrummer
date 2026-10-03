<script setup lang="ts">
import { ref, watch } from 'vue'
import { colors, type Zone } from '../model'
import { state } from '../store'
import { htmlDrop } from '../drag'
import { maxZones, minZoneWidth, setBoundary, splitZone, removeZone } from '../lib/zones'
const props = defineProps<{ zones: Zone[]; scope: string; compact?: boolean }>()
const selected = ref(0)
watch(() => props.zones.length, count => { selected.value = Math.min(selected.value, count - 1) })
function resize(e: PointerEvent, index: number) {
 e.preventDefault()
 const handle = e.currentTarget as HTMLElement, box = handle.parentElement!.parentElement!, bounds = box.getBoundingClientRect()
 handle.setPointerCapture(e.pointerId)
 const move = (event: PointerEvent) => setBoundary(props.zones, index, 1 - (event.clientY - bounds.top) / bounds.height)
 const up = () => { handle.removeEventListener('pointermove', move); handle.removeEventListener('pointerup', up); handle.removeEventListener('pointercancel', up) }
 handle.addEventListener('pointermove', move); handle.addEventListener('pointerup', up); handle.addEventListener('pointercancel', up)
}
function keyboard(e: KeyboardEvent, index: number) {
 if (e.key !== 'ArrowUp' && e.key !== 'ArrowDown') return
 e.preventDefault(); setBoundary(props.zones, index, props.zones[index].upper + (e.key === 'ArrowUp' ? 1 : -1) * (e.shiftKey ? .05 : .01))
}
</script>
<template><div class="zone-control" :class="{compact}">
 <div class="zone-tools"><select v-model.number="selected" aria-label="Zone to edit"><option v-for="(_,i) in zones" :key="i" :value="i">Zone {{zones.length-i}}</option></select><button :disabled="zones.length>=maxZones||zones[selected].upper-(selected?zones[selected-1].upper:0)<minZoneWidth*2" title="Split selected zone" @click="splitZone(zones,selected)">＋ Split</button><button :disabled="zones.length<=1" title="Remove selected zone" @click="removeZone(zones,selected)">− Remove</button></div>
 <div class="zone-editor"><div v-for="(zone,i) in zones" :key="i" class="zone" :class="{editing:selected===i}" :style="{bottom:`${(i?zones[i-1].upper:0)*100}%`,height:`${(zone.upper-(i?zones[i-1].upper:0))*100}%`,'--zone':colors[zones.length-1-i]}" :data-drop="`zone:${scope}:${i}`" @pointerdown="selected=i" @dragover.prevent @drop="htmlDrop($event,`zone:${scope}:${i}`)"><span class="zone-number">{{zones.length-i}}</span><select v-model="zone.clipId" :aria-label="`Zone ${zones.length-i} pattern`"><option value="">Silent zone</option><option v-for="clip in state.patch.clips" :key="clip.id" :value="clip.id">{{clip.name}}</option></select><span class="zone-range">{{Math.round((i?zones[i-1].upper:0)*100)}}–{{Math.round(zone.upper*100)}}%</span><div v-if="i<zones.length-1" class="zone-boundary" tabindex="0" role="separator" aria-orientation="horizontal" :aria-label="`Resize zone ${zones.length-i}`" :aria-valuenow="Math.round(zone.upper*100)" :aria-valuemin="Math.round((i?zones[i-1].upper:0)*100+1)" :aria-valuemax="Math.round(zones[i+1].upper*100-1)" @keydown="keyboard($event,i)" @pointerdown.stop="resize($event,i)"/></div></div>
</div></template>
<style scoped>
.zone-control{height:100%;display:flex;flex-direction:column;gap:4px;min-height:0}.zone-control.compact{height:165px;flex-shrink:0}.zone-tools{display:flex;gap:4px;flex:0 0 23px}.zone-tools select{flex:1;min-width:0}.zone-tools button,.zone-tools select{font-size:10px;padding:2px 5px}.zone-editor{flex:1;min-height:100px;height:auto}.zone.editing{box-shadow:inset 3px 0 var(--zone)}.zone select{max-height:100%}.zone-boundary:focus-visible{outline:1px solid #f4d394;background:#f4d39466}.zone-range{pointer-events:none}
</style>
