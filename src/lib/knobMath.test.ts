import { describe, expect, it } from 'vitest'
import { arcFor, pctToAngle, pctToValue, valueToPct } from './knobMath'

describe('knob math', () => {
  it('round-trips linear values', () => {
    expect(valueToPct(5, 0, 10)).toBe(0.5)
    expect(pctToValue(0.5, 0, 10)).toBe(5)
    expect(pctToValue(0.33, -12, 12, 'lin', 0.5)).toBe(-4)
  })

  it('round-trips log values', () => {
    expect(pctToValue(0, 20, 20000, 'log')).toBe(20)
    expect(pctToValue(1, 20, 20000, 'log')).toBe(20000)
    expect(pctToValue(valueToPct(632, 20, 20000, 'log'), 20, 20000, 'log')).toBeCloseTo(632, 3)
  })

  it('pow taper reaches 0 and spans a long range with fine control low down', () => {
    expect(pctToValue(0, 0, 60000, 'pow')).toBe(0)
    expect(pctToValue(1, 0, 60000, 'pow')).toBe(60000)
    expect(pctToValue(0.2, 0, 60000, 'pow', 1)).toBe(480)
    expect(valueToPct(480, 0, 60000, 'pow')).toBeCloseTo(0.2)
  })

  it('grain curve: one sample, 5-500 ms over most of the travel, then out to the sample length', () => {
    const one = 1000 / 48000
    const at = (p: number) => pctToValue(p, one, 30000, 'grain')
    expect(at(0)).toBeCloseTo(one, 9) // a single sample: no OFF position any more
    expect(at(0.15)).toBeCloseTo(5, 6)
    expect(at(0.5)).toBeGreaterThan(30)
    expect(at(0.5)).toBeLessThan(80) // the middle of the knob is a grain, not a phrase
    expect(at(0.85)).toBeCloseTo(500, 6)
    expect(at(1)).toBe(30000)
    // below 5 ms it moves in whole samples
    for (const p of [0.01, 0.05, 0.1]) expect((at(p) / one) % 1).toBeCloseTo(0, 6)
    for (const v of [one, 12 * one, 5, 60, 500, 4000, 30000])
      expect(pctToValue(valueToPct(v, one, 30000, 'grain'), one, 30000, 'grain')).toBeCloseTo(v, 3)
    expect(pctToValue(1, one, 300, 'grain')).toBe(300) // short samples: one sample .. their length
    expect(valueToPct(300, one, 300, 'grain')).toBe(1)
  })

  it('clamps', () => {
    expect(valueToPct(99, 0, 10)).toBe(1)
    expect(pctToValue(-1, 0, 10)).toBe(0)
  })

  it('maps travel to a 270° sweep', () => {
    expect(pctToAngle(0)).toBe(-135)
    expect(pctToAngle(0.5)).toBe(0)
    expect(pctToAngle(1)).toBe(135)
  })

  it('draws unipolar arcs from 7:30', () => {
    expect(arcFor(1, false)).toEqual({ rotate: 225, value: 75 })
  })

  it('draws bipolar arcs from 12 o’clock', () => {
    expect(arcFor(0.5, true).value).toBe(0)
    expect(arcFor(1, true)).toEqual({ rotate: 0, value: 37.5 })
    expect(arcFor(0, true)).toEqual({ rotate: 225, value: 37.5 })
  })
})
