import {describe,expect,it} from 'vitest'
import {factory,clone} from '../model'
import {captureSoundKit,withSoundKit} from './kits'
describe('sound-only kits',()=>{
 it('captures sounds and changes only sounds on existing keywaves',()=>{
  const source=factory();source.keys[0].sound.s.pitch=7
  const kit=captureSoundKit(source,'Acoustic','kit-1')
  expect(Object.keys(kit.keys[0]).sort()).toEqual(['note','sound'])
  const patch=factory();patch.name='My rhythm';patch.keyboardMode='toggle';patch.keys[0].parts=[42];patch.keys[0].routes[0].max=.3
  const before=clone(patch),result=withSoundKit(patch,kit)
  expect(result.keys[0].sound.s.pitch).toBe(7)
  for(let i=0;i<result.keys.length;++i)result.keys[i].sound=before.keys[i].sound
  expect(result).toEqual(before);expect(patch).toEqual(before)
 })
 it('keeps unmapped keywaves and rejects duplicate or malformed entries before applying',()=>{
  const patch=factory(),kit=captureSoundKit(patch,'Small','kit-2');kit.keys=kit.keys.slice(0,1);kit.keys[0].sound.s.pitch=9
  const next=withSoundKit(patch,kit);expect(next.keys[1]).toEqual(patch.keys[1]);expect(next.keys.length).toBe(patch.keys.length)
  expect(()=>withSoundKit(patch,{...kit,keys:[...kit.keys,...kit.keys]})).toThrow('invalid')
  expect(()=>withSoundKit(patch,{...kit,version:2} as any)).toThrow('supported')
 })
 it('loads a saved sound onto a missing keywave without importing a pattern or filter',()=>{
  const patch=factory(),kit=captureSoundKit(patch,'Extra sound','kit-3');kit.keys=[{...kit.keys[0],note:96}]
  const loaded=withSoundKit(patch,kit),key=loaded.keys.find(key=>key.note===96)!
  expect(key.sound).toEqual(kit.keys[0].sound);expect(key.parts).toEqual([])
  expect(key.zones).toEqual([{upper:1,clipId:''}]);expect(loaded.clips).toEqual(patch.clips)
 })
})
