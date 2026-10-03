<script setup lang="ts">
import {computed,onBeforeUnmount,ref,watch} from 'vue'
import {clone,type Sound} from '../model'
import {preview,audition,stopPreview} from '../preview'
import {attempt} from '../store'
import {request,native} from '../native/bridge'
import Knob from './Knob.vue'
import Waveform from './Waveform.vue'
import {
  fmtCents, fmtChoke, fmtDb, fmtGrainRate, fmtGrainShape, fmtGrainSize, fmtHz, fmtNum, fmtOct, fmtPan, fmtPct, fmtQ,
  fmtRatio, fmtRepeat, fmtSec, fmtSemis, fmtSemisJitter, midiNoteName,
} from '../lib/format'
import {
  FILTER_LABEL, GRAIN_DEFAULTS, GRAIN_MIN_MS, GRAIN_RATE_MAX, GRAIN_RATE_MIN,

} from '../types'

const props=defineProps<{sound:Sound;note?:number}>()
const emit=defineEmits<{bind:[target:string]}>()
const s=computed(()=>props.sound.s)
const folded=ref<string[]>([])
const previewId=computed(()=>`sampler:${props.note??'selected'}`)
const isPreviewing=computed(()=>preview.id===previewId.value)
function playSound(){return audition({id:previewId.value,path:props.sound.path,name:s.value.name,folder:false,size:0,preset:{sound:clone(props.sound),parts:[],routes:[]}},false)}
watch(()=>[props.sound,props.sound.path,props.note],(_,previous)=>{if(previous&&preview.id===`sampler:${previous[2]??'selected'}`)void attempt(stopPreview)})
onBeforeUnmount(()=>{if(isPreviewing.value)void attempt(stopPreview)})
const isFolded=(k:string)=>folded.value.includes(k)
const toggleFold=(k:string)=>{folded.value=isFolded(k)?folded.value.filter(x=>x!==k):[...folded.value,k]}
const board={cycleFilter:()=>{s.value.filterType=({lowpass:'highpass',highpass:'bandpass',bandpass:'lowpass'} as const)[s.value.filterType]}}
const peaks=ref<number[]>([]),duration=ref(.5)
const grainMax=()=>Math.max(500,duration.value*1000)
const outputRate=()=>48000
const fmtNote=(v:number)=>midiNoteName(Math.round(v))
const fmtBend=(v:number)=>`±${Math.round(v)}st`
let waveformTicket=0
watch(()=>[props.sound.path,props.sound.asset],async ()=>{
 const ticket=++waveformTicket;peaks.value=[]
 if(!native())return
 try{const result=await request('waveform',{path:props.sound.path,asset:props.sound.asset});if(ticket===waveformTicket){peaks.value=result.peaks;duration.value=result.duration}}catch{/* Missing assets are shown in the sound panel. */}
},{immediate:true})
async function relink(){const sound=props.sound,previous=sound.path;const path=await request<string|null>('relinkSound');if(path){if(props.sound!==sound||sound.path!==previous)throw Error('The selected sound changed while locating the file. Select the missing sound and try again.');sound.path=path;delete sound.asset;delete sound.missing}}

