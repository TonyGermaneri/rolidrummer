import { describe, expect, it } from 'vitest'
import {
  GRAIN_FREQ_FLOOR, grainFreqMin, grainLevel, hermite, readAt, scanOffset, sprayOffset, tuneRndRatio, windowAt, windowTable, WINDOW_POINTS,
} from './grainMath'

describe('grain window (Granulator II\'s Window patch)', () => {
  it('is a sine window at SHAPE 0: the square root of a Hann', () => {
    const w = windowTable(0, 'std')
    expect(w.length).toBe(WINDOW_POINTS + 1)
    for (const i of [0, 37, 128, 256, 300, 480]) expect(w[i]).toBeCloseTo(Math.sin((Math.PI * Math.min(i, 513 - i)) / 512), 6)
    expect(w[256]).toBe(1)
    // two grains half a period apart keep the power constant (within the 1 % its mirror at 513, not 512, costs)
    for (const ph of [0.05, 0.2, 0.37]) expect(Math.abs(windowAt(w, ph) ** 2 + windowAt(w, ph + 0.5) ** 2 - 1)).toBeLessThan(0.01)
  })

  it('SHAPE steepens the edges and flattens the top, to a square half a period wide', () => {
    const w = windowTable(1, 'std') // F = 65
    expect(w[100]).toBe(0)
    expect(w[200]).toBe(1)
    expect(w[256]).toBe(1)
    expect(w[330]).toBe(1)
    expect(w[420]).toBe(0)
    const half = windowTable(0.5, 'std') // F = 9: edges from 128 ∓ 128 / 9
    expect(half[110]).toBe(0)
    expect(half[125]).toBeGreaterThan(0.2)
    expect(half[125]).toBeLessThan(0.8)
    expect(half[150]).toBe(1)
  })

  it('FALL and RISE stretch one half of the window over the grain; NOIZ is noise near the top', () => {
    const fall = windowTable(0, 'fall')
    const rise = windowTable(0, 'rise')
    expect(fall[0]).toBeCloseTo(1, 3)
    expect(fall[512]).toBeLessThan(0.01)
    expect(rise[0]).toBeLessThan(0.01)
    expect(rise[512]).toBeCloseTo(1, 3)
    for (let i = 1; i <= 512; i++) {
      expect(fall[i]).toBeLessThanOrEqual(fall[i - 1] + 1e-9)
      expect(rise[i]).toBeGreaterThanOrEqual(rise[i - 1] - 1e-9)
    }
    const noiz = windowTable(0, 'noiz')
    const vals = new Set(Array.from(noiz, (v) => v.toFixed(3)))
    expect(vals.size).toBeGreaterThan(50)
    for (const v of noiz) {
      expect(v).toBeGreaterThan(0.7)
      expect(v).toBeLessThanOrEqual(1)
    }
  })
})

describe('GRAIN\'s range', () => {
  it('reaches down to one grain as long as the sample', () => {
    expect(grainFreqMin(3.32)).toBeCloseTo(1 / 3.32) // the dial's long end is the whole file
    expect(grainFreqMin(30)).toBeCloseTo(1 / 30)
    expect(grainFreqMin(0)).toBe(0.25) // not decoded yet: Granulator II's floor
    expect(grainFreqMin(1e6)).toBe(GRAIN_FREQ_FLOOR)
    expect(grainFreqMin(0.001)).toBe(150) // shorter than the shortest grain
  })
})

