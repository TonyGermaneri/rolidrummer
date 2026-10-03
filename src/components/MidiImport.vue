<script setup lang="ts">
import { computed, ref, watch, nextTick } from 'vue'
import { midiImport as model, finishMidiImport } from '../midiImport'
const dialog = ref<HTMLDialogElement>()
const channels = computed(() => model.track < 0 ? model.info?.channels ?? [] : model.info?.tracks.find(t => t.index === model.track)?.channels ?? [])
watch(() => model.track, () => { if (model.channel && !channels.value.includes(model.channel)) model.channel = channels.value.length === 1 ? channels.value[0] : 0 })
watch(() => model.open, async open => { await nextTick(); if (open) dialog.value?.showModal(); else dialog.value?.close() })
</script>
<template><dialog ref="dialog" class="midi-import" aria-labelledby="midi-title" @cancel.prevent="finishMidiImport(false)">
 <h2 id="midi-title">Choose pattern notes</h2><p>{{model.name}}</p>
 <label>Track<select v-model.number="model.track"><option :value="-1">All tracks</option><option v-for="track in model.info?.tracks.filter(t=>t.notes)" :key="track.index" :value="track.index">{{track.name}} · {{track.notes}} notes</option></select></label>
 <label>Channel<select v-model.number="model.channel"><option :value="0">All channels</option><option v-for="channel in channels" :key="channel" :value="channel">Channel {{channel}}{{channel===10?' · drums':''}}</option></select></label>
 <p class="hint">Timing and the full file’s loop length are preserved. Each key plays only the drum notes assigned to its part.</p>
 <div class="actions"><button @click="finishMidiImport(false)">Cancel</button><button class="primary" @click="finishMidiImport(true)">Use pattern</button></div>
</dialog></template>
<style scoped>
.midi-import{position:fixed;margin:auto;width:420px;max-width:90vw;padding:24px;background:var(--panel-mid);color:var(--text);border:1px solid var(--line);border-radius:8px;box-shadow:0 20px 80px #0009}.midi-import::backdrop{background:#080c12b0}.midi-import h2{margin:0 0 8px}.midi-import p{font-size:13px;overflow-wrap:anywhere}.midi-import label{justify-content:space-between;margin-top:16px}.midi-import select{width:290px}.midi-import .hint{color:var(--text-dim);font-size:12px;line-height:1.6;margin:20px 0}.actions{display:flex;justify-content:end;gap:8px}
</style>