function clicked(e:MouseEvent){const target=(e.target as HTMLElement).closest('[data-param]') as HTMLElement|null;if(target)emit('bind',target.dataset.param!)}
</script>
<template>
 <div class="panel wide" @click.capture="clicked">
 <div class="top"><div class="scope"><Waveform :peaks="peaks" :duration="duration" :note="note" :clip-in="s.clipIn" :clip-out="s.clipOut"/><span>{{sound.path.split('/').pop()}}</span></div><label class="sound-title">SOUND<input v-model="s.name"/><button type="button" class="sampler-preview" :aria-label="isPreviewing?'Stop sound preview':'Preview sound'" :aria-pressed="isPreviewing" :aria-busy="isPreviewing&&preview.loading" @click.stop="attempt(playSound)">{{isPreviewing?'■ Stop':'▶ Play sound'}}</button><button :class="{on:s.reverse}" data-param="reverse" @click="s.reverse=!s.reverse">Reverse</button></label></div>
    <div v-if="sound.missing" class="missing-sound"><span>{{sound.missing}}</span><button @click="attempt(relink)">Locate sound…</button></div>
    <div class="modules">
      <section class="module" :class="{ folded: isFolded('play') }">
        <h4>PLAY
          <button class="fold" :title="isFolded('play') ? 'Show' : 'Fold'" @click="toggleFold('play')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'volume'" v-model="s.volume" label="VOL" :max="1.5" :default="1" :format="fmtPct" />
          <Knob :data-param="'pan'" v-model="s.pan" label="PAN" :min="-1" :max="1" :default="0" bipolar :format="fmtPan" />
          <Knob :data-param="'repeat'" v-model="s.repeat" label="RPT" :min="0" :max="16" :step="1" :default="1" :format="fmtRepeat" />
          <Knob :data-param="'choke'" v-model="s.choke" label="CHOKE" :min="0" :max="8" :step="1" :default="0" :format="fmtChoke" />
        </div>
      </section>

      <section class="module" :class="{ folded: isFolded('tune') }">
        <h4>
          TUNE
          <button
            class="chip"
            :class="{ on: s.timeMode === 'stretch' }"
            :title="s.timeMode === 'tape'
              ? 'TAPE: pitch and speed change together, like a sampler. Click for STRETCH.'
              : 'STRETCH: granular — speed and pitch are independent. Click for TAPE.'"
            @click="s.timeMode = s.timeMode === 'tape' ? 'stretch' : 'tape'"
          >
            {{ s.timeMode === 'tape' ? 'TAPE' : 'STRETCH' }}
          </button>
          <button class="fold" :title="isFolded('tune') ? 'Show' : 'Fold'" @click="toggleFold('tune')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'pitch'" v-model="s.pitch" label="PITCH" :min="-24" :max="24" :step="1" :default="0" bipolar :format="fmtSemis" />
          <Knob :data-param="'fine'" v-model="s.fine" label="FINE" :min="-100" :max="100" :step="1" :default="0" bipolar :format="fmtCents" />
          <Knob :data-param="'speed'" v-model="s.speed" label="SPEED" :min="0.25" :max="4" curve="log" :default="1" :format="fmtRatio" />
        </div>
      </section>

      <section class="module" :class="{ folded: isFolded('keys') }">
        <h4>KEYS
          <button class="fold" :title="isFolded('keys') ? 'Show' : 'Fold'" @click="toggleFold('keys')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'rootNote'" v-model="s.rootNote" label="ROOT" :min="0" :max="127" :step="1" :default="60" :format="fmtNote" />
          <Knob :data-param="'velAmount'" v-model="s.velAmount" label="VEL AMT" :default="1" :format="fmtPct" />
          <Knob :data-param="'bendRange'" v-model="s.bendRange" label="BEND" title="Glide pitch range when Glide has no other mapping" :min="0" :max="48" :step="1" :default="2" :format="fmtBend" />
        </div>
      </section>

      <section class="module" :class="{ folded: isFolded('clip') }">
        <h4>CLIP
          <button class="fold" :title="isFolded('clip') ? 'Show' : 'Fold'" @click="toggleFold('clip')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'clipIn'" v-model="s.clipIn" label="IN" :default="0" :format="fmtPct" />
          <Knob :data-param="'clipOut'" v-model="s.clipOut" label="OUT" :default="1" :format="fmtPct" />
        </div>
      </section>

      <section class="module" :class="{ folded: isFolded('adsr') }">
        <h4>ADSR
          <button class="fold" :title="isFolded('adsr') ? 'Show' : 'Fold'" @click="toggleFold('adsr')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'attack'" v-model="s.attack" label="A" :min="0.001" :max="5" curve="log" :default="0.001" :format="fmtSec" />
          <Knob :data-param="'decay'" v-model="s.decay" label="D" :min="0.001" :max="5" curve="log" :default="0.2" :format="fmtSec" />
          <Knob :data-param="'sustain'" v-model="s.sustain" label="S" :default="1" :format="fmtPct" />
          <Knob :data-param="'release'" v-model="s.release" label="R" :min="0.001" :max="10" curve="log" :default="0.02" :format="fmtSec" />
        </div>
      </section>

      <section class="module" :class="{ folded: isFolded('filter') }">
        <h4>
          FILTER
          <button class="chip on" title="Filter type — click to cycle LP / HP / BP" @click="board.cycleFilter()">
            {{ FILTER_LABEL[s.filterType] }}
          </button>
          <button class="fold" :title="isFolded('filter') ? 'Show' : 'Fold'" @click="toggleFold('filter')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'cutoff'" v-model="s.cutoff" label="CUTOFF" :min="20" :max="20000" curve="log" :default="20000" :format="fmtHz" color="secondary" />
          <Knob :data-param="'resonance'" v-model="s.resonance" label="RES" :min="0.1" :max="20" curve="log" :default="0.7" :format="fmtQ" color="secondary" />
        </div>
      </section>

      <section class="module" :class="{ folded: isFolded('filter-adsr') }">
        <h4>FILTER ADSR
          <button class="fold" :title="isFolded('filter-adsr') ? 'Show' : 'Fold'" @click="toggleFold('filter-adsr')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'fEnvAmount'" v-model="s.fEnvAmount" label="AMT" :min="-5" :max="5" :step="0.1" :default="0" bipolar :format="fmtOct" color="secondary" />
          <Knob :data-param="'fAttack'" v-model="s.fAttack" label="A" :min="0.001" :max="5" curve="log" :default="0.001" :format="fmtSec" color="secondary" />
          <Knob :data-param="'fDecay'" v-model="s.fDecay" label="D" :min="0.001" :max="5" curve="log" :default="0.3" :format="fmtSec" color="secondary" />
          <Knob :data-param="'fSustain'" v-model="s.fSustain" label="S" :default="0" :format="fmtPct" color="secondary" />
          <Knob :data-param="'fRelease'" v-model="s.fRelease" label="R" :min="0.001" :max="10" curve="log" :default="0.2" :format="fmtSec" color="secondary" />
        </div>
      </section>

      <section class="module" :class="{ folded: isFolded('eq') }">
        <h4>EQ
          <button class="fold" :title="isFolded('eq') ? 'Show' : 'Fold'" @click="toggleFold('eq')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'eqLow'" v-model="s.eqLow" label="BASS" :min="-12" :max="12" :step="0.5" :default="0" bipolar :format="fmtDb" color="secondary" />
          <Knob :data-param="'eqMid'" v-model="s.eqMid" label="MID" :min="-12" :max="12" :step="0.5" :default="0" bipolar :format="fmtDb" color="secondary" />
          <Knob :data-param="'eqHigh'" v-model="s.eqHigh" label="TREBLE" :min="-12" :max="12" :step="0.5" :default="0" bipolar :format="fmtDb" color="secondary" />
        </div>
      </section>

      <section class="module wide" :class="{ folded: isFolded('grain'), off: !s.grain }">
        <h4>GRAIN
          <button
            class="chip"
            :class="{ on: s.grain }"
            :title="s.grain ? 'Grain cloud on: the sound plays as grains — click for the plain sample' : 'Play the sound as a cloud of grains'"
            @click="s.grain = !s.grain"
          >
            {{ s.grain ? 'ON' : 'OFF' }}
          </button>
          <button class="fold" :title="isFolded('grain') ? 'Show' : 'Fold'" @click="toggleFold('grain')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'grainSize'" v-model="s.grainSize" label="SIZE" :min="GRAIN_MIN_MS" :max="grainMax()" curve="grain" :default="GRAIN_DEFAULTS.grainSize" :format="(v: number) => fmtGrainSize(v, outputRate())" color="accent" />
          <Knob :data-param="'grainRate'" v-model="s.grainRate" label="RATE" :min="GRAIN_RATE_MIN" :max="GRAIN_RATE_MAX" curve="log" :default="GRAIN_DEFAULTS.grainRate" :format="fmtGrainRate" color="accent" />
          <Knob :data-param="'grainPos'" v-model="s.grainPos" label="POS" :default="0.5" :format="fmtPct" color="accent" />
          <Knob :data-param="'grainWidth'" v-model="s.grainWidth" label="SPRAY" :default="GRAIN_DEFAULTS.grainWidth" :format="fmtPct" color="accent" />
          <Knob :data-param="'grainShape'" v-model="s.grainShape" label="SHAPE" :default="1" :format="fmtGrainShape" color="accent" />
          <Knob :data-param="'grainScatter'" v-model="s.grainScatter" label="SCATTR" :default="GRAIN_DEFAULTS.grainScatter" :format="fmtPct" color="accent" />
          <Knob :data-param="'grainJitter'" v-model="s.grainJitter" label="JITTER" :max="12" :step="0.1" :default="0" :format="fmtSemisJitter" color="accent" />
          <Knob :data-param="'grainReverse'" v-model="s.grainReverse" label="REV" :default="0" :format="fmtPct" color="accent" />
          <Knob :data-param="'grainSpread'" v-model="s.grainSpread" label="SPREAD" :default="GRAIN_DEFAULTS.grainSpread" :format="fmtPct" color="accent" />
          <Knob :data-param="'grainStreams'" v-model="s.grainStreams" label="STRMS" :min="1" :max="8" :step="1" :default="1" :format="(v: number) => `${Math.round(v)}`" color="accent" />
          <Knob :data-param="'grainDrift'" v-model="s.grainDrift" label="DRIFT" :default="0" :format="fmtPct" color="accent" />
        </div>
      </section>

      <section class="module" :class="{ folded: isFolded('delay') }">
        <h4>DELAY
          <button class="fold" :title="isFolded('delay') ? 'Show' : 'Fold'" @click="toggleFold('delay')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'delayTime'" v-model="s.delayTime" label="TIME" :min="0.01" :max="2" curve="log" :default="0.25" :format="fmtSec" color="success" />
          <Knob :data-param="'delayFeedback'" v-model="s.delayFeedback" label="FDBK" :max="0.9" :default="0.35" :format="fmtPct" color="success" />
          <Knob :data-param="'delayMix'" v-model="s.delayMix" label="MIX" :default="0" :format="fmtPct" color="success" />
        </div>
      </section>

      <section class="module" :class="{ folded: isFolded('reverb') }">
        <h4>REVERB
          <button class="fold" :title="isFolded('reverb') ? 'Show' : 'Fold'" @click="toggleFold('reverb')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'reverbSize'" v-model="s.reverbSize" label="SIZE" :min="0.1" :max="6" :default="2" :format="fmtSec" color="success" />
          <Knob :data-param="'reverbDecay'" v-model="s.reverbDecay" label="DECAY" :min="0.5" :max="10" :default="3" :format="fmtNum" color="success" />
          <Knob :data-param="'reverbMix'" v-model="s.reverbMix" label="MIX" :default="0" :format="fmtPct" color="success" />
        </div>
      </section>
    </div>


 </div>
