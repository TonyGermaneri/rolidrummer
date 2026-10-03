/**
 * lin; log (min > 0); pow = cubic taper from min (fine at the bottom, reaches 0, spans huge ranges);
 * grain = GRAIN SIZE: one sample up to the whole sample, with most of the travel where grains sound like grains
 */
export type Curve = 'lin' | 'log' | 'pow' | 'grain'

/**
 * GRAIN SIZE (ms), from `min` (one sample) to `max` (the sample's length), in three log stretches: the first
 * 15 % of travel goes from one sample to 5 ms (clicks, buzz, pulsar trains), the next 70 % from 5 to 500 ms --
 * where grains sound like grains, so the middle of the knob is ~50 ms -- and the rest out to `max`. Below 5 ms
 * values snap to whole samples (of `min`).
 */
const GRAIN_LO = 5
const GRAIN_KNEE = 500
const GRAIN_A = 0.15
const GRAIN_B = 0.85
const logLerp = (a: number, b: number, q: number) => a * Math.pow(b / a, clamp(q, 0, 1))
const logPos = (a: number, b: number, v: number) => (b > a ? Math.log(v / a) / Math.log(b / a) : 0)
function grainValue(p: number, min: number, max: number): number {
  let v: number
  if (p <= GRAIN_A) v = logLerp(min, Math.min(GRAIN_LO, max), p / GRAIN_A)
  else if (max <= GRAIN_KNEE) v = logLerp(GRAIN_LO, Math.max(GRAIN_LO, max), (p - GRAIN_A) / (1 - GRAIN_A))
  else if (p <= GRAIN_B) v = logLerp(GRAIN_LO, GRAIN_KNEE, (p - GRAIN_A) / (GRAIN_B - GRAIN_A))
  else v = logLerp(GRAIN_KNEE, max, (p - GRAIN_B) / (1 - GRAIN_B))
  return v < GRAIN_LO ? Math.max(1, Math.round(v / min)) * min : v
}
function grainPct(v: number, min: number, max: number): number {
  const x = clamp(v, min, max)
  if (x <= GRAIN_LO) return logPos(min, Math.min(GRAIN_LO, max), x) * GRAIN_A
  if (max <= GRAIN_KNEE) return GRAIN_A + logPos(GRAIN_LO, max, x) * (1 - GRAIN_A)
  if (x <= GRAIN_KNEE) return GRAIN_A + logPos(GRAIN_LO, GRAIN_KNEE, x) * (GRAIN_B - GRAIN_A)
  return GRAIN_B + logPos(GRAIN_KNEE, max, x) * (1 - GRAIN_B)
}

export const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v))

/** Map a value to 0..1 knob travel. */
export function valueToPct(value: number, min: number, max: number, curve: Curve = 'lin'): number {
  if (max === min) return 0
  const v = clamp(value, min, max)
  if (curve === 'log') return Math.log(v / min) / Math.log(max / min)
  if (curve === 'pow') return Math.cbrt((v - min) / (max - min))
  if (curve === 'grain') return grainPct(v, min, max)
  return (v - min) / (max - min)
}

/** Map 0..1 knob travel to a value, snapped to `step` when given. */
export function pctToValue(pct: number, min: number, max: number, curve: Curve = 'lin', step = 0): number {
  const p = clamp(pct, 0, 1)
  let v =
    curve === 'log' ? min * Math.pow(max / min, p)
    : curve === 'pow' ? min + p ** 3 * (max - min)
    : curve === 'grain' ? grainValue(p, min, max)
    : min + p * (max - min)
  if (step > 0) v = Math.round((v - min) / step) * step + min
  // avoid float noise like 0.30000000000000004
  return clamp(Number(v.toPrecision(10)), min, max)
}

/** Knob pointer angle in degrees from 12 o'clock (-135..135). */
export const pctToAngle = (pct: number) => -135 + clamp(pct, 0, 1) * 270

/**
 * v-progress-circular arc for a 270° pot. Vuetify draws `value`% of a full
 * circle starting at 12 o'clock + `rotate` degrees.
 */
export function arcFor(pct: number, bipolar: boolean): { rotate: number; value: number } {
  const p = clamp(pct, 0, 1)
  if (!bipolar) return { rotate: 225, value: p * 75 }
  if (p >= 0.5) return { rotate: 0, value: (p - 0.5) * 75 }
  return { rotate: 360 + (p - 0.5) * 270, value: (0.5 - p) * 75 }
}