describe('per-grain draws', () => {
  it('SPRAY offsets up to ±SPRAY ms, one-sided when asked, clustered near POS by SLOPE', () => {
    expect(sprayOffset(0, 1, 'sym', 0.9, 0.9)).toBe(0)
    expect(sprayOffset(100, 1, 'sym', 1 - 1e-9, 0.9)).toBeCloseTo(100)
    expect(sprayOffset(100, 1, 'sym', 1 - 1e-9, 0.1)).toBeCloseTo(-100)
    expect(sprayOffset(100, 1, 'sym', 0.75, 0.9)).toBeCloseTo(50)
    expect(sprayOffset(100, 1, 'right', 0.75, 0.1)).toBeCloseTo(50)
    expect(sprayOffset(100, 1, 'left', 0.75, 0.9)).toBeCloseTo(-50)
    expect(sprayOffset(100, 10, 'sym', 0.75, 0.9)).toBeCloseTo(100 * 0.5 ** 10)
  })

  it('T<RND moves each grain\'s rate by up to ±0.5 × amount²', () => {
    expect(tuneRndRatio(0, 0)).toBe(1)
    expect(tuneRndRatio(1, 0)).toBe(0.5)
    expect(tuneRndRatio(1, 1)).toBe(1.5)
    expect(tuneRndRatio(0.5, 0)).toBe(0.875)
  })

  it('FLUX sets a random level (made up by 6 dB × amount); VOID drops √amount of the grains', () => {
    expect(grainLevel('flux', 0, 0, 0.1)).toBe(1)
    expect(grainLevel('flux', 1, 0, 0.5)).toBeCloseTo(10 ** (6 / 20))
    expect(grainLevel('flux', 1, 0, 0)).toBe(0)
    expect(grainLevel('flux', 0.5, 0, 0)).toBeCloseTo(0.25 * 10 ** (3 / 20))
    expect(grainLevel('void', 0.25, 0.5, 0.4)).toBeCloseTo(0.0625) // dropped, keeping 0.5⁴
    expect(grainLevel('void', 0.25, 0.5, 0.6)).toBe(1)
    let dropped = 0
    for (let i = 0; i < 1000; i++) if (grainLevel('void', 0.49, 0, (i + 0.5) / 1000) < 1) dropped++
    expect(dropped / 1000).toBeCloseTo(0.7, 2)
  })

  it('SCAN travels its distance at 100 / TIME times real time, along its curve, then stays', () => {
    expect(scanOffset(0, 100, 1, 1, 2)).toBe(0)
    expect(scanOffset(1, 100, 1, 1, 2)).toBeCloseTo(1)
    expect(scanOffset(3, 100, 1, 1, 2)).toBeCloseTo(2)
    expect(scanOffset(1, 200, 1, 1, 2)).toBeCloseTo(0.5)
    expect(scanOffset(1, 100, 1, 2, 2)).toBeCloseTo(0.5)
    expect(scanOffset(1, 100, 0.25, 1, 2)).toBeCloseTo(0.5)
  })
})

describe('reading the sample', () => {
  it('passes through the samples and follows a curve between them', () => {
    expect(hermite(1, 2, 3, 4, 0)).toBe(2)
    expect(hermite(1, 2, 3, 4, 1)).toBeCloseTo(3)
    expect(hermite(1, 2, 3, 4, 0.25)).toBeCloseTo(2.25) // a line stays a line
    const sq = (x: number) => x * x
    expect(hermite(sq(-1), sq(0), sq(1), sq(2), 0.5)).toBeCloseTo(sq(0.5)) // a parabola too
    const s = (x: number) => Math.sin((Math.PI / 4) * x)
    const linear = s(0) + (s(1) - s(0)) * 0.5
    expect(Math.abs(hermite(s(-1), s(0), s(1), s(2), 0.5) - s(0.5))).toBeLessThan(Math.abs(linear - s(0.5)) / 4)
  })

  it('is silent outside the clip', () => {
    const x = new Float32Array([1, 1, 1, 1, 1, 1, 1, 1])
    expect(readAt(x, 3.5, 2, 6)).toBe(1)
    expect(readAt(x, 1.5, 2, 6)).toBe(0)
    expect(readAt(x, 5.5, 2, 6)).toBe(0)
    expect(readAt(x, -3, 0, 8)).toBe(0)
  })
})