</template>
<style scoped>
.panel {
  position: relative;
  width: 412px;
  padding: 12px 12px 10px;
  margin-bottom: 8px;
  border-radius: 6px;
  background:
    repeating-linear-gradient(90deg, rgba(255, 255, 255, 0.015) 0 1px, transparent 1px 3px),
    linear-gradient(180deg, var(--panel-hi) 0%, var(--panel-mid) 55%, var(--panel-lo) 100%);
  box-shadow:
    0 10px 24px rgba(0, 0, 0, 0.6),
    inset 0 1px 0 rgba(255, 255, 255, 0.12),
    inset 0 -2px 0 rgba(0, 0, 0, 0.4);
  border: 1px solid #111;
  cursor: default;
}
.screws i {
  position: absolute;
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: radial-gradient(circle at 40% 35%, #9a98a0, #3c3b40);
  box-shadow: inset 0 0 0 1px rgba(0, 0, 0, 0.5);
}
.screws i:nth-child(1) { top: 4px; left: 4px; }
.screws i:nth-child(2) { top: 4px; right: 4px; }
.screws i:nth-child(3) { bottom: 4px; left: 4px; }
.screws i:nth-child(4) { bottom: 4px; right: 4px; }
.fields {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
  margin: 8px 0 6px;
}
.scope {
  min-width: 0;
}
/* full-width panel: scope + name/tag side by side, modules flow across the row */
.panel.wide {
  width: 100%;
}
.panel.wide .top {
  display: flex;
  gap: 12px;
  align-items: flex-start;
  margin-bottom: 8px;
}
.panel.wide .scope {
  flex: 1;
}
.panel.wide .fields {
  flex: 0 0 340px;
  grid-template-columns: 1fr;
  margin: 0;
}
.panel.wide .module.wide {
  flex-basis: auto;
}
.panel.wide .module {
  flex-grow: 1;
}
.fields :deep(.v-field) {
  font-family: 'VT323', monospace;
  font-size: 18px;
  background: var(--lcd-bg);
  color: rgb(var(--v-theme-primary));
}
.fields :deep(.v-label) {
  font-family: 'Orbitron', sans-serif;
  font-size: 9px;
  letter-spacing: 0.1em;
}
.modules {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.module {
  flex: 1 1 auto;
  padding: 2px 4px 2px;
  border: 1px solid rgba(255, 255, 255, 0.07);
  border-radius: 4px;
  background: rgba(0, 0, 0, 0.18);
}
.module.wide {
  flex-basis: 100%;
}
.module {
  position: relative;
}
.module h4 {
  padding-right: 16px;
}
/* folded: title bar only, as narrow as it needs */
.module.folded {
  flex: 0 1 auto;
  padding-bottom: 1px;
}
.module.folded > :not(h4) {
  display: none;
}
.module.folded h4 {
  margin-bottom: 0;
  opacity: 0.7;
}
.fold {
  position: absolute;
  top: 2px;
  right: 3px;
  display: grid;
  place-items: center;
  width: 12px;
  height: 12px;
  padding: 0;
  border: 0;
  background: none;
  appearance: none;
}
/* a little triangle: ▼ open, ◀ folded */
.fold i {
  width: 0;
  height: 0;
  border-left: 4px solid transparent;
  border-right: 4px solid transparent;
  border-top: 5px solid var(--text-mute);
  transition: transform 120ms;
}
.folded .fold i {
  transform: rotate(90deg);
}
.fold:hover i {
  border-top-color: var(--c-primary);
}
.module h4 {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 0 0 2px 2px;
  font-family: 'Orbitron', sans-serif;
  font-size: 8px;
  font-weight: 900;
  letter-spacing: 0.18em;
  color: var(--text-head);
}
.chip {
  padding: 0 5px;
  border-radius: 2px;
  font-family: 'VT323', monospace;
  font-size: 12px;
  letter-spacing: 0.08em;
  line-height: 13px;
  color: var(--ink-dim);
  background: #111;
  border: 1px solid #000;
}
.chip.on {
  color: var(--c-secondary);
  text-shadow: 0 0 4px color-mix(in srgb, var(--c-secondary) 70%, transparent);
}
/* a section switched off (GRAIN): its knobs still work, dimmed */
.module.off > .row {
  opacity: 0.45;
}
.row {
  display: flex;
  flex-wrap: wrap;
  justify-content: space-around;
}
/* narrow panels (the rack's three slots): one row of grain knobs, icon-only footer buttons */
.panel {
  container-type: inline-size;
}
@container (max-width: 440px) {
  .module.wide .row :deep(.knob) {
    width: calc(var(--size) + 2px);
  }
  .module.wide .row :deep(.label) {
    font-size: 6px;
    letter-spacing: 0;
  }
  .footer .lbl {
    display: none;
  }
}
.module.folded {
  align-self: flex-start;
}
.footer {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 6px;
  margin-top: 8px;
}
.vco .sub {
  font-family: 'VT323', monospace;
  font-size: 12px;
  font-weight: 400;
  letter-spacing: 0.04em;
  color: var(--text-mute);
}
.slot {
  padding: 3px 0 4px;
  border-top: 1px solid rgba(255, 255, 255, 0.06);
}
.slot-top {
  display: flex;
  align-items: center;
  gap: 6px;
}
.slot .num {
  font-family: 'Orbitron', sans-serif;
  font-size: 10px;
  font-weight: 900;
  color: var(--c-accent);
}
.picker {
  flex: 1;
  min-width: 0;
}
.picker :deep(.v-field) {
  font-family: 'VT323', monospace;
  font-size: 16px;
  background: var(--lcd-bg);
  color: var(--c-secondary);
}
.add-vco {
  margin-top: 4px;
}
.badge {
  margin-left: 2px;
  padding: 0 4px;
  border-radius: 6px;
  font-family: 'VT323', monospace;
  font-size: 13px;
  line-height: 13px;
  color: #111;
  background: var(--c-primary);
}
.preset-menu {
  width: 260px;
  padding: 8px;
  border-radius: 6px;
  background: linear-gradient(180deg, var(--panel-hi), var(--panel-lo));
  border: 1px solid #000;
  box-shadow: 0 10px 30px rgba(0, 0, 0, 0.7);
  font-family: 'VT323', monospace;
}
.pm-row {
  display: flex;
  gap: 6px;
  margin-bottom: 6px;
}
.pm-input {
  flex: 1;
  min-width: 0;
  padding: 0 6px;
  border-radius: 3px;
  font-family: 'VT323', monospace;
  font-size: 17px;
  color: var(--c-primary);
  background: var(--lcd-bg);
  border: 1px solid #000;
  outline: none;
}
.pm-empty,
.pm-hint {
  padding: 4px 2px;
  font-size: 14px;
  color: var(--text-mute);
}
.pm-item {
  display: flex;
  align-items: center;
  border-top: 1px solid rgba(255, 255, 255, 0.06);
}
.pm-load {
  flex: 1;
  padding: 3px 4px;
  text-align: left;
  font-family: 'VT323', monospace;
  font-size: 18px;
  color: var(--c-secondary);
  text-transform: uppercase;
}
.pm-load:hover {
  background: color-mix(in srgb, var(--c-secondary) 8%, transparent);
}
.pm-del {
  padding: 0 4px;
  color: var(--text-mute);
}
.pm-del:hover {
  color: var(--c-danger);
}
.mode {
  min-width: 108px;
}
.leds {
  display: inline-flex;
  gap: 2px;
  margin-right: 2px;
}
.leds i {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: #3a1a08;
}
.leds i.on {
  background: var(--c-primary);
  box-shadow: 0 0 5px var(--c-primary);
}
.blink {
  animation: blink 0.6s steps(2) infinite;
}
@keyframes blink {
  50% {
    color: #111;
    background: var(--c-primary);
  }
}
</style>

<style scoped>.missing-sound{display:flex;align-items:center;gap:12px;color:#efb385;font-size:11px;padding:8px 0}.missing-sound span{flex:1}</style>
