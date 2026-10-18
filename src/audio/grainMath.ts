/**
 * The grain cloud, after Robert Henke's Granulator II (Max for Live; its voice is ml.poly.granulator130120.maxpat).
 * The page's worklet (grains.worklet.ts) runs it and native/engine mirrors it line for line.
 *
 * Per note there are two grain streams, left and right, each driven by a phasor at the GRAIN frequency. A grain
 * starts each time the phasor wraps and another half a period later, and each grain's window spans one whole
 * period, so grains are always 1 / GRAIN long and two overlap. GRAIN follows the keyboard (G<KEY) and so does the
 * sample's playback rate (T<KEY): turn GRAIN up and the grains become a pitched buzz that plays in tune with the
 * keys. Where a grain reads (POS, SPRAY, SCAN), its pitch (T<RND) and its level (FLUX / VOID) are drawn when it
 * starts; FM moves the read position continuously.
 */

export type Symmetry = 'std' | 'fall' | 'rise' | 'noiz'
export const SYMMETRIES: Symmetry[] = ['std', 'fall', 'rise', 'noiz']
export type SpraySign = 'sym' | 'right' | 'left'
export const SPRAY_SIGNS: SpraySign[] = ['sym', 'right', 'left']
export type AmpMode = 'flux' | 'void'

/** What a voice's grain processor plays; sent again whenever a knob moves. Times are AudioContext seconds. */
export interface GrainConfig {
  kind: 'cloud' | 'stretch'
  /** the clip, seconds of the sample */
  clipIn: number
  clipOut: number
  freq: number // GRAIN: Hz; each grain lasts one period, two overlap
  freqKey: number // G<KEY 0..1: GRAIN follows the note by this much of its semitones from ROOT
  freqRnd: number // G<RND 0..1: GRAIN wanders up to ±25 semitones, a new target 8 × GRAIN times a second
  stereo: number // SPREAD 0..1: left grains at GRAIN / (1 + s²), right at GRAIN × (1 + s²)
  pos: number // POS 0..1 of the clip
  posKey: number // P<KEY -1..1: at ±1 POS moves 1 % of the clip per semitone
  spray: number // SPRAY: ms of random offset per grain
  spraySlope: number // 1..10: offsets cluster nearer POS as it rises
  spraySign: SpraySign
  window: number // SHAPE 0..1: sine window .. a square half a period wide
  symmetry: Symmetry
  tuneKey: number // T<KEY 0..1: the note transposes the sample by this much
  tuneRnd: number // T<RND 0..1: each grain's rate × (1 ± 0.5 · T<RND²)
  ampMode: AmpMode
  amp: number // FLUX: random level per grain / VOID: chance of dropping one (√ of it)
  voidLevel: number // VOID: what a dropped grain keeps (its 4th power)
  fm: boolean
  fmFreq: number // Hz
  fmAmount: number // 0..250: the read position swings ± amount × 0.02 ms
  fmKey: number // 0..2: the FM frequency follows the note
  scan: boolean
  scanTime: number // %: 100 plays through at real time, 200 at half speed
  scanDist: number // 0..1 of the clip
  scanCurve: number // 0.5..2
  speed: number // stretch: clip seconds per second
  kRate: number // the pad's PITCH / FINE as a playback rate
  glide: { from: number; to: number; start: number; dur: number } // semitones from ROOT
  t0: number
  /** no grain starts after this */
  end: number
}

/** A grain as it started, for the waveform. pos / len in seconds of the sample; when / dur in context time. */
export interface GrainEvent {
  when: number
  pos: number
  len: number
  dur: number
  rate: number // playback rate (pitch)
  reverse: boolean
  pan: number
  gain: number // level, 0..1
  stream: number // 0 left, 1 right
}

export type ToGrains =
  | { type: 'config'; config: GrainConfig }
  | { type: 'sample'; id: string; channels: Float32Array[]; rate: number }
  | { type: 'stop' }
export type FromGrains = { type: 'grains'; list: GrainEvent[] } | { type: 'need'; id: string } | { type: 'done' }

