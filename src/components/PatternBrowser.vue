<script setup lang="ts">
import {ref} from 'vue'
import AssetBrowser from './AssetBrowser.vue'
import LibraryPanel from './LibraryPanel.vue'
const view=ref('library')
</script>
<template><section class="pattern-workspace">
 <nav class="pattern-views segmented" aria-label="Pattern browser view"><button :class="{active:view==='library'}" @click="view='library'">INDEXED LIBRARY</button><button :class="{active:view==='folders'}" @click="view='folders'">FILES AND PATCH PATTERNS</button></nav>
 <LibraryPanel v-if="view==='library'"/><AssetBrowser v-else patterns/>
</section></template>
<style scoped>
.pattern-workspace{height:100%;min-height:0;display:flex;flex-direction:column;gap:6px}.pattern-views{display:flex;gap:5px;flex:none}.pattern-views button{font:9px monospace;padding:5px 10px}.pattern-workspace>:last-child{flex:1;min-height:0}
</style>
