<script setup lang="ts">
import { computed, ref } from 'vue'
import { arcFor, pctToAngle, pctToValue, valueToPct, type Curve } from '../lib/knobMath'

const props = withDefaults(
  defineProps<{
    label: string
    min?: number
    max?: number
    step?: number
    curve?: Curve
    default?: number
    bipolar?: boolean
    size?: number
    color?: string
    format?: (v: number) => string
    /** snap back to default on release (pitch bend) */
    spring?: boolean
  }>(),
  {
    min: 0, max: 1, step: 0, curve: 'lin', default: undefined, bipolar: false, size: 34, color: 'primary',
    format: undefined, spring: false,
  },
)
const model = defineModel<number>({ required: true })

const pct = computed(() => valueToPct(model.value, props.min, props.max, props.curve))
const arc = computed(() => arcFor(pct.value, props.bipolar))
const angle = computed(() => pctToAngle(pct.value))
const readout = computed(() => (props.format ? props.format(model.value) : model.value.toFixed(2)))
const dragging = ref(false)

function setPct(p: number) {
  const v = pctToValue(p, props.min, props.max, props.curve, props.step)
  if (v !== model.value) model.value = v
}

let startY = 0
let startPct = 0
function onDown(e: PointerEvent) {
  if (e.button !== 0) return
  ;(e.currentTarget as HTMLElement).setPointerCapture(e.pointerId)
  dragging.value = true
  startY = e.clientY
  startPct = pct.value
}
function onMove(e: PointerEvent) {
  if (!dragging.value) return
  // 160px of travel for the full sweep; Shift for fine control
  const range = e.shiftKey ? 800 : 160
  setPct(startPct + (startY - e.clientY) / range)
}
function onUp() {
  dragging.value = false
  if (props.spring) reset()
}
function onWheel(e: WheelEvent) {
  setPct(pct.value + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.005 : 0.025))
}
function onKey(e: KeyboardEvent) {
  const d = e.shiftKey ? 0.005 : 0.025
  if (e.key === 'ArrowUp' || e.key === 'ArrowRight') setPct(pct.value + d)
  else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') setPct(pct.value - d)
  else if (e.key === 'Home') setPct(0)
  else if (e.key === 'End') setPct(1)
  else return
  e.preventDefault()
  e.stopPropagation()
}
function reset() {
  if (props.default !== undefined) model.value = props.default
}
</script>

<template>
  <div class="knob" :class="{ dragging }" :style="{ '--size': `${size}px` }">
    <div
      class="knob-body"
      role="slider"
      tabindex="0"
      :aria-label="label"
      :aria-valuemin="min"
      :aria-valuemax="max"
      :aria-valuenow="model"
      :aria-valuetext="readout"
      :title="`${label}: ${readout} — drag, scroll, double-click to reset`"
      @pointerdown.stop="onDown"
      @pointermove="onMove"
      @pointerup="onUp"
      @pointercancel="onUp"
      @wheel.prevent.stop="onWheel"
      @dblclick.stop="reset"
      @keydown="onKey"
      @click.stop
    >
      <v-progress-circular class="track" :model-value="75" :rotate="225" :size="size" :width="3" color="#050407" />
      <v-progress-circular
        class="arc"
        :model-value="arc.value"
        :rotate="arc.rotate"
        :size="size"
        :width="3"
        :color="color"
      />
      <div class="cap">
        <div class="pointer" :style="{ transform: `rotate(${angle}deg)` }" />
      </div>
    </div>
    <div class="readout">{{ readout }}</div>
    <div class="label">{{ label }}</div>
  </div>
</template>

<style scoped>
.knob {
  display: flex;
  flex-direction: column;
  align-items: center;
  width: calc(var(--size) + 12px);
  user-select: none;
}
.knob-body {
  position: relative;
  width: var(--size);
  height: var(--size);
  cursor: ns-resize;
  border-radius: 50%;
  outline: none;
  touch-action: none;
}
.knob-body:focus-visible {
  box-shadow: 0 0 0 2px rgb(var(--v-theme-secondary));
}
.track,
.arc {
  position: absolute;
  inset: 0;
}
.arc :deep(.v-progress-circular__overlay) {
  transition: none;
  filter: drop-shadow(0 0 3px currentColor);
}
.track :deep(.v-progress-circular__underlay),
.arc :deep(.v-progress-circular__underlay) {
  stroke: transparent;
}
.cap {
  position: absolute;
  inset: 6px;
  border-radius: 50%;
  background: radial-gradient(circle at 35% 30%, #5a5860, #26252a 60%, #111013);
  box-shadow:
    0 2px 3px rgba(0, 0, 0, 0.7),
    inset 0 1px 1px rgba(255, 255, 255, 0.18);
}
.pointer {
  position: absolute;
  inset: 0;
}
.pointer::after {
  content: '';
  position: absolute;
  left: 50%;
  top: 2px;
  width: 2px;
  height: 36%;
  margin-left: -1px;
  border-radius: 1px;
  background: #f4efe2;
}
.readout {
  margin-top: 1px;
  font-family: 'VT323', monospace;
  font-size: 13px;
  line-height: 1;
  color: rgb(var(--v-theme-primary));
  text-shadow: 0 0 4px color-mix(in srgb, rgb(var(--v-theme-primary)) 60%, transparent);
  white-space: nowrap;
}
.label {
  font-family: 'Orbitron', sans-serif;
  font-size: 7.5px;
  font-weight: 700;
  letter-spacing: 0.08em;
  color: var(--text-dim);
  text-transform: uppercase;
  white-space: nowrap;
}
.dragging .readout {
  color: #fff;
}
</style>
