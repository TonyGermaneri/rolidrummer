/**
 * Granulator III's filter: two 12 dB trapezoidal state-variable stages in series, each left and right. The page runs
 * it in an AudioWorklet (svf processor in grains.worklet.ts), the plugin's engine the same in C++ (ssb::dsp::Svf).
 *
 * A stage is LP, HP, NOTCH or off (passing its input, its state held). Its NOTCH is Granulator III's own:
 * x − bp + (1 − k)·lp, a true notch only at Q 1, a dip that also takes the lows down below it and a resonant bump
 * above it. WIDTH puts the second stage that many semitones from the first (types that use it); the second stage's
 * Q is 0.707, or for LP+HP and NO+NO RES mapped from 0.7..7 onto 0.7..3.
 */

export type SvfStage = 0 | 1 | 2 | 3 // off, LP, NOTCH, HP

/** Granulator III's filter types, as SSB's FILTER types. */
export const SVF_TYPES = ['lp12', 'lp24', 'lphp', 'lpnotch', 'notch', 'notch2', 'hpnotch', 'hp12', 'hp24'] as const
export type SvfType = (typeof SVF_TYPES)[number]

/** stage 1, stage 2, WIDTH used, stage 2's Q follows RES */
export const SVF_TABLE: Record<SvfType, [SvfStage, SvfStage, boolean, boolean]> = {
  lp12: [1, 0, false, false],
  lp24: [1, 1, false, false],
  lphp: [1, 3, true, true],
  lpnotch: [1, 2, true, false],
  notch: [2, 0, false, false],
  notch2: [2, 2, true, true],
  hpnotch: [3, 2, true, false],
  hp12: [3, 0, false, false],
  hp24: [3, 3, false, false],
}

export const isSvfType = (t: string): t is SvfType => t in SVF_TABLE

/** stage 2's Q */
export const svfQ2 = (type: SvfType, q: number) => (SVF_TABLE[type][3] ? 0.7 + ((q - 0.7) * 2.3) / 6.3 : 0.707)

/** the knob's cutoff follows it by 0.02 a sample (about 1 ms) */
export const SVF_SMOOTH = 0.02
/** coefficients are worked out every this many samples (the plugin's control rate) */
export const SVF_EVERY = 16

/** g for cutoff `fc` (clamped to 10 Hz .. 0.45 × the sample rate) */
export function svfG(fc: number, sampleRate: number): number {
  const f = Math.min(0.45 * sampleRate, Math.max(10, fc))
  return Math.tan((Math.PI * f) / sampleRate)
}

/** One stage's state. */
export interface SvfState {
  s1: number
  s2: number
}

/** One sample through a stage with coefficients g, k (= 1 / Q). */
export function svfTick(st: SvfState, mode: SvfStage, g: number, k: number, x: number): number {
  if (mode === 0) return x
  const hp = (x - st.s2 - (k + g) * st.s1) / (1 + g * (g + k))
  const bp = st.s1 + g * hp
  const lp = st.s2 + g * bp
  st.s1 = bp + g * hp
  st.s2 = lp + g * bp
  return mode === 1 ? lp : mode === 3 ? x - k * bp - lp : x - bp + (1 - k) * lp
}
