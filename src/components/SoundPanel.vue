<script setup lang="ts">
import {computed,onBeforeUnmount,ref,watch} from 'vue'
import {clone,type Sound} from '../model'
import {preview,audition,stopPreview} from '../preview'
import {attempt} from '../store'
import {request,native} from '../native/bridge'
import { DENSITY_MAX, DENSITY_MIN, DETUNE_MAX, GRAIN_MODES, NOTE_RND_MAX, VAR_PITCH_MAX, VAR_POS_MAX, VAR_SIZE_MAX, VAR_VOL_MAX, FM_AMOUNT_MAX, FM_FREQ_MAX, FM_FREQ_MIN, GRAIN_FREQ_MAX, grainFreqMin, SCAN_DIST_MIN, SCAN_TIME_MAX, SCAN_TIME_MIN, SPRAY_MAX, SPRAY_SIGNS, SYMMETRIES } from '../audio/grainMath'
import Knob from './Knob.vue'
import Waveform from './Waveform.vue'
import {
  fmtCents, fmtChoke, fmtDb, fmtFinePct, fmtFix1, fmtFix2, fmtGrainHz, fmtMs, fmtSignedPct, fmtWholePct, fmtHz, fmtNum, fmtOct, fmtPan, fmtPct, fmtQ,
  fmtRatio, fmtRepeat, fmtSec, fmtSemis, midiNoteName,
} from '../lib/format'
import {
  FILTER_LABEL, FILTER_TYPES, GRAIN_DEFAULTS,

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
const board={cycleFilter:()=>{s.value.filterType=next(FILTER_TYPES,s.value.filterType)}}
const peaks=ref<number[]>([]),duration=ref(.5)
const grainMin=()=>grainFreqMin(duration.value)
const next=<T,>(list:readonly T[],v:T):T=>list[(list.indexOf(v)+1)%list.length]
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
 <div class="top"><div class="scope"><Waveform :peaks="peaks" :duration="duration" :note="note" :clip-in="s.clipIn" :clip-out="s.clipOut"/><span>{{sound.path.split('/').pop()}}</span></div><slot name="key-controls"/><label class="sound-title"><span class="sound-caption">SOUND</span><input v-model="s.name"/><button type="button" class="sampler-preview" :aria-label="isPreviewing?'Stop sound preview':'Preview sound'" :aria-pressed="isPreviewing" :aria-busy="isPreviewing&&preview.loading" @click.stop="attempt(playSound)">{{isPreviewing?'■ Stop':'▶ Play sound'}}</button><button :class="{on:s.reverse}" data-param="reverse" @click="s.reverse=!s.reverse">Reverse</button></label></div>
    <div v-if="sound.missing" class="missing-sound"><span>{{sound.missing}}</span><button @click="attempt(relink)">Locate sound…</button></div>
    <div class="modules">
      <section class="module" :class="{ folded: isFolded('play') }">
        <h4>PLAY
          <button class="fold" :title="isFolded('play') ? 'Show' : 'Fold'" @click="toggleFold('play')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'volume'" v-model="s.volume" label="VOL" :max="1.5" :default="1" :format="fmtPct" />
          <Knob data-param="volKey" v-model="s.volKey" label="K>VOL" :min="-3" :max="3" :default="0" bipolar :format="fmtDb" />
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
          <Knob data-param="envKey" v-model="s.envKey" label="ENV KEY" :default="0" :format="fmtPct" />
        </div>
      </section>

      <section class="module" :class="{ folded: isFolded('filter') }">
        <h4>
          FILTER
          <button class="chip on" title="Filter type — click to cycle biquad and Granulator III filters" @click="board.cycleFilter()">
            {{ FILTER_LABEL[s.filterType] }}
          </button>
          <button class="fold" :title="isFolded('filter') ? 'Show' : 'Fold'" @click="toggleFold('filter')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'cutoff'" v-model="s.cutoff" label="CUTOFF" :min="20" :max="20000" curve="log" :default="20000" :format="fmtHz" color="secondary" />
          <Knob :data-param="'resonance'" v-model="s.resonance" label="RES" :min="0.1" :max="20" curve="log" :default="0.7" :format="fmtQ" color="secondary" />
          <Knob data-param="filterWidth" v-model="s.filterWidth" label="WIDTH" :min="-48" :max="48" :default="-12" bipolar :format="fmtSemis" color="secondary" />
          <Knob data-param="filterKey" v-model="s.filterKey" label="F.KEY" :default="0" :format="fmtPct" color="secondary" />
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
          <Knob data-param="fEnvKey" v-model="s.fEnvKey" label="ENV KEY" :default="0" :format="fmtPct" color="secondary" />
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

    </div>
    <slot/>
    <div class="modules secondary-modules">
      <section class="module wide" :class="{ folded: isFolded('grain'), off: !s.grain }">
        <h4>GRAIN
          <button
            class="chip"
            :class="{ on: s.grain }"
            :title="s.grain ? 'Grain cloud on: the sound plays as grains (Granulator III\'s engine) — click for the plain sample' : 'Play the sound as a cloud of grains (Granulator III\'s engine)'"
            @click="s.grain = !s.grain"
          >
            {{ s.grain ? 'ON' : 'OFF' }}
          </button>
          <button
            class="chip on"
            title="Granulator III's modes. CLASSIC: Granulator II, two overlapping grains a side. LOOP: plays a segment one grain long and crossfades into the next (XFADE), backwards as often as REVERSE says. CLOUD: DENSITY grains a side, each a little longer than the last, drifting apart — click for the next"
            @click="s.grainMode = next(GRAIN_MODES, s.grainMode)"
          >
            {{ s.grainMode.toUpperCase() }}
          </button>
          <button class="chip on" title="Window: STD (a sine), FALL (its falling half), RISE (its rising half), NOIZ (noise) — click for the next" @click="s.grainSymmetry = next(SYMMETRIES, s.grainSymmetry)">
            {{ s.grainSymmetry.toUpperCase() }}
          </button>
          <button class="chip on" title="SPRAY goes either way (SYM), only later in the sample (RIGHT) or only earlier (LEFT) — click for the next" @click="s.grainSpraySign = next(SPRAY_SIGNS, s.grainSpraySign)">
            {{ s.grainSpraySign.toUpperCase() }}
          </button>
          <button class="chip on" title="FLUX: each grain's level is random; VOID: grains drop out — click to switch" @click="s.grainAmpMode = s.grainAmpMode === 'flux' ? 'void' : 'flux'">
            {{ s.grainAmpMode.toUpperCase() }}
          </button>
          <button class="chip" :class="{ on: s.grainFm }" title="FM: a sine swings where the grains read, at FM HZ (following the keys by FM<KEY)" @click="s.grainFm = !s.grainFm">FM</button>
          <button class="chip" :class="{ on: s.grainScanOn }" title="SCAN: from each note, POS travels S.DIST of the clip; S.TIME 100 % is real time, 200 % half speed" @click="s.grainScanOn = !s.grainScanOn">SCAN</button>
          <button class="chip" :class="{ on: s.grainScanLoop, dim: !s.grainScanOn }" title="S.LOOP: SCAN starts over each time it has travelled S.DIST, instead of stopping there" @click="s.grainScanLoop = !s.grainScanLoop">S.LOOP</button>
          <button class="chip" :class="{ on: s.grainScanKey, dim: !s.grainScanOn }" title="S.KEY: SCAN goes faster for higher notes (an octave up, twice as fast)" @click="s.grainScanKey = !s.grainScanKey">S.KEY</button>
          <button class="fold" :title="isFolded('grain') ? 'Show' : 'Fold'" @click="toggleFold('grain')"><i /></button>
        </h4>
        <div class="row">
          <Knob :data-param="'grainFreq'" v-model="s.grainFreq" label="GRAIN" :min="grainMin()" :max="GRAIN_FREQ_MAX" curve="pow" :default="GRAIN_DEFAULTS.grainFreq" :format="fmtGrainHz" color="accent" />
          <Knob :data-param="'grainFreqKey'" v-model="s.grainFreqKey" label="G<KEY" :default="GRAIN_DEFAULTS.grainFreqKey" :format="fmtPct" color="accent" />
          <Knob :data-param="'grainFreqRnd'" v-model="s.grainFreqRnd" label="G<RND" curve="pow2" :default="0" :format="fmtPct" color="accent" />
          <Knob :data-param="'grainStereo'" v-model="s.grainStereo" label="SPREAD" curve="pow" :default="0" :format="fmtPct" color="accent" />
          <Knob :data-param="'grainWindow'" v-model="s.grainWindow" label="SHAPE" :default="0" :format="fmtPct" color="accent" />
          <Knob :data-param="'grainPos'" v-model="s.grainPos" label="POS" :default="0.5" :format="fmtPct" color="accent" />
          <Knob :data-param="'grainPosKey'" v-model="s.grainPosKey" label="P<KEY" :min="-1" :max="1" :default="0" bipolar :format="fmtSignedPct" color="accent" />
          <Knob :data-param="'grainSpray'" v-model="s.grainSpray" label="SPRAY" :max="SPRAY_MAX" curve="pow5" :default="0" :format="fmtMs" color="accent" />
          <Knob :data-param="'grainSpraySlope'" v-model="s.grainSpraySlope" label="SLOPE" :min="1" :max="10" curve="pow" :default="1" :format="fmtFix1" color="accent" />
          <Knob :data-param="'grainTuneKey'" v-model="s.grainTuneKey" label="T<KEY" :default="GRAIN_DEFAULTS.grainTuneKey" :format="fmtPct" color="accent" />
          <Knob :data-param="'grainTuneRnd'" v-model="s.grainTuneRnd" label="T<RND" :default="0" :format="fmtPct" color="accent" />
        </div>
        <div class="row">
          <Knob :data-param="'grainAmp'" v-model="s.grainAmp" :label="s.grainAmpMode.toUpperCase()" :default="0" :format="fmtPct" color="accent" />
          <Knob :data-param="'grainVoid'" v-model="s.grainVoid" label="RESID" :default="0" :format="fmtPct" color="accent" :class="{ dim: s.grainAmpMode !== 'void' }" />
          <Knob :data-param="'grainFmFreq'" v-model="s.grainFmFreq" label="FM HZ" :min="FM_FREQ_MIN" :max="FM_FREQ_MAX" curve="pow5" :default="GRAIN_DEFAULTS.grainFmFreq" :format="fmtHz" color="accent" :class="{ dim: !s.grainFm }" />
          <Knob :data-param="'grainFmAmount'" v-model="s.grainFmAmount" label="FM AMT" :max="FM_AMOUNT_MAX" curve="pow" :default="0" :format="fmtFix1" color="accent" :class="{ dim: !s.grainFm }" />
          <Knob :data-param="'grainFmKey'" v-model="s.grainFmKey" label="FM<KEY" :max="2" :default="GRAIN_DEFAULTS.grainFmKey" :format="fmtPct" color="accent" :class="{ dim: !s.grainFm }" />
          <Knob :data-param="'grainScanTime'" v-model="s.grainScanTime" label="S.TIME" :min="SCAN_TIME_MIN" :max="SCAN_TIME_MAX" curve="pow4" :default="GRAIN_DEFAULTS.grainScanTime" :format="fmtWholePct" color="accent" :class="{ dim: !s.grainScanOn }" />
          <Knob :data-param="'grainScanDist'" v-model="s.grainScanDist" label="S.DIST" :min="SCAN_DIST_MIN" :max="1" curve="pow4" :default="GRAIN_DEFAULTS.grainScanDist" :format="fmtFinePct" color="accent" :class="{ dim: !s.grainScanOn }" />
          <Knob :data-param="'grainScanCurve'" v-model="s.grainScanCurve" label="CURVE" :min="0.5" :max="2" curve="pow2" :default="1" :format="fmtFix2" color="accent" :class="{ dim: !s.grainScanOn }" />
        </div>
        <!-- Granulator III -->
        <div class="row">
          <Knob data-param="grainSkew" v-model="s.grainSkew" :label="s.grainMode === 'loop' ? 'XFADE' : 'SKEW'" :default="GRAIN_DEFAULTS.grainSkew" :format="fmtPct" color="accent" :class="{ dim: s.grainMode === 'cloud' }" />
          <Knob data-param="grainDensity" v-model="s.grainDensity" label="DENSITY" :min="DENSITY_MIN" :max="DENSITY_MAX" :step="1" :default="GRAIN_DEFAULTS.grainDensity" :format="fmtNum" color="accent" :class="{ dim: s.grainMode !== 'cloud' }" />
          <Knob data-param="grainLoopReverse" v-model="s.grainReverse" label="REVERSE" :default="0" :format="fmtPct" color="accent" :class="{ dim: s.grainMode !== 'loop' }" />
          <Knob data-param="grainVariation" v-model="s.grainVariation" label="VARIATION" curve="pow2" :default="0" :format="fmtPct" color="accent" />
          <Knob data-param="grainVarPos" v-model="s.grainVarPos" label="V.POS" :max="VAR_POS_MAX" curve="pow2" :default="GRAIN_DEFAULTS.grainVarPos" :format="fmtMs" color="accent" :class="{ dim: !s.grainVariation }" />
          <Knob data-param="grainVarSize" v-model="s.grainVarSize" label="V.SIZE" :max="VAR_SIZE_MAX" :default="0" :format="fmtSemis" color="accent" :class="{ dim: !s.grainVariation }" />
          <Knob data-param="grainVarPitch" v-model="s.grainVarPitch" label="V.PITCH" :max="VAR_PITCH_MAX" :default="0" :format="fmtSemis" color="accent" :class="{ dim: !s.grainVariation || s.grainMode !== 'cloud' }" />
          <Knob data-param="grainVarVol" v-model="s.grainVarVol" label="V.VOL" :max="VAR_VOL_MAX" curve="pow2" :default="0" :format="fmtFix1" color="accent" :class="{ dim: !s.grainVariation }" />
          <Knob data-param="grainDetune" v-model="s.grainDetune" label="DETUNE" :max="DETUNE_MAX" curve="pow4" :default="0" :format="fmtSemis" color="accent" />
          <Knob data-param="grainNoteRnd" v-model="s.grainNoteRnd" label="N<RND" :max="NOTE_RND_MAX" curve="pow2" :default="0" :format="fmtSemis" color="accent" />
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
.panel.wide .sound-title{width:210px;display:grid;grid-template-columns:1fr 1fr;gap:5px;align-items:center}
.sound-caption,.sound-title input{grid-column:1/-1}.sound-title input{width:100%;padding:3px 7px!important;color:var(--c-primary);background:var(--lcd-bg)!important;font:13px monospace!important}
.sound-title button{font:10px monospace;padding:4px 6px;background:linear-gradient(var(--panel-hi),var(--panel-lo));box-shadow:inset 0 1px #ffffff15,0 1px #0008;border:1px solid #0009}
.secondary-modules{margin-top:4px}
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
  flex-wrap: wrap;
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
.module .dim {opacity:.45}
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
