<script setup lang="ts">
import {ref,onBeforeUnmount,watch} from 'vue'
import {state,attempt} from '../store'
import {kitState,matchSounds,cancelSoundSearch} from '../kits'
import {midiNoteName} from '../lib/format'
import AssetBrowser from './AssetBrowser.vue'
import KitPreview from './KitPreview.vue'
const query=ref('')
watch(()=>[query.value,state.selected],cancelSoundSearch)
onBeforeUnmount(cancelSoundSearch)
</script>
<template><aside class="sound-aside workspace-ui" aria-label="Keywave sound browser"><header><b>SOUNDS</b><span>{{midiNoteName(state.selected)}}</span></header><form class="sound-search" @submit.prevent="attempt(()=>matchSounds(query,state.selected))"><input v-model="query" aria-label="Semantic sound search" placeholder="Find a sound for this keywave…"/><button :disabled="kitState.busy||!query.trim()" aria-label="Match sound for selected keywave">{{kitState.busy?'…':'Find'}}</button></form><KitPreview/><AssetBrowser sound-only :assign-to="state.selected"/><p v-if="kitState.status" class="sound-status">{{kitState.status}}</p></aside></template>
<style scoped>
.sound-aside{min-height:0;min-width:0;display:flex;flex-direction:column;gap:8px;padding:9px;border:1px solid var(--line);border-radius:3px;background:var(--panel-lo)}header{display:flex;justify-content:space-between;color:var(--text-dim);font:10px monospace;letter-spacing:.1em}header span{color:var(--c-primary)}.sound-search{display:flex;gap:5px}.sound-search input{flex:1;min-width:0;font-size:11px}.sound-search button{font-size:10px}.sound-aside :deep(.asset-browser){flex:1;min-height:0;height:auto}.sound-aside :deep(.browser-tools){gap:4px;margin-bottom:5px;flex-wrap:wrap}.sound-aside :deep(.browser-tools button){padding:4px 6px;font-size:9px}.sound-aside :deep(.browser-tools input){font-size:10px;padding:4px;min-width:65px}.sound-aside :deep(.browser-body){min-height:100px}.sound-aside :deep(.browser-footer){height:auto;flex-wrap:wrap;gap:6px;padding-top:5px;font:9px monospace}.sound-aside :deep(.browser-footer button){font-size:9px}.sound-status{font:10px/1.4 monospace;color:var(--text-dim);margin:0}
</style>