/** The knobs' ranges (Granulator II's). */
/** Granulator II's floor (4 s grains); the knob's own reaches one grain as long as the sample, see grainFreqMin */
export const GRAIN_FREQ_MIN = 0.25
/** The lowest GRAIN kept at all: a grain an hour long. */
export const GRAIN_FREQ_FLOOR = 1 / 3600
/** GRAIN's floor for a sample `seconds` long: one grain lasting the whole sample (0.25 Hz until it is decoded). */
export const grainFreqMin = (seconds: number) =>
  seconds > 0 ? Math.min(GRAIN_FREQ_MAX, Math.max(GRAIN_FREQ_FLOOR, 1 / seconds)) : GRAIN_FREQ_MIN
export const GRAIN_FREQ_MAX = 150
export const SPRAY_MAX = 20000 // ms
export const FM_FREQ_MIN = 2
export const FM_FREQ_MAX = 20000
export const FM_AMOUNT_MAX = 250
export const SCAN_TIME_MIN = 25
export const SCAN_TIME_MAX = 10000
export const SCAN_DIST_MIN = 0.0001

/** G<RND at 1: ± this many semitones. */
export const FREQ_RND_SEMIS = 25
/** G<RND's random target changes this many times per GRAIN period (Max's rand~ at 8 × GRAIN). */
export const FREQ_RND_RATE = 8
/** A phasor never moves more than half a cycle a sample (so each grain slot starts at most once). */
export const MAX_PHASE_STEP = 0.5

/** Grains started by the stretch mode (SPEED without pitch): fixed size, four overlapping, steady. */
export const STRETCH_GRAIN = 0.09 // seconds
export const STRETCH_OVERLAP = 4

// ───────────────────────────────────────────────────────────────────────── window

/** Points in a window table (Max's cycle~ reads 512 of a buffer); one more so interpolation wraps. */
export const WINDOW_POINTS = 512

/** xorshift32, so both engines draw the same NOIZ table. */
export function xorshift(seed: number): () => number {
  let s = seed >>> 0 || 1
  return () => {
    s ^= s << 13
    s >>>= 0
    s ^= s >>> 17
    s ^= s << 5
    s >>>= 0
    return s
  }
}

/** Granulator II's "table noiz": 514 values 0..127 (a Max table's range), fixed. */
export const NOIZ_TABLE: Int32Array = (() => {
  const next = xorshift(0x5eed)
  const t = new Int32Array(WINDOW_POINTS + 2)
  for (let i = 0; i < t.length; i++) t[i] = next() % 128
  return t
})()

/**
 * The grain window, as Granulator II's Window patch fills its 512-point buffer. Index i places a point x on a
 * 0..513 axis (Std: x = i; Fall: the falling half stretched over the grain; Rise: the rising half; Noiz: a random
 * point per index), folds it about the middle to u, squeezes u towards a quarter period by SHAPE's factor
 * F = 1 + (4 · shape)³ (so the edges steepen and the top flattens, down to a square half a period wide), and reads
 * the rising half of a sine there: sin(π v / 512), v = clip(u · F + 128, 0, 256). At SHAPE 0, Std is the sine window
 * sin(π i / 512) -- the square root of a Hann, so two grains half a period apart keep the power constant.
 */
export function windowTable(shape: number, symmetry: Symmetry): Float32Array {
  const F = 1 + Math.pow(0.04 * Math.min(100, Math.max(0, shape * 100)), 3)
  const t = new Float32Array(WINDOW_POINTS + 1)
  for (let i = 0; i <= WINDOW_POINTS; i++) {
    let x: number
    if (symmetry === 'fall') x = 0.5 * i + 256
    else if (symmetry === 'rise') x = 512 - 0.5 * i
    else if (symmetry === 'noiz') x = NOIZ_TABLE[i] + 256
    else x = i
    const y = Math.min(513, Math.max(0, x))
    const u = y <= 256 ? y - 128 : 385 - y
    const v = Math.min(256, Math.max(0, u * F + 128))
    t[i] = Math.sin((Math.PI * v) / 512)
  }
  return t
}

