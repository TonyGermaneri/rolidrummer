import {describe,it,expect} from 'vitest'
import {factory,normalize,clone} from '../model'
import {keyOrigins} from './keyMapping'
import {captureSoundKit,withSoundKit} from './kits'
import {seaboardKeys,keyAtPoint,keySlide,keyBed,ribbonAtPoint,ribbonLanes,surfaceAtPoint} from './seaboardKeys'
const legacy=[48,50,52,53,55,57,59,60,62,64,65,67,69,71,72,74]
describe('chromatic keywaves and parallel pitches',()=>{
 it('repeats the ordered bank at both edges across every MIDI pitch without inventing bindings',()=>{
  const patch=factory(),origins=keyOrigins(patch.keys)
  for(let note=0;note<128;++note)expect(origins[note]).toBe(48+((note-48)%16+16)%16)
  expect(origins[47]).toBe(63);expect(origins[64]).toBe(48)
  expect(patch.keys).toHaveLength(16)
  expect(keyOrigins([]).every(origin=>origin===-1)).toBe(true)
  expect(keyOrigins([{note:127}]).every(origin=>origin===127)).toBe(true)
  const sparse=keyOrigins([{note:80},{note:20},{note:33}]);expect(sparse[33]).toBe(33);expect(sparse[19]).toBe(80)
 })
 it('migrates legacy pitches and their neural metadata once while preserving sounds, patterns and MPE settings',()=>{
  const patch=factory(),before=clone(patch);delete patch.keyLayoutVersion
  patch.keys.forEach((key,i)=>key.note=legacy[i]);patch.neural={version:1,keys:[{key:74}],warnings:[{key:50,reason:'test'}]} as any
  normalize(patch)
  expect(patch.keys).toEqual(before.keys);expect(patch.clips).toEqual(before.clips)
  expect(patch.neural!.keys[0].key).toBe(63);expect(patch.neural!.warnings[0].key).toBe(49)
  patch.keys[1].note=90;normalize(patch);expect(patch.keys[1].note).toBe(90)
 })
 it('loads legacy kits in the same chromatic order as migrated patches',()=>{
  const patch=factory(),kit=captureSoundKit(patch,'Legacy','kit-old');delete kit.keyLayoutVersion
  kit.keys.forEach((key,i)=>{key.note=legacy[i];key.sound.s.name=`Sound ${i}`})
  const applied=withSoundKit(patch,kit)
  expect(applied.keys.map(key=>key.sound.s.name)).toEqual(Array.from({length:16},(_,i)=>`Sound ${i}`))
  expect(applied.keys.map(key=>key.routes)).toEqual(patch.keys.map(key=>key.routes))
 })
})
describe('black-key touch geometry',()=>{
 it('uses each black key’s full short height and routes the region below it to adjacent white keys',()=>{
  const keys=seaboardKeys(48)
  for(const black of keys.filter(key=>key.black)){
   const center=black.x+black.w/2,bottom=keyBed.top+black.h
   expect(keyAtPoint(keys,center,keyBed.top+1)?.note).toBe(black.note)
   expect(keySlide(black,keyBed.top)).toBe(1);expect(keySlide(black,bottom)).toBe(0)
   expect(keySlide(black,keyBed.top+black.h/2)).toBeCloseTo(.5)
   expect(keyAtPoint(keys,center-black.w*.2,bottom+1)?.note).toBe(black.note-1)
   expect(keyAtPoint(keys,center+black.w*.2,bottom+1)?.note).toBe(black.note+1)
  }
 })
 it('keeps E/F and B/C independently playable throughout the key body',()=>{
  const keys=seaboardKeys(48)
  for(const note of [52,53,59,60]){
   const key=keys.find(key=>key.note===note)!
   for(const y of [keyBed.top+1,keyBed.top+key.h/2,keyBed.top+key.h-1])expect(keyAtPoint(keys,key.x+key.w/2,y)?.note).toBe(note)
  }
  expect(keyAtPoint(keys,keyBed.left-1,keyBed.top+20)).toBeUndefined()
 })
 it('separates the white-key zone matrix from both full-width ribbons',()=>{
  const keys=seaboardKeys(48),white=keys.find(key=>!key.black)!,x=white.x+white.w/2
  expect(keyBed.top).toBe(175);expect(keyBed.top+white.h).toBe(272)
  expect(keySlide(white,keyBed.top)).toBe(1);expect(keySlide(white,keyBed.top+white.h)).toBe(0)
  for(const [y,action] of [[164,'top'],[174,'top'],[273,'stop'],[287,'stop']] as const){
   expect(keyAtPoint(keys,x,y)).toBeUndefined();expect(ribbonAtPoint(x,y)).toBe(action)
  }
  for(const y of [keyBed.top,keyBed.top+white.h])expect(ribbonAtPoint(x,y)).toBeUndefined()
  expect(ribbonAtPoint(keyBed.left-1,164)).toBeUndefined()
  expect(ribbonAtPoint(keyBed.left+keyBed.width,280)).toBeUndefined()
 })
})

it('gives every chromatic key its own top and stop lane, with white-key territory between a black key and its stop lane',()=>{
 const keys=seaboardKeys(48),lanes=ribbonLanes(keys)
 for(const lane of lanes){const x=lane.x+lane.w/2;expect(surfaceAtPoint(keys,x,168)?.key.note).toBe(lane.note);expect(surfaceAtPoint(keys,x,280)?.key.note).toBe(lane.note)}
 const black=keys.find(key=>key.note===49)!,x=black.x+black.w*.25
 expect(surfaceAtPoint(keys,x,keyBed.top+black.h+5)?.key.note).toBe(48)
 const stopLane=lanes.find(key=>key.note===49)!
 expect(surfaceAtPoint(keys,stopLane.x+stopLane.w/2,280)).toMatchObject({key:{note:49},area:'stop'})
})
