import {describe,it,expect} from 'vitest'
import {normalizeFx,busValue,busTargets} from './masterFx'
import {factory,normalize,clone} from './model'
describe('main bus settings',()=>{
 it('migrates old patches to dry, open-filter settings without creating mappings',()=>{
  const patch=factory();delete (patch as any).fx;normalize(patch)
  expect(patch.fx.reverbMix+patch.fx.delayMix+patch.fx.chorusMix).toBe(0)
  expect(patch.fx.cutoff).toBe(20000)
  expect(busTargets.map(t=>patch.fx.bindings[t])).toEqual([-1,-1,-1,-1,-1])
 })
 it('preserves effect settings and independent controller bindings through patch round trips',()=>{
  const patch=factory();patch.fx=normalizeFx({delayMix:.4,delaySync:true,delayBeats:.75,cutoff:800,bindings:{reverbMix:0,delayMix:1,chorusMix:2,cutoff:3,resonance:4}})
  expect(normalize(clone(patch)).fx).toEqual(patch.fx)
 })
 it('bounds malformed settings and maps the five surface values to the intended units',()=>{
  const fx=normalizeFx({cutoff:Infinity,delayFeedback:20,chorusMix:-1,bindings:{cutoff:9,resonance:1.5} as any})
  expect(fx.cutoff).toBe(20000);expect(fx.delayFeedback).toBe(.9);expect(fx.chorusMix).toBe(0);expect(fx.bindings.cutoff).toBe(-1);expect(fx.bindings.resonance).toBe(-1)
  expect(busValue('cutoff',0)).toBe(20);expect(busValue('cutoff',1)).toBe(20000);expect(busValue('cutoff',.5)).toBeCloseTo(Math.sqrt(20*20000));expect(busValue('resonance',1)).toBe(20)
 })
})