/** The window at phase 0..1 (linear between table points, as cycle~ reads it). */
export function windowAt(table: Float32Array, phase: number): number {
  const x = (phase - Math.floor(phase)) * WINDOW_POINTS
  const i = Math.floor(x)
  return table[i] + (table[i + 1] - table[i]) * (x - i)
}

// ───────────────────────────────────────────────────────────────────────── per-grain draws

/**
 * SPRAY: a grain's offset in ms, from two uniform numbers in [0, 1). Its size is |n|^slope × SPRAY (a slope of 1
 * spreads them evenly; higher keeps most near POS); its side is random (sym), always later (right) or earlier (left).
 */
export function sprayOffset(spray: number, slope: number, sign: SpraySign, u1: number, u2: number): number {
  if (spray <= 0) return 0
  const mag = Math.pow(Math.abs(2 * u1 - 1), Math.max(1, slope))
  const side = sign === 'right' ? 1 : sign === 'left' ? -1 : u2 < 0.5 ? -1 : 1
  return spray * mag * side
}

/** T<RND: a grain's rate factor, 1 ± 0.5 · amount² (uniform). */
export const tuneRndRatio = (amount: number, u: number) => 1 + 0.5 * amount * amount * (2 * u - 1)

/**
 * A grain's level. FLUX: (1 - amount · |n|)², made up by 6 dB × amount. VOID: dropped when n falls under
 * 2√amount - 1 (so √amount of them), keeping VOID LEVEL⁴ of their level. n = 2u - 1.
 */
export function grainLevel(mode: AmpMode, amount: number, voidLevel: number, u: number): number {
  if (amount <= 0) return 1
  const n = 2 * u - 1
  if (mode === 'void') return n < 2 * Math.sqrt(amount) - 1 ? Math.pow(voidLevel, 4) : 1
  const g = 1 - amount * Math.abs(n)
  return g * g * Math.pow(10, (6 * amount) / 20)
}

/** SCAN's offset from POS (seconds of the sample), `t` seconds after the note: it travels the distance at
 *  100 / SCAN TIME times real time along its curve, then stays. */
export function scanOffset(t: number, timePct: number, dist: number, curve: number, clipLen: number): number {
  const d = dist * clipLen
  if (d <= 0) return 0
  const x = Math.min(1, Math.max(0, (t * (100 / timePct)) / d))
  return d * Math.pow(x, curve)
}

// ───────────────────────────────────────────────────────────────────────── reading the sample

/**
 * 4-point, 3rd-order Hermite interpolation: the value `f` (0..1) of the way from y0 to y1, shaped by the
 * neighbours ym1 and y2 (Max's play~ reads with 4-point interpolation too).
 */
export function hermite(ym1: number, y0: number, y1: number, y2: number, f: number): number {
  const c1 = 0.5 * (y1 - ym1)
  const c2 = ym1 - 2.5 * y0 + 2 * y1 - 0.5 * y2
  const c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1)
  return ((c3 * f + c2) * f + c1) * f + y0
}

/** `x` at frame position `p`, silent outside [lo, hi) (the clip). */
export function readAt(x: Float32Array, p: number, lo: number, hi: number): number {
  const i0 = Math.floor(p)
  if (i0 < lo || i0 + 1 >= hi) return 0
  const f = p - i0
  if (i0 - 1 >= lo && i0 + 2 < hi) return hermite(x[i0 - 1], x[i0], x[i0 + 1], x[i0 + 2], f)
  return x[i0] + (x[i0 + 1] - x[i0]) * f
}

// ───────────────────────────────────────────────────────────────────────── stretch

/** STRETCH's grain window: Hann at sample `i` of `n` (sample centres, so a one-sample grain still plays). */
export function hann(i: number, n: number): number {
  if (n < 2) return 1
  const x = (i + 0.5) / n
  return 0.5 - 0.5 * Math.cos(2 * Math.PI * Math.min(1, Math.max(0, x)))
}
