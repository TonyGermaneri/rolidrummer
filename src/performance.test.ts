import {beforeEach,describe,expect,it} from 'vitest'
import {acceptPerformance,performanceView,elapsedBeats,historyLimit,type PlayedEvent} from './performance'
import {patternGeometry,liveRectangle,pitchRange} from './lib/pianoRoll'
const hit=(overrides:Partial<PlayedEvent>={}):PlayedEvent=>({type:0,epoch:1,beat:2,duration:1,key:48,note:36,velocity:.8,owner:'a',token:'t',...overrides})
const frame=(events:PlayedEvent[],beat=3,epoch=1)=>acceptPerformance({performance:{events,beat,epoch,bpm:120,dropped:0}})
beforeEach(()=>{performanceView.epoch=-1;performanceView.notes=[];performanceView.patterns={}})
describe('live drum monitoring',()=>{
 it('retains independent source keywaves and ends all owners of a shared scheduled note',()=>{
  frame([hit(),hit({owner:'b',key:49}),hit({token:'other',owner:'b',note:38})])
  frame([hit({type:1,beat:2.5})])
  expect(performanceView.notes.map(n=>n.end)).toEqual([2.5,2.5,3])
  frame([hit({type:2,owner:'b',beat:2.7})])
  expect(performanceView.notes.map(n=>n.end)).toEqual([2.5,2.5,2.7])
 })
 it('bounds history, has finite endings when events overflow and resets on a new audio epoch',()=>{
  frame(Array.from({length:historyLimit+5},(_,i)=>hit({token:String(i)})))
  expect(performanceView.notes).toHaveLength(historyLimit)
  expect(performanceView.notes[0].token).toBe('5')
  expect(performanceView.notes.every(n=>n.end===3)).toBe(true)
  frame([hit({type:3,beat:2.2})]);expect(performanceView.notes.every(n=>n.end===2.2)).toBe(true)
  frame([hit()],0,2);expect(performanceView.notes).toHaveLength(0)
  frame([hit()],3,1);expect(performanceView.epoch).toBe(2)
 })
 it('retains the last actual pattern and frozen marker, and invalidates it on active configuration change',()=>{
  const position={identity:'config:1:48:0',key:48,zone:2,candidate:3,phase:1,playing:true}
  const pattern={...position,known:true,name:'Actual fill',length:4,notes:[hit()]}
  acceptPerformance({pattern,patternPosition:position})
  acceptPerformance({patternPosition:{...position,phase:1.5,playing:false}})
  expect(performanceView.patterns[48].pattern.name).toBe('Actual fill')
  expect(performanceView.patterns[48].position).toMatchObject({phase:1.5,playing:false})
  acceptPerformance({patternPosition:{...position,identity:'new-config:1:49:0',key:49}})
  expect(performanceView.patterns[48]).toBeUndefined()
 })
 it('limits interpolation when audio stops and does not run backwards before the latest snapshot',()=>{
  expect(elapsedBeats(5000,1000,120)).toBe(.2)
  expect(elapsedBeats(900,1000,120)).toBe(0)
 })
})
describe('piano roll geometry',()=>{
 it('filters MIDI drum pitches for each keywave and wraps note tails across a loop boundary',()=>{
  const notes=[hit({beat:3.5,duration:1}),hit({note:38,beat:1})]
  const geometry=patternGeometry(notes,4,[36])
  expect(geometry.count).toBe(1);expect(geometry.paths.join('')).toContain('M875')
  expect(geometry.paths.join('')).toContain('M0 ')
  expect(patternGeometry(notes,4,[]).count).toBe(0)
  expect(pitchRange([hit({note:127})])).toEqual({low:116,high:127})
 })
 it('positions live notes so their start scrolls left from a fixed playhead while the future is clipped',()=>{
  const note={beat:10,end:12,note:36},r=liveRectangle(note,0,50)
  const screenStart=(beat:number)=>r.x+900-beat*50
  const visibleWidth=(beat:number)=>Math.min(r.width,900-screenStart(beat))
  expect(screenStart(10)).toBe(900);expect(visibleWidth(10)).toBe(0)
  expect(screenStart(11)).toBe(850);expect(visibleWidth(11)).toBe(50)
  expect(visibleWidth(13)).toBe(100);expect(r.y).toBe((127-36)*12+2)
 })
})
