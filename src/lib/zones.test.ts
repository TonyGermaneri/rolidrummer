import { describe, expect, it } from 'vitest'
import { splitZone, removeZone, setBoundary } from './zones'
describe('zone editing', () => {
 it('preserves complete coverage and assignments while splitting or removing end zones', () => {
  const zones = [{ upper: .2, clipId: 'hat' }, { upper: 1, clipId: 'fill' }]
  expect(splitZone(zones, 1)).toBe(true)
  expect(zones).toEqual([{ upper: .2, clipId: 'hat' }, { upper: .6, clipId: 'fill' }, { upper: 1, clipId: 'fill' }])
  removeZone(zones, 2); expect(zones[1].upper).toBe(1)
  removeZone(zones, 0); expect(zones).toEqual([{ upper: 1, clipId: 'fill' }])
  expect(removeZone(zones, 0)).toBe(false)
 })
 it('keeps boundaries ordered and limits a patch to eight nonempty zones', () => {
  const zones = [{ upper: .2, clipId: 'hat' }, { upper: 1, clipId: 'fill' }]
  setBoundary(zones, 0, 8); expect(zones[0].upper).toBe(.99)
  setBoundary(zones, 0, -8); expect(zones[0].upper).toBe(.01)
  expect(splitZone(zones, 0)).toBe(false)
  for (let i = 0; i < 6; i++) expect(splitZone(zones, zones.length - 1)).toBe(true)
  expect(splitZone(zones, zones.length - 1)).toBe(false)
  expect(zones.length).toBe(8); expect(zones.at(-1)?.upper).toBe(1)
 })
})
