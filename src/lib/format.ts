export const fmtHz = (v: number) => (v >= 1000 ? `${(v / 1000).toFixed(v >= 10000 ? 0 : 1)}k` : `${Math.round(v)}`)
export const fmtPct = (v: number) => `${Math.round(v * 100)}`
export const fmtDb = (v: number) => `${v > 0 ? '+' : ''}${v.toFixed(1)}`
export const fmtSec = (v: number) => (v < 1 ? `${Math.round(v * 1000)}ms` : `${v.toFixed(2)}s`)
export const fmtRepeat = (v: number) => (v <= 0 ? '∞' : `×${Math.round(v)}`)
export const fmtSemis = (v: number) => `${v > 0 ? '+' : ''}${Math.round(v)}st`
export const fmtPan = (v: number) => (Math.abs(v) < 0.01 ? 'C' : `${v < 0 ? 'L' : 'R'}${Math.round(Math.abs(v) * 100)}`)
export const fmtChoke = (v: number) => (v <= 0 ? '—' : `G${Math.round(v)}`)
export const fmtQ = (v: number) => v.toFixed(1)
export const fmtNum = (v: number) => v.toFixed(1)

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
export const midiNoteName = (n: number) => `${NOTE_NAMES[n % 12]}${Math.floor(n / 12) - 1}`

/** Stable pad hue from the theme's palette: pads sharing a tag share a color; untagged pads vary by name. */
export function padHue(tag: string, name: string, hues: number[]): number {
  const key = (tag.split(',')[0].trim() || name).toLowerCase()
  let h = 0
  for (const c of key) h = (h * 31 + c.charCodeAt(0)) >>> 0
  return hues[h % hues.length]
}

export const stripExt = (name: string) => name.replace(/\.[^.]+$/, '')
export const fmtCents = (v: number) => `${v > 0 ? '+' : ''}${Math.round(v)}c`
export const fmtRatio = (v: number) => `×${v.toFixed(2)}`
export const fmtOct = (v: number) => `${v > 0 ? '+' : ''}${v.toFixed(1)}oc`
export const fmtSemisJitter = (v: number) => (v <= 0 ? 'OFF' : `±${v.toFixed(1)}`)
export const fmtScale = (v: number) => `${Math.round(v * 100)}%`
export const fmtRate = (v: number) => `${v < 1 ? v.toFixed(2) : v.toFixed(1)}Hz`
/** GRAIN: Hz, as Granulator II shows it; below 1 Hz, how long each grain lasts */
export function fmtGrainHz(v: number): string {
  if (v < 1) {
    const s = 1 / v
    return `${s < 10 ? s.toFixed(2) : s < 100 ? s.toFixed(1) : Math.round(s)}s`
  }
  return `${v < 10 ? v.toFixed(2) : v < 100 ? v.toFixed(1) : Math.round(v)}Hz`
}
/** milliseconds, then seconds */
export const fmtMs = (v: number) => (v < 1000 ? `${Math.round(v)}ms` : `${(v / 1000).toFixed(v < 10000 ? 2 : 1)}s`)
export const fmtSignedPct = (v: number) => `${v > 0 ? '+' : ''}${Math.round(v * 100)}`
/** a percentage stored as itself (SCAN TIME) */
export const fmtWholePct = (v: number) => `${Math.round(v)}%`
/** a fraction shown as a percentage, with decimals once it is small (SCAN DIST) */
export const fmtFinePct = (v: number) => (v < 0.01 ? (v * 100).toFixed(2) : v < 0.1 ? (v * 100).toFixed(1) : `${Math.round(v * 100)}`)
export const fmtFix2 = (v: number) => v.toFixed(2)
export const fmtFix1 = (v: number) => v.toFixed(1)
