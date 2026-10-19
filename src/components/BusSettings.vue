<script setup lang="ts">
import {state} from '../store'
import {busTargets,busLabels,surfaceLabels} from '../masterFx'
import {DIVISIONS} from '../types'
</script>
<template><div class="bus-settings">
 <h3>Mix effects</h3>
 <div class="effects-properties"><fieldset><legend>Reverb</legend><label>Size<input v-model.number="state.patch.fx.reverbSize" type="number" min=".1" max="8" step=".1" aria-label="Main reverb size"/></label><label>Decay<input v-model.number="state.patch.fx.reverbDecay" type="number" min=".5" max="10" step=".1" aria-label="Main reverb decay"/></label></fieldset>
 <fieldset><legend>Delay</legend><label>Clock<select v-model="state.patch.fx.delaySync" aria-label="Main delay clock"><option :value="true">Tempo sync</option><option :value="false">Seconds</option></select></label><label v-if="state.patch.fx.delaySync">Division<select v-model.number="state.patch.fx.delayBeats" aria-label="Main delay division"><option v-for="d in DIVISIONS.filter(d=>d.beats<=8)" :key="d.id" :value="d.beats">{{d.id}}</option></select></label><label v-else>Time<input v-model.number="state.patch.fx.delayTime" type="number" min=".01" max="2" step=".001" aria-label="Main delay time"/></label><label>Feedback<input v-model.number="state.patch.fx.delayFeedback" type="number" min="0" max=".9" step=".01" aria-label="Main delay feedback"/></label></fieldset>
 <fieldset><legend>Chorus</legend><label>Rate<input v-model.number="state.patch.fx.chorusRate" type="number" min=".05" max="8" step=".05" aria-label="Main chorus rate"/></label><label>Depth<input v-model.number="state.patch.fx.chorusDepth" type="number" min="0" max="1" step=".01" aria-label="Main chorus depth"/></label></fieldset></div>
 <h3>Mix controller bindings</h3><label v-for="target in busTargets" :key="target" class="bus-binding">{{busLabels[target]}}<select v-model.number="state.patch.fx.bindings[target]" :aria-label="`${busLabels[target]} controller`"><option :value="-1">Header dial</option><option v-for="(name,i) in surfaceLabels" :key="i" :value="i">{{name}} · CC {{state.patch.globalCC[i]}}</option></select></label>
 <p class="settings-hint">Bindings affect the whole mix. Turning a header dial returns it to direct control. Reassign or learn controllers in MIDI &amp; controllers.</p>
</div></template>
<style scoped>
.bus-settings{margin-top:24px}.bus-settings h3{margin:18px 0 12px;font-size:13px;font-weight:500}.effects-properties{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px}.effects-properties fieldset{border:1px solid var(--line);padding:10px;min-width:0}.effects-properties legend{font-size:11px;padding:0 4px;color:var(--text-dim)}.effects-properties label{display:flex;flex-direction:column;align-items:stretch;gap:4px;margin:5px 0 10px;font-size:11px}.effects-properties :is(input,select){width:100%;min-width:0}.bus-binding{display:flex;justify-content:space-between;align-items:center;gap:18px;margin:7px 0;font-size:11px}.bus-binding select{width:230px;max-width:65%}@media(max-width:700px){.effects-properties{grid-template-columns:1fr}}
</style>
