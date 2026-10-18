import { describe, expect, it } from 'vitest'
import {
  cloudGain, cloudLength, GRAIN_FREQ_FLOOR, grainFreqMin, grainLevel, hermite, loopFade, loopSegment, readAt, scanOffset,
  skewExponent, skewPhase, sprayOffset, tuneRndRatio, varGain, varPitchRatio, varPosOffset, varSizeRatio, windowAt, windowTable,
  WINDOW_POINTS,
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

  it('S.LOOP starts SCAN over at its distance; S.KEY\'s speed scales how fast it gets there', () => {
    expect(scanOffset(2.5, 100, 1, 1, 2, true)).toBeCloseTo(0.5)
    expect(scanOffset(4.5, 100, 1, 1, 2, true)).toBeCloseTo(0.5)
    expect(scanOffset(2.5, 100, 1, 1, 2, false)).toBeCloseTo(2)
    expect(scanOffset(0.5, 100, 1, 1, 2, false, 2)).toBeCloseTo(1)
    expect(scanOffset(1.5, 100, 1, 1, 2, true, 2)).toBeCloseTo(1)
  })
})

describe('Granulator III', () => {
  it('SKEW: an exponent on the window\'s phase, 1 in the middle', () => {
    expect(skewExponent(0.5)).toBeCloseTo(1, 6)
    expect(skewExponent(0)).toBeCloseTo(0.1 ** 1.5, 6)
    expect(skewExponent(1)).toBeCloseTo(10 ** 1.5, 6)
    expect(skewExponent(-1)).toBe(skewExponent(0))
    expect(skewExponent(2)).toBe(skewExponent(1))
    for (let s = 0.05; s < 1; s += 0.05) expect(skewExponent(s)).toBeGreaterThan(skewExponent(s - 0.05))
    expect(skewPhase(0.3, 1)).toBe(0.3)
    expect(skewPhase(0, 0.03)).toBe(0)
    expect(skewPhase(0.25, 2)).toBeCloseTo(0.0625)
    // low SKEW reaches the window's top (phase 0.5) almost at once; high SKEW only near the end
    expect(0.5 ** (1 / skewExponent(0.1))).toBeLessThan(0.001)
    expect(0.5 ** (1 / skewExponent(0.9))).toBeGreaterThan(0.93)
  })

  it('CLOUD: grain i lasts 1 + √i grains; the level falls towards 0.9 as there are more', () => {
    expect([0, 1, 2, 3].map(cloudLength)).toEqual([1, 2, 1 + Math.SQRT2, 1 + Math.sqrt(3)])
    expect(cloudGain(2)).toBeCloseTo(0.9 * 2 ** (1.0573 / 2), 9)
    expect(cloudGain(20)).toBeLessThan(cloudGain(2))
    expect(cloudGain(1e9)).toBeCloseTo(0.9, 6)
  })

  it('VARIATION: off at 0; each draw scales within its range', () => {
    for (const u of [0, 0.3, 1]) {
      expect(varGain(0, 1, u)).toBe(1)
      expect(varGain(12, 0, u)).toBe(1)
      expect(varSizeRatio(0, 1, u)).toBe(1)
      expect(varSizeRatio(12, 0, u)).toBe(1)
      expect(varPitchRatio(0, 1, u)).toBe(1)
      expect(varPosOffset(0, 1, u)).toBe(0)
      expect(varPosOffset(500, 0, u)).toBe(0)
    }
    // VOL: from 1.75 of its dB quieter to a quarter of it louder
    expect(varGain(12, 1, 0)).toBeCloseTo(10 ** (-21 / 20), 9)
    expect(varGain(12, 1, 1)).toBeCloseTo(10 ** (3 / 20), 9)
    expect(varGain(12, 0.5, 1)).toBeCloseTo(10 ** (1.5 / 20), 9)
    // SIZE: ± its semitones × amount
    expect(varSizeRatio(12, 1, 1)).toBeCloseTo(2)
    expect(varSizeRatio(12, 1, 0)).toBeCloseTo(0.5)
    expect(varSizeRatio(12, 0.5, 1)).toBeCloseTo(Math.SQRT2)
    expect(varSizeRatio(12, 1, 0.5)).toBe(1)
    // PITCH: the full interval at amount 1, a linear share of it below
    expect(varPitchRatio(12, 1, 1)).toBeCloseTo(2)
    expect(varPitchRatio(12, 1, 0)).toBeCloseTo(0.5)
    expect(varPitchRatio(12, 0.5, 1)).toBeCloseTo(1.5)
    expect(varPitchRatio(12, 0.5, 0)).toBeCloseTo(0.75)
    // POS: always later, by up to amount² × ms
    expect(varPosOffset(1000, 1, 0)).toBeCloseTo(1)
    expect(varPosOffset(1000, 1, 1)).toBeCloseTo(1)
    expect(varPosOffset(1000, 1, 0.5)).toBe(0)
    expect(varPosOffset(1000, 0.5, 1)).toBeCloseTo(0.25)
    for (let u = 0; u <= 1; u += 0.1) expect(varPosOffset(800, 0.7, u)).toBeGreaterThanOrEqual(0)
  })

  it('LOOP: XFADE is SKEW of the span, no more than the room left', () => {
    expect(loopFade(0.5, 1000, 5000)).toBe(500)
    expect(loopFade(0.5, 1000, 200)).toBe(200)
    expect(loopFade(0, 1000, 200)).toBe(0)
    expect(loopFade(0.5, 1000, -10)).toBe(0)
  })

  it('LOOP: a forward segment reads up from where it wants, leaving room for the fade', () => {
    expect(loopSegment(100, 50, 10, 0, 1000, 1)).toEqual({ head: 100, end: 150, room: 850 })
    // too near the top: pulled down so its end plus the fade fits
    expect(loopSegment(980, 50, 10, 0, 1000, 1)).toEqual({ head: 940, end: 990, room: 10 })
    // below the clip: pulled up to its start
    expect(loopSegment(-20, 50, 10, 100, 1000, 1)).toEqual({ head: 100, end: 150, room: 850 })
    // a span longer than the clip: the clip
    expect(loopSegment(0, 5000, 0, 100, 1000, 1)).toEqual({ head: 100, end: 1000, room: 0 })
  })

  it('LOOP: a reversed segment reads down from its top, leaving room below it for the fade', () => {
    expect(loopSegment(100, 50, 10, 0, 1000, -1)).toEqual({ head: 150, end: 100, room: 100 })
    // too near the bottom: pushed up past the fade's room
    expect(loopSegment(3, 50, 10, 0, 1000, -1)).toEqual({ head: 60, end: 10, room: 10 })
    // too near the top: its top stays in the clip
    expect(loopSegment(990, 50, 10, 0, 1000, -1)).toEqual({ head: 1000, end: 950, room: 950 })
    expect(loopSegment(400, 50, 10, 200, 1000, -1).room).toBe(200)
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
