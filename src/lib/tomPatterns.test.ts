import {describe,it,expect} from 'vitest'
import {factory,normalize,clone} from '../model'
import {isTomKey,synchronizeTomZones} from './tomPatterns'
describe('shared tom performances',()=>{
 it('links zones and boundaries by drum mask while retaining independent sounds and filters',()=>{
  const patch=factory(),toms=patch.keys.filter(isTomKey),source=toms[1],other=patch.keys[0],before=clone(other)
  const sounds=toms.map(key=>clone(key.sound)),parts=toms.map(key=>clone(key.parts))
  source.zones=[{upper:.4,clipId:'roll'},{upper:1,clipId:'fill',candidates:[{clipId:'unrelated',position:[0,0,0,0]}]}]
  source.neuralLock=true;source.zoneSource=3
  synchronizeTomZones(patch,source)
  for(const key of toms){expect(key.zones).toEqual([{upper:.4,clipId:'roll'},{upper:1,clipId:'fill'}]);expect(key.zoneSource).toBe(3);expect(key.neuralLock).toBe(true)}
  expect(toms.map(key=>key.sound)).toEqual(sounds);expect(toms.map(key=>key.parts)).toEqual(parts);expect(other).toEqual(before)
  toms[0].zones[0].clipId='edit';expect(toms[1].zones[0].clipId).toBe('roll')
 })
 it('migrates existing independent tom searches deterministically and disables divergent gesture choices',()=>{
  const patch=factory(),toms=patch.keys.filter(isTomKey)
  toms[1].neuralLock=true;toms[1].zones[2].clipId='chosen-roll'
  for(const key of toms)key.patternIntent={enabled:true,weights:[1,0,0,0],smoothingMs:0,minimumDwell:0,transitionPenalty:0}
  patch.keys.reverse();normalize(patch)
  for(const key of toms){expect(key.zones[2].clipId).toBe('chosen-roll');expect(key.patternIntent!.enabled).toBe(false)}
  expect(isTomKey({parts:[36,50]})).toBe(false);expect(isTomKey({parts:[]})).toBe(false)
 })
})
