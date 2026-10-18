import {describe,it,expect} from 'vitest'
import {factory,normalize,grooveNotes,legacyGrooveNotes,partName} from './model'
import {THEMES} from './theme/themes'
import {visualVars} from './theme/current'
describe('playable factory kit and legacy migration',()=>{
 it('has a complete tuned kit with backbeats, ghost notes and independent drum roles',()=>{
  const patch=factory();expect(patch.keys).toHaveLength(16)
  expect(patch.keys.map(k=>k.note)).toEqual([48,50,52,53,55,57,59,60,62,64,65,67,69,71,72,74])
  expect(patch.keys.every(k=>[0,2,4,5,7,9,11].includes(k.note%12))).toBe(true)
  for(const key of patch.keys){
   expect(key.routes.map(r=>r.source)).toEqual([0,1,2,3,4])
   expect(partName(key.parts)).not.toBe('Custom')
   expect(key.routes.find(r=>r.target==='pitch')!.min+2).toBe(key.sound.s.pitch)
   for(const clip of patch.clips)expect(clip.notes.some(n=>key.parts.includes(n.note))).toBe(true)
  }
  expect(patch.keys.slice(3,6).map(k=>k.sound.s.pitch)).toEqual([7,2,-5])
  for(const clip of patch.clips){
   expect(clip.length).toBe(8)
   expect(clip.notes.filter(n=>n.note===38&&n.velocity>.7).map(n=>n.beat)).toEqual([1,3,5,7])
   expect(clip.notes.filter(n=>n.note===36).map(n=>n.beat)).not.toEqual([1,3,5,7])
   for(const n of clip.notes){expect(n.beat).toBeGreaterThanOrEqual(0);expect(n.beat).toBeLessThan(8);expect(n.velocity).toBeGreaterThan(0);expect(n.velocity).toBeLessThanOrEqual(1)}
  }
  expect(patch.clips[2].notes.filter(n=>n.note===38&&n.velocity<.4).length).toBeGreaterThan(0)
  const hat=patch.keys.find(k=>k.name==='Closed hat')!,open=patch.keys.find(k=>k.name==='Open hat')!
  expect(hat.sound.s.choke).toBe(open.sound.s.choke);expect(open.sound.s.decay).toBeGreaterThan(hat.sound.s.decay)
 })
 it('upgrades untouched old grooves and mappings while preserving edited patterns and explicit unmapping',()=>{
  const patch=factory();patch.clips[0].notes=legacyGrooveNotes(0,true).map(n=>({velocity:n.velocity,note:n.note,duration:n.duration,beat:n.beat}))
  patch.clips[1].notes=legacyGrooveNotes(1,true);patch.clips[1].notes[0].velocity=.123;patch.clips[0].length=4;patch.clips[1].length=4;patch.clips[2].notes=legacyGrooveNotes(2);patch.clips[2].length=4
  delete patch.keys[0].mpeDefaultsVersion;patch.keys[0].routes=[]
  patch.keys[1].routes=[]
  normalize(patch)
  expect(patch.clips[0].notes).toEqual(grooveNotes(0));expect(patch.clips[0].length).toBe(8);expect(patch.clips[2].notes).toEqual(grooveNotes(2));expect(patch.clips[1].notes[0].velocity).toBe(.123)
  expect(patch.keys[0].routes).toHaveLength(5);expect(patch.keys[1].routes).toHaveLength(0)
 })
 it('derives distinct readable zone colors for every SSB theme',()=>{
  for(const theme of THEMES){const vars=visualVars(theme);const zones=Array.from({length:8},(_,i)=>vars[`--zone-${i}`]);zones.forEach(c=>expect(c).toMatch(/^#[0-9a-f]{6}$/i));expect(new Set(zones).size).toBeGreaterThan(1);expect(vars['--ink']).toBe(theme.text[0])}
 })
})


describe('SSB grain model migration',()=>{
 it('preserves a saved cloud length, position and expression routes before applying new defaults',()=>{
  const patch=factory();const key=patch.keys[0]
  key.sound.s={grain:true,grainSize:80,grainPos:.23,grainRate:30,grainStreams:4,reverse:true} as any
  key.routes=[{source:2,target:'grainSize',min:20,max:200,curve:1,smoothing:8}]
  normalize(patch)
  expect(key.sound.s.grainFreq).toBe(12.5);expect(key.sound.s.grainPos).toBe(.23)
  expect(key.sound.s.grain).toBe(true);expect(key.sound.s.reverse).toBe(true)
  expect(key.sound.s.grainSymmetry).toBe('std');expect(key.sound.s.grainScanOn).toBe(false)
  expect(key.sound.s).not.toHaveProperty('grainSize');expect(key.sound.s).not.toHaveProperty('grainStreams')
  expect(key.routes[0].target).toBe('grainSize')
  const saved=JSON.stringify(patch);normalize(patch);expect(JSON.stringify(patch)).toBe(saved)
 })
 it('keeps new FM, scanning and window selections through normalization and recall',()=>{
  const patch=factory();Object.assign(patch.keys[0].sound.s,{grain:true,grainFreq:.05,grainFm:true,grainFmAmount:45,grainScanOn:true,grainScanTime:240,grainSymmetry:'noiz',grainAmpMode:'void'})
  const recalled=normalize(JSON.parse(JSON.stringify(patch)))
  expect(recalled.keys[0].sound.s).toEqual(patch.keys[0].sound.s)
 })
})

 it('defaults old patches to CC64 and preserves valid reassignment',()=>{
  const p=factory();delete (p as Partial<typeof p>).sustainCC
  expect(normalize(p).sustainCC).toBe(64)
  p.sustainCC=66;expect(normalize(JSON.parse(JSON.stringify(p))).sustainCC).toBe(66)
  p.sustainCC=74;expect(normalize(p).sustainCC).toBe(64)
 })

it('defaults legacy keyboard behavior and recalls Hold/Toggle mode',()=>{
 const patch=factory();delete (patch as Partial<typeof patch>).keyboardMode
 expect(normalize(patch).keyboardMode).toBe('normal')
 for(const mode of ['hold','toggle'] as const){patch.keyboardMode=mode;expect(normalize(JSON.parse(JSON.stringify(patch))).keyboardMode).toBe(mode)}
 ;(patch as any).keyboardMode='invalid';expect(normalize(patch).keyboardMode).toBe('normal')
})
