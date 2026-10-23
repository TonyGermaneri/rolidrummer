// @vitest-environment ./src/testing/environment.ts
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { factory, clone, factoryAssets } from '../model'
import {htmlDrag,htmlDrop} from '../drag'
import { event, find, mount, nodes, settle } from '../testing/componentHost'

const backend = vi.hoisted(() => ({ request: vi.fn(), midi: vi.fn(), state: null as any }))
vi.mock('../native/bridge', () => ({ request: backend.request, native: () => true }))
vi.mock('../store', async () => {
  const { reactive } = await import('vue')
  backend.state = reactive({ patch: {}, selected: 48, learn: -1, error: '', telemetry: { macros: [0, 0, 0, 0, 0], contacts: [] } })
  return { state: backend.state, sendMidi: backend.midi, observeKeyTouch: () => {}, attempt: async (job: () => unknown) => { try { return await job() } catch (error) { backend.state.error = String(error) } } }
})
vi.mock('./Knob.vue', async () => {
  const { defineComponent, h } = await import('vue')
  return { default: defineComponent({ setup: () => () => h('knob-contract') }) }
})
import ZoneEditor from './ZoneEditor.vue'
import Seaboard from './Seaboard.vue'
import MidiSettings from './MidiSettings.vue'
import MidiImport from './MidiImport.vue'
import SoundPanel from './SoundPanel.vue'
import Waveform from './Waveform.vue'
import ExpressionCurve from './ExpressionCurve.vue'
import ExpressionPanel from './ExpressionPanel.vue'
import KeyControls from './KeyControls.vue'
import PianoRollPanel from './PianoRollPanel.vue'
import {acceptPerformance,performanceView} from '../performance'
import { importPattern, midiImport, finishMidiImport } from '../midiImport'
import {seaboardKeys,keyBed,ribbonLanes} from '../lib/seaboardKeys'

let cleanup: (() => void)[] = []
beforeEach(() => {
  backend.state.patch = factory(); backend.state.selected = 48; backend.state.error = ''
  backend.state.ready=false;backend.state.trackKeywave=false;performanceView.patterns={};performanceView.notes=[];performanceView.epoch=-1
  backend.state.telemetry.contacts = []; backend.state.telemetry.fingers = []; backend.state.telemetry.macros = [0, 0, 0, 0, 0]
  backend.state.telemetry.touches=[]
  backend.state.telemetry.pendingLatches=[];backend.state.telemetry.ppq=0
  backend.request.mockReset().mockImplementation(async (method:string)=>method==='waveform'?{peaks:[],duration:.5}:true); backend.midi.mockReset()
  vi.stubGlobal('window', { addEventListener: vi.fn(), removeEventListener: vi.fn() })
  vi.stubGlobal('document', { activeElement: null })
  vi.stubGlobal('Document', class {}); vi.stubGlobal('ShadowRoot', class {})
})
afterEach(() => { cleanup.forEach(fn => fn()); cleanup = []; finishMidiImport(false); vi.unstubAllGlobals() })
async function render(component: any, props = {}) { const view = await mount(component, props); cleanup.push(view.unmount); return view.root }

describe('keywave piano roll contracts',()=>{
 it('shows the current song phase on unplayed zones and follows manual edits across toms',async()=>{
  backend.state.telemetry.ppq=5.25
  const key=backend.state.patch.keys.find((k:any)=>k.name==='Mid tom')
  const root=await render(ZoneEditor,{zones:key.zones,scope:String(key.note)})
  const markers=nodes(root,n=>n.tag==='line'&&String(n.props.class).includes('pattern-transport'))
  expect(markers).toHaveLength(3);for(const marker of markers)expect(marker.props.x1).toBe(656.25)
  key.zones[1].clipId='factory-2';key.zones[0].upper=.3;await settle()
  for(const tom of backend.state.patch.keys.filter((k:any)=>['High tom','Mid tom','Low tom'].includes(k.name))){expect(tom.zones[1].clipId).toBe('factory-2');expect(tom.zones[0].upper).toBe(.3)}
  expect(backend.state.patch.keys[0].zones[0].upper).toBe(.22)
  backend.state.telemetry.ppq=6;await settle()
  for(const marker of nodes(root,n=>n.tag==='line'&&String(n.props.class).includes('pattern-transport')))expect(marker.props.x1).toBe(750)
 })

 it('shows distinct queued start/stop indicators on parallel and origin keys even after the finger has lifted',async()=>{
  backend.state.telemetry.fingers=[];backend.state.telemetry.pendingLatches=[{note:64,origin:48,action:'start'}]
  const root=await render(Seaboard,{base:36})
  for(const note of [48,64]){
   const marker=find(root,n=>n.props['data-queued-key']===note)
   expect(marker.props.class).toContain('latch-start');expect(marker.props['aria-label']).toBe('Queued start')
  }
  backend.state.telemetry.pendingLatches=[{note:64,origin:48,action:'stop'}];await settle()
  expect(nodes(root,n=>n.props['data-queued-action']==='stop')).toHaveLength(2)
  expect(find(root,n=>n.props['data-queued-key']===48).props.class).toContain('latch-stop')
  backend.state.telemetry.pendingLatches=[];backend.state.telemetry.ppq=0;await settle()
  expect(nodes(root,n=>n.props['data-queued-key']!==undefined)).toHaveLength(0)
  expect(find(root,n=>n.props['data-drop']==='key:48').props.class).not.toContain('queued')
 })
 it('lights a parallel key and its origin while keeping the parallel key visually unbound and tracking the origin',async()=>{
  backend.state.trackKeywave=true
  backend.state.telemetry.fingers=[{channel:1,note:64,origin:48,zone:1,slide:.5,glide:.5,pressure:.5,playing:true}]
  backend.state.telemetry.contacts[1]=49
  const root=await render(Seaboard,{base:36}),origin=find(root,n=>n.props['data-drop']==='key:48'),parallel=find(root,n=>n.props['data-drop']==='key:64')
  expect(origin.props.class).toContain('held');expect(origin.props.class).toContain('parallel-origin')
  expect(parallel.props.class).toContain('held');expect(parallel.props['data-parallel']).toBe(true)
  expect(nodes(root,n=>n.props.class==='seaboard-key-label')).toHaveLength(16)
  await event(parallel,'keydown',{key:'Enter'});expect(backend.state.selected).toBe(48)
  expect(backend.midi).toHaveBeenLastCalledWith([0x92,64,100])
 })
 it('hit-tests below a black key as white-key input and scales its slide to the black key’s actual height',async()=>{
  const root=await render(Seaboard,{base:48}),sharp=find(root,n=>n.props['data-drop']==='key:49'),key=seaboardKeys(48).find(key=>key.note===49)!
  ;(sharp as any).ownerSVGElement={createSVGPoint:()=>({x:0,y:0,matrixTransform(){return {x:this.x,y:this.y}}}),getScreenCTM:()=>({inverse:()=>({})})}
  for(const [x,y,note,slide] of [[key.x+key.w/2,keyBed.top+key.h/2,49,64],[key.x+key.w*.25,keyBed.top+key.h+4,48,50],[key.x+key.w*.75,keyBed.top+key.h+4,50,50]]){
   backend.midi.mockClear();await event(sharp,'pointerdown',{button:0,pointerId:1,clientX:x,clientY:y,pressure:.5})
   expect(backend.midi.mock.calls.some(call=>call[0][0]===0xb1&&call[0][1]===74&&Math.abs(call[0][2]-slide)<=1)).toBe(true)
   expect(backend.midi).toHaveBeenLastCalledWith([0x91,note,100]);await event(sharp,'pointerup',{pointerId:1})
   expect(backend.midi).toHaveBeenLastCalledWith([0x81,note,64])
  }
 })
 it('targets individual chromatic ribbon lanes and visits every crossed white and black key during a sweep',async()=>{
  const root=await render(Seaboard,{base:48}),lanes=ribbonLanes(seaboardKeys(48))
  const top=find(root,n=>n.props['data-ribbon-note']===48&&n.props['data-ribbon-action']==='top')
  const svg={createSVGPoint:()=>({x:0,y:0,matrixTransform(){return {x:this.x,y:this.y}}}),getScreenCTM:()=>({inverse:()=>({})})}
  ;(top as any).ownerSVGElement=svg
  const x=(note:number)=>{const lane=lanes.find(key=>key.note===note)!;return lane.x+lane.w/2}
  await event(top,'pointerdown',{button:0,pointerId:7,clientX:x(48),clientY:168})
  expect(backend.request).toHaveBeenLastCalledWith('ribbon',{action:'top',note:48,channel:1,velocity:100})
  await event(top,'pointermove',{pointerId:7,clientX:x(52),clientY:168})
  expect(backend.request.mock.calls.filter(call=>call[0]==='ribbon').map(call=>call[1].note)).toEqual([48,49,50,51,52])
  await event(top,'pointermove',{pointerId:7,clientX:x(52),clientY:169})
  expect(backend.request.mock.calls.filter(call=>call[0]==='ribbon')).toHaveLength(5)
  await event(top,'pointermove',{pointerId:7,clientX:x(52),clientY:280})
  expect(backend.request).toHaveBeenLastCalledWith('ribbon',{action:'stop',note:52})
  backend.request.mockClear();await event(top,'pointermove',{pointerId:7,clientX:x(48),clientY:280})
  expect(backend.request.mock.calls.filter(call=>call[0]==='ribbon').map(call=>call[1])).toEqual([51,50,49,48].map(note=>({action:'stop',note})))
  await event(top,'pointerup',{pointerId:7})
  expect(nodes(root,n=>n.props['data-ribbon-action']==='top')).toHaveLength(49)
  expect(nodes(root,n=>n.props['data-ribbon-action']==='stop')).toHaveLength(49)
 })
 it('keeps the black-key stop lane separate from the intervening white-key body and supports per-key ribbon keyboard controls',async()=>{
  const root=await render(Seaboard,{base:48}),sharp=seaboardKeys(48).find(key=>key.note===49)!,x=sharp.x+sharp.w*.25
  const key=find(root,n=>n.props['data-drop']==='key:49'),svg={createSVGPoint:()=>({x:0,y:0,matrixTransform(){return {x:this.x,y:this.y}}}),getScreenCTM:()=>({inverse:()=>({})})};(key as any).ownerSVGElement=svg
  await event(key,'pointerdown',{button:0,pointerId:1,clientX:x,clientY:keyBed.top+sharp.h+5,pressure:.5})
  expect(backend.midi).toHaveBeenLastCalledWith([0x91,48,100])
  const lane=ribbonLanes(seaboardKeys(48)).find(key=>key.note===49)!
  await event(key,'pointermove',{pointerId:1,clientX:lane.x+lane.w/2,clientY:280})
  expect(backend.request).toHaveBeenLastCalledWith('ribbon',{action:'stop',note:49})
  await event(key,'pointerup',{pointerId:1})
  const stop=find(root,n=>n.props['data-ribbon-note']===49&&n.props['data-ribbon-action']==='stop')
  await event(stop,'keydown',{key:'Enter',repeat:false});expect(backend.request).toHaveBeenLastCalledWith('ribbon',{action:'stop',note:49})
  backend.request.mockClear();await event(stop,'keydown',{key:'Enter',repeat:true});expect(backend.request).not.toHaveBeenCalled()
  const top=find(root,n=>n.props['data-ribbon-note']===49&&n.props['data-ribbon-action']==='top')
  await event(top,'keydown',{key:'Enter',repeat:false});expect(backend.request).toHaveBeenLastCalledWith('ribbon',{action:'top',note:49,channel:1,velocity:100})
  await event(top,'keyup',{key:'Enter'});expect(backend.midi).toHaveBeenLastCalledWith([0x81,49,64])
 })
 it('preserves drum filters and mappings through a sound-only drag from a kit or sidebar',async()=>{
  const key=backend.state.patch.keys[0],parts=clone(key.parts),routes=clone(key.routes),zones=clone(key.zones)
  const asset=factoryAssets().find(asset=>asset.name==='Snare')!;let payload=''
  htmlDrag({dataTransfer:{setData:(_type:string,value:string)=>{payload=value}}} as any,asset,false,true)
  htmlDrop({preventDefault(){},dataTransfer:{getData:()=>payload}} as any,`key:${key.note}`);await settle()
  expect(key.sound.s.name).toBe('Snare');expect(key.parts).toEqual(parts);expect(key.routes).toEqual(routes);expect(key.zones).toEqual(zones)
 })
 it('exposes enabled-by-default latch quantization, instant bypass and bar counts',async()=>{
  const root=await render(Seaboard,{base:36})
  const toggle=find(root,n=>n.props['aria-label']==='Quantize latch and unlatch')
  expect(toggle.props['aria-pressed']).toBe(true)
  await event(toggle,'click');expect(backend.state.patch.latchQuantize).toBe(false)
  const bars=find(root,n=>n.props['aria-label']==='Latch quantization bars')
  bars.options.forEach(option=>{option.selected=option.value===4});await event(bars,'change');expect(backend.state.patch.latchBars).toBe(4)
  bars.options.forEach(option=>{option.selected=option.value===0});await event(bars,'change');expect(backend.state.patch.latchBars).toBe(0)
 })
 it('lights up the source keywave for scheduled pattern notes',async()=>{
  const root=await render(Seaboard,{base:36})
  acceptPerformance({performance:{epoch:4,beat:1,bpm:120,dropped:0,events:[{type:0,epoch:4,beat:1,duration:.01,key:48,note:36,velocity:1,owner:'played',token:'hit'}]}});await settle()
  expect(find(root,n=>n.props['data-playing-key']===48)).toBeDefined()
 })
 it('splits waveform and pattern preview, showing the actual filtered variation and continuously phased muted marker',async()=>{
  backend.state.ready=true
  const key=backend.state.patch.keys.find((k:any)=>k.note===48)
  const root=await render(SoundPanel,{sound:key.sound,note:48})
  expect(find(root,n=>n.props.class==='scope-pair')).toBeDefined()
  expect(backend.request).toHaveBeenCalledWith('monitorKey',48)
  const position={identity:'test:1:48:2',key:48,zone:1,candidate:2,phase:2,beat:6,playing:true}
  backend.state.telemetry.ppq=6
  acceptPerformance({pattern:{identity:position.identity,key:48,known:true,name:'Live variation',length:4,notes:[{beat:1,duration:.25,note:36,velocity:1}]},patternPosition:position})
  await settle()
  expect(root.textContent).toContain('PLAYINGLive variation')
  expect(find(root,n=>n.tag==='line'&&String(n.props.class).includes('pattern-transport')).props.x1).toBe(750)
  expect(root.textContent).toContain('↻ ×2')
  expect(find(root,n=>n.props.role==='img'&&n.props.title==='Repeats every 4 beats · showing 8 beats').props['aria-label']).toBe('2 drum notes, playing')
  backend.state.telemetry.ppq=6.5
  acceptPerformance({patternPosition:{...position,phase:2.5,beat:6.5,playing:false}});await settle()
  expect(root.textContent).toContain('LASTLive variation')
  expect(find(root,n=>n.tag==='line'&&String(n.props.class).includes('pattern-transport')).props.x1).toBe(812.5)
  backend.state.telemetry.ppq=7;await settle()
  expect(find(root,n=>n.tag==='line'&&String(n.props.class).includes('pattern-transport')).props.x1).toBe(875)
 })
 it('shows a filtered mini preview on each key zone and exposes the Track toggle',async()=>{
  const key=backend.state.patch.keys.find((k:any)=>k.note===48)
  key.parts=[36];const clip=backend.state.patch.clips.find((c:any)=>c.id===key.zones[0].clipId)
  clip.notes=[{beat:0,duration:.25,note:36,velocity:1},{beat:1,duration:.25,note:38,velocity:1}]
  const root=await render(ZoneEditor,{zones:key.zones,scope:'48'})
  const previews=nodes(root,n=>n.props.role==='img')
  expect(previews).toHaveLength(key.zones.length)
  expect(previews.some(n=>n.props['aria-label']==='1 drum notes')).toBe(true)
  const controls=await render(KeyControls,{binding:key}),track=find(controls,n=>n.props['aria-label']==='Track last touched keywave')
  expect(track.props['aria-pressed']).toBe(false)
  await event(track,'click');expect(backend.state.trackKeywave).toBe(true);expect(track.props['aria-pressed']).toBe(true)
 })
 it('draws a live keyboard and growing note history and clears only the display',async()=>{
  performanceView.epoch=1;performanceView.beat=2
  const root=await render(PianoRollPanel)
  acceptPerformance({performance:{epoch:1,beat:3,bpm:120,dropped:0,events:[{type:0,epoch:1,beat:2,duration:2,key:48,note:36,velocity:1,owner:'a',token:'t'}]}});await settle()
  expect(find(root,n=>n.props['aria-label']==='MIDI note keyboard')).toBeDefined()
  expect(nodes(root,n=>String(n.props.class).includes('played-notes'))).toHaveLength(1)
  expect(nodes(root,n=>n.tag==='clipPath')).toHaveLength(1)
  await event(find(root,n=>n.tag==='button'&&n.textContent==='Clear'),'click')
  expect(performanceView.notes).toHaveLength(0);expect(backend.midi).not.toHaveBeenCalled()
 })
})

describe('zone component interaction contracts', () => {
  it('resizes the selected boundary with pointer and keyboard input, then splits and removes zones', async () => {
    const zones = backend.state.patch.keys[0].zones
    const root = await render(ZoneEditor, { zones, scope: '48' })
    const boundary = find(root, n => n.props.role === 'separator')
    await event(boundary, 'pointerdown', { pointerId: 1 })
    await event(boundary, 'pointermove', { clientY: 120 })
    await event(boundary, 'pointerup')
    expect(zones[0].upper).toBeCloseTo(.4)
    await event(boundary, 'keydown', { key: 'ArrowUp', shiftKey: true })
    expect(zones[0].upper).toBeCloseTo(.45)
    await event(find(root, n => n.props.title === 'Split selected zone'), 'click')
    expect(zones).toHaveLength(4)
    expect(zones[0].upper).toBeCloseTo(.225)
    await event(find(root, n => n.props.title === 'Remove selected zone'), 'click')
    expect(zones).toHaveLength(3)
    expect(zones.at(-1).upper).toBe(1)
  })

  it('routes an HTML pattern drop to the actual displayed key zone', async () => {
    const key = backend.state.patch.keys[0]
    const root = await render(ZoneEditor, { zones: key.zones, scope: '48' })
    const clip = backend.state.patch.clips[2]
    const payload = JSON.stringify({ asset: { id: clip.id, path: clip.id }, patterns: true })
    await event(find(root, n => n.props['data-drop'] === 'zone:48:0'), 'drop', { dataTransfer: { getData: () => payload } })
    expect(key.zones[0].clipId).toBe(clip.id)
    expect(backend.request).not.toHaveBeenCalled()
  })
})

describe('Seaboard component interaction contracts', () => {
  it('changes the persisted keyboard mode and distinguishes toggled keys from pedal sustain',async()=>{
    const root=await render(Seaboard,{base:36})
    const mode=(label:string)=>find(root,n=>n.tag==='button'&&n.textContent===label)
    expect(mode('Normal').props['aria-pressed']).toBe(true)
    await event(mode('Hold'),'click');expect(backend.state.patch.keyboardMode).toBe('hold')
    expect(mode('Hold').props['aria-pressed']).toBe(true)
    await event(mode('Toggle'),'click');expect(backend.state.patch.keyboardMode).toBe('toggle')
    backend.state.telemetry.fingers=[{owner:'201',channel:1,note:48,zone:2,slide:.9,glide:.5,pressure:.5,sustained:true,toggled:true,playing:true},{owner:'202',channel:2,note:50,zone:0,slide:.1,glide:.5,pressure:.5,sustained:true,toggled:false,playing:true}]
    await settle()
    const kick=find(root,n=>n.props['data-drop']==='key:48'),hat=find(root,n=>n.props['data-drop']==='key:50')
    expect(kick.props.class).toContain('toggled');expect(kick.props['aria-pressed']).toBe(true)
    expect(nodes(kick,n=>n.props.class==='key-toggled')).toHaveLength(1)
    expect(hat.props['aria-pressed']).toBe(false);expect(nodes(hat,n=>n.props.class==='key-toggled')).toHaveLength(0)
    expect(find(root,n=>n.props['data-owner']==='201').props['aria-label']).toBe('Toggled note 48')
    backend.state.telemetry.fingers=[];await settle();expect(kick.props['aria-pressed']).toBe(false)
    await event(mode('Normal'),'click');expect(backend.state.patch.keyboardMode).toBe('normal')
    expect(backend.midi).not.toHaveBeenCalled()
  })

  it('connects the device preset arrows to patch navigation without sending note events', async () => {
    const select=vi.fn(),root=await render(Seaboard,{base:36,onPatch:select})
    await event(find(root,n=>n.props['aria-label']==='Previous patch'),'click')
    expect(select).toHaveBeenLastCalledWith(-1)
    await event(find(root,n=>n.props['aria-label']==='Next patch'),'keydown',{key:'Enter'})
    expect(select).toHaveBeenLastCalledWith(1)
    expect(backend.midi).not.toHaveBeenCalled()
  })

  it('offers direct pedal reassignment and pedal learn in the controller panel', async () => {
    const root=await render(Seaboard,{base:36})
    await event(find(root,n=>n.props['aria-label']==='Global MIDI bindings'),'click',{})
    expect(backend.state.tab).toBe('Settings');expect(backend.state.settingsSection).toBe('midi')
    const settings=await render(MidiSettings)
    const selector=find(settings,n=>n.props['aria-label']==='Sustain pedal CC')
    selector.props['onUpdate:modelValue'](66);await settle()
    expect(backend.state.patch.sustainCC).toBe(66)
    const learn=find(settings,n=>n.tag==='button'&&n.text==='Learn pedal')
    await event(learn,'click',{});expect(backend.state.learn).toBe(5)
  })

  it('renders 49 piano hit targets and avoids hardware member channels when auditioning', async () => {
    backend.state.telemetry.contacts[1] = 49
    const root = await render(Seaboard, { base: 36 })
    expect(nodes(root, n => n.props['data-drop']?.startsWith('key:'))).toHaveLength(49)
    const kick = find(root, n => n.props['data-drop'] === 'key:48')
    await event(kick, 'keydown', { key: 'Enter', repeat: false })
    expect(backend.midi.mock.calls.map(c => c[0])).toEqual([[0xe2, 0, 64], [0xd2, 100, 0], [0xb2, 74, 64], [0x92, 48, 100]])
    await event(kick, 'keydown', { key: 'Enter', repeat: true })
    expect(backend.midi).toHaveBeenCalledTimes(4)
    await event(kick, 'keyup', { key: 'Enter' })
    expect(backend.midi).toHaveBeenLastCalledWith([0x82, 48, 64])
  })

  it('fits the Seaboard and controls inside wide, tall and short parent areas on resize', async () => {
    let resized!:()=>void;const disconnect=vi.fn()
    vi.stubGlobal('ResizeObserver',class {constructor(callback:()=>void){resized=callback}observe(){}disconnect=disconnect})
    const root=await render(Seaboard,{base:36})
    const stage=find(root,n=>n.props.class==='seaboard-stage'),surface=find(root,n=>n.props.class==='seaboard')
    for(const [width,height] of [[2100,160],[480,240],[1100,90]]){
      stage.bounds.width=width;stage.bounds.height=height;resized();await settle()
      const w=parseFloat(surface.props.style.width),h=parseFloat(surface.props.style.height)
      expect(w).toBeLessThanOrEqual(width);expect(h).toBeLessThanOrEqual(height+.00001)
      expect(w/h).toBeCloseTo(705/210);expect(Math.abs(w-width)<.001||Math.abs(h-height)<.001).toBe(true)
    }
    cleanup.pop()!();expect(disconnect).toHaveBeenCalled()
  })

  it('moves physical finger markers while held and removes them on release', async () => {
    backend.state.telemetry.touches = [{id:'1',channel:1,note:48,pitch:48,slide:0,pressure:.1,surface:true}]
    const root = await render(Seaboard, {base: 36})
    const marker = find(root, n => n.props['data-finger'] === 1)
    const before = marker.props.transform
    const radius = nodes(marker, n => n.tag === 'circle')[1].props.r
    backend.state.telemetry.touches[0].slide = 1
    backend.state.telemetry.touches[0].pressure = 1
    await settle()
    expect(marker.props.transform).not.toBe(before)
    expect(nodes(marker, n => n.tag === 'circle')[1].props.r).toBeGreaterThan(radius)
    expect(nodes(root, n => n.props.class === 'key-selected')).toHaveLength(1)
    backend.state.telemetry.touches = []; await settle()
    expect(nodes(root, n => n.props['data-finger'] !== undefined)).toHaveLength(0)
  })

  it('leaves a command echo after release while a reused channel gets a new physical finger', async () => {
    const old={owner:'101',touch:'1',channel:1,note:48,zone:1,slide:.7,glide:.6,pressure:.8,sustained:false}
    backend.state.telemetry.fingers=[old]
    backend.state.telemetry.touches=[{id:'1',channel:1,note:48,pitch:48.2,slide:.7,pressure:.8,surface:true}]
    const root=await render(Seaboard,{base:36})
    const marker=find(root,n=>n.props['data-owner']==='101'),position=marker.props.transform
    backend.state.telemetry.fingers=[{...old,sustained:true},{owner:'102',touch:'2',channel:1,note:50,zone:0,slide:.1,glide:.5,pressure:.2,sustained:false}]
    backend.state.telemetry.touches=[{id:'2',channel:1,note:50,pitch:50,slide:.1,pressure:.2,surface:true}]
    await settle()
    expect(find(root,n=>n.props['data-owner']==='101')).toBe(marker)
    expect(marker.props.transform).toBe(position)
    expect(marker.props.class).toContain('sustained')
    expect(marker.props['aria-label']).toBe('Sustained note 48')
    expect(nodes(root,n=>n.props['data-finger']===1)).toHaveLength(1)
    const physical=find(root,n=>n.props['data-touch']==='touch:2')
    expect(physical.props.class).toContain('physical-finger')
    backend.state.telemetry.fingers=[backend.state.telemetry.fingers[1]];await settle()
    expect(nodes(root,n=>n.props['data-owner']==='101')).toHaveLength(0)
    expect(find(root,n=>n.props['data-touch']==='touch:2')).toBe(physical)
  })

  it('keeps all four nearby physical fingers visible when commands merge, toggle off or queue',async()=>{
    backend.state.telemetry.touches=[48,49,50,51].map((note,i)=>({id:String(i+1),channel:i+1,note,pitch:note,slide:.7,pressure:.4,surface:true}))
    backend.state.telemetry.fingers=[{owner:'101',touch:'4',channel:4,note:51,origin:48,zone:1,slide:.5,glide:.5,pressure:.4,playing:true}]
    const root=await render(Seaboard,{base:36})
    const fingers=nodes(root,n=>n.props['data-touch']!==undefined)
    expect(fingers).toHaveLength(4)
    expect(new Set(fingers.map(n=>n.props.transform)).size).toBe(4)
    expect(nodes(root,n=>n.props['data-emission']!==undefined)).toHaveLength(1)
    backend.state.patch.keyboardMode='toggle';backend.state.telemetry.fingers=[]
    backend.state.telemetry.pendingLatches=[{note:51,origin:48,action:'stop',touch:'4'}]
    await settle()
    for(const finger of fingers)expect(find(root,n=>n.props['data-touch']===finger.props['data-touch'])).toBe(finger)
    expect(nodes(root,n=>n.props['data-finger']!==undefined)).toHaveLength(4)
    backend.state.telemetry.pendingLatches=[];backend.state.telemetry.ppq=0;await settle()
    expect(nodes(root,n=>n.props['data-finger']!==undefined)).toHaveLength(4)
    expect(nodes(root,n=>n.props['data-echo']!==undefined)).toHaveLength(0)
  })

  it('shows an on-screen finger immediately and keeps its exact position through the stop ribbon',async()=>{
    const root=await render(Seaboard,{base:48}),key=find(root,n=>n.props['data-drop']==='key:48')
    ;(key as any).ownerSVGElement={createSVGPoint:()=>({x:0,y:0,matrixTransform(){return {x:this.x,y:this.y}}}),getScreenCTM:()=>({inverse:()=>({})})}
    await event(key,'pointerdown',{button:0,pointerId:7,clientX:155,clientY:250,pressure:.3})
    const finger=find(root,n=>n.props['data-touch']==='pointer:7')
    expect(finger.props.transform).toBe('translate(155 250)')
    backend.state.telemetry.touches=[{id:'1',channel:1,note:48,pitch:48,slide:.2,pressure:.3,surface:false}];await settle()
    expect(nodes(root,n=>n.props['data-finger']!==undefined)).toHaveLength(1)
    await event(key,'pointermove',{pointerId:7,clientX:157,clientY:280,pressure:.7})
    expect(find(root,n=>n.props['data-touch']==='pointer:7')).toBe(finger)
    expect(finger.props.transform).toBe('translate(157 280)')
    expect(backend.request).toHaveBeenLastCalledWith('ribbon',{action:'stop',note:48})
    // An old audio snapshot must not resurrect a released on-screen pointer.
    await settle()
    expect(nodes(root,n=>n.props['data-finger']!==undefined)).toHaveLength(1)
    await event(key,'pointerup',{pointerId:7})
    expect(nodes(root,n=>n.props['data-finger']!==undefined)).toHaveLength(0)
  })

  it('shows an unbound touch even when every member channel is occupied',async()=>{
    backend.state.patch.keys=[];backend.state.telemetry.contacts=Array.from({length:16},()=>1)
    const root=await render(Seaboard,{base:48}),key=find(root,n=>n.props['data-drop']==='key:48')
    ;(key as any).ownerSVGElement={createSVGPoint:()=>({x:0,y:0,matrixTransform(){return {x:this.x,y:this.y}}}),getScreenCTM:()=>({inverse:()=>({})})}
    await event(key,'pointerdown',{button:0,pointerId:9,clientX:155,clientY:250,pressure:.4})
    expect(find(root,n=>n.props['data-touch']==='pointer:9').props.transform).toBe('translate(155 250)')
    expect(backend.midi).not.toHaveBeenCalled()
    await event(key,'pointerup',{pointerId:9})
    expect(nodes(root,n=>n.props['data-finger']!==undefined)).toHaveLength(0)
  })

  it('releases only its own contact on unmount and sends XY keyboard controls to native parameters', async () => {
    const root = await render(Seaboard, { base: 36 })
    await event(find(root, n => n.props['data-drop'] === 'key:48'), 'keydown', { key: ' ' })
    await event(find(root, n => n.props['aria-label'] === 'Global XY pad. Use arrow keys to adjust.'), 'keydown', { key: 'ArrowRight', shiftKey: true })
    expect(backend.request).toHaveBeenLastCalledWith('parameter', { id: 'macro3', value: .1 })
    cleanup.pop()!()
    expect(backend.midi).toHaveBeenLastCalledWith([0x81, 48, 64])
    expect(backend.midi.mock.calls.some(c => c[0][1] === 123)).toBe(false)
  })
})

describe('MIDI chooser component contract', () => {
  const asset = { id: '/kit.mid', path: '/kit.mid', name: 'Kit MIDI', folder: false, size: 100 }
  const info = { channels: [2, 10], autoChannel: 10, ppq: 960, tracks: [{ index: 0, name: 'Drums', notes: 4, channels: [10] }, { index: 1, name: 'Percussion', notes: 4, channels: [2] }] }
  it('imports the chosen track with its valid channel and closes the dialog', async () => {
    const root = await render(MidiImport)
    const imported = clone(factory().clips[0])
    backend.request.mockImplementation(async (method: string) => method === 'inspectMidi' ? info : imported)
    const importing = importPattern(asset); await settle()
    const dialog = find(root, n => n.tag === 'dialog')
    expect(dialog.open).toBe(true)
    const track = nodes(root, n => n.tag === 'select')[0]
    track.options.forEach(o => { o.selected = o.value === 1 })
    await event(track, 'change')
    expect(midiImport.track).toBe(1); expect(midiImport.channel).toBe(2)
    await event(find(root, n => n.tag === 'button' && n.textContent === 'Use pattern'), 'click')
    expect(await importing).toEqual(imported)
    expect(backend.request).toHaveBeenLastCalledWith('importMidi', { path: '/kit.mid', track: 1, channel: 2 })
    expect(dialog.open).toBe(false)
  })

  it('cancels without importing a file when the dialog receives Escape/cancel', async () => {
    const root = await render(MidiImport)
    backend.request.mockResolvedValue(info)
    const importing = importPattern(asset); await settle()
    await event(find(root, n => n.tag === 'dialog'), 'cancel')
    expect(await importing).toBeNull()
    expect(backend.request).toHaveBeenCalledTimes(1)
  })
})

describe('missing-sound relinking', () => {
  it('replaces a missing path and clears stale cache metadata', async () => {
    const sound = backend.state.patch.keys[0].sound
    sound.missing = 'Missing sample'; sound.asset = 'old-cache-id'
    backend.request.mockImplementation(async (method: string) => method === 'relinkSound' ? '/new/kick.wav' : { peaks: [0, 1, 0], duration: .5 })
    const root = await render(SoundPanel, { sound })
    await event(find(root, n => n.tag === 'button' && n.textContent === 'Locate sound…'), 'click')
    expect(sound.path).toBe('/new/kick.wav')
    expect(sound.asset).toBeUndefined(); expect(sound.missing).toBeUndefined()
  })

  it('does not apply a late file-dialog result to a different selected key', async () => {
    let complete!: (path: string) => void
    backend.request.mockImplementation((method: string) => method === 'relinkSound' ? new Promise(resolve => { complete = resolve }) : Promise.resolve({ peaks: [], duration: .5 }))
    const original = backend.state.patch.keys[0].sound, other = backend.state.patch.keys[1].sound
    original.missing = 'Missing sample'
    const view = await mount(SoundPanel, { sound: original }); cleanup.push(view.unmount)
    await event(find(view.root, n => n.tag === 'button' && n.textContent === 'Locate sound…'), 'click')
    await view.update({ sound: other })
    complete('/late.wav'); await settle()
    expect(other.path).toBe('factory:hat'); expect(original.path).toBe('factory:kick')
    expect(backend.state.error).toContain('selected sound changed')
  })
})

it('shows selected-sound playback positions, including reverse movement', async () => {
  backend.state.telemetry.playheads = [{id: 1, note: 48, pos: .7, velocity: 1, grains: false}, {id: 2, note: 49, pos: .9, velocity: 1, grains: false}]
  const root = await render(Waveform, {peaks: [0,1,0], duration: 1, note: 48, clipIn: .1, clipOut: .9})
  const heads = nodes(root, n => n.props['data-voice'] !== undefined)
  expect(heads).toHaveLength(1)
  expect(find(heads[0], n => n.tag === 'line').props.x1).toBe(700)
  backend.state.telemetry.playheads[0].pos = .2; await settle()
  expect(find(heads[0], n => n.tag === 'line').props.x1).toBe(200)
})

describe('sampler audition controls',()=>{
 it('previews the current edited sound, toggles stop and leaves note input alone',async()=>{
  const sound=backend.state.patch.keys[0].sound;sound.s.pitch=-7;sound.s.delayMix=.22
  const root=await render(SoundPanel,{sound,note:48})
  await event(find(root,n=>n.props['aria-label']==='Preview sound'),'click');await settle()
  expect(backend.request).toHaveBeenLastCalledWith('preview',{kind:'sound',path:sound.path,sound:clone(sound)})
  expect(backend.midi).not.toHaveBeenCalled()
  await event(find(root,n=>n.props['aria-label']==='Stop sound preview'),'click');await settle()
  expect(backend.request).toHaveBeenLastCalledWith('stopPreview')
  expect(find(root,n=>n.props['aria-label']==='Preview sound')).toBeDefined()
 })
 it('stops audition when switching keys and opens all effect sections initially',async()=>{
  const sound=backend.state.patch.keys[0].sound
  const view=await mount(SoundPanel,{sound,note:48});cleanup.push(view.unmount)
  for(const name of ['EQ','DELAY','REVERB']){
   const module=nodes(view.root,n=>n.tag==='section').find(n=>n.textContent.trim().startsWith(name))!
   expect(module).toBeDefined();expect(module.props.class).not.toContain('folded')
  }
  await event(find(view.root,n=>n.props['aria-label']==='Preview sound'),'click');await settle()
  backend.request.mockClear()
  await view.update({sound:backend.state.patch.keys[1].sound,note:50});await settle()
  expect(backend.request).toHaveBeenCalledWith('stopPreview')
  expect(find(view.root,n=>n.props['aria-label']==='Preview sound')).toBeDefined()
 })
})


it('edits the new grain modes and exposes bind targets in the sampler',async()=>{
 const sound=backend.state.patch.keys[0].sound
 const root=await render(SoundPanel,{sound,note:48})
 for(const target of ['grainFreq','grainStereo','grainWindow','grainSpray','grainTuneRnd','grainFmAmount','grainScanTime'])expect(find(root,n=>n.props['data-param']===target)).toBeDefined()
 for(const label of ['STD','SYM','FLUX','FM','SCAN'])await event(find(root,n=>n.tag==='button'&&n.textContent.trim()===label),'click')
 expect(sound.s.grainSymmetry).toBe('fall');expect(sound.s.grainSpraySign).toBe('right')
 expect(sound.s.grainAmpMode).toBe('void');expect(sound.s.grainFm).toBe(true);expect(sound.s.grainScanOn).toBe(true)
 await event(find(root,n=>n.props['aria-label']==='Preview sound'),'click');await settle()
 expect(backend.request).toHaveBeenLastCalledWith('preview',{kind:'sound',path:sound.path,sound:clone(sound)})
})


describe('integrated expression controls',()=>{
 it('edits a curve by dragging, constrains it, supports keyboard and resets to linear',async()=>{
  const update=vi.fn();const root=await render(ExpressionCurve,{modelValue:1,'onUpdate:modelValue':update})
  const canvas=find(root,n=>n.tag==='canvas');canvas.bounds={left:0,top:0,width:80,height:46}
  await event(canvas,'pointerdown',{button:0,pointerId:1,clientX:40,clientY:32.5})
  expect(update).toHaveBeenLastCalledWith(2)
  await event(canvas,'pointermove',{clientX:40,clientY:42-38*Math.sqrt(.5)})
  expect(update.mock.calls.at(-1)![0]).toBeCloseTo(.5)
  await event(canvas,'pointermove',{clientX:40,clientY:-100});expect(update).toHaveBeenLastCalledWith(.1)
  await event(canvas,'pointerup',{});const calls=update.mock.calls.length
  await event(canvas,'pointermove',{clientX:40,clientY:42});expect(update).toHaveBeenCalledTimes(calls)
  await event(canvas,'keydown',{key:'End'});expect(update).toHaveBeenLastCalledWith(10)
  await event(canvas,'dblclick',{});expect(update).toHaveBeenLastCalledWith(1)
 })
 it('cycles surfaces, preserves routes while folded, and exposes range and smoothing dials',async()=>{
  const binding=backend.state.patch.keys[0];const root=await render(ExpressionPanel,{binding})
  expect(nodes(root,n=>n.tag==='knob-contract')).toHaveLength(binding.routes.length*3)
  const source=find(root,n=>n.props['aria-label']==='Expression source: Strike')
  await event(source,'click',{});expect(binding.routes[0].source).toBe(1)
  const target=find(root,n=>n.props['aria-label']==='Sound parameter')
  target.props['onUpdate:modelValue']('pan');await event(target,'change',{})
  expect(binding.routes[0].min).toBe(-1);expect(binding.routes[0].max).toBe(1)
  const fold=find(root,n=>n.tag==='button'&&n.textContent.includes('EXPRESSION'))
  const before=JSON.stringify(binding.routes);await event(fold,'click',{})
  expect(fold.props['aria-expanded']).toBe(false);expect(JSON.stringify(binding.routes)).toBe(before)
  await event(fold,'click',{});expect(fold.props['aria-expanded']).toBe(true)
  await event(find(root,n=>n.props['aria-label']==='Add expression mapping'),'click',{})
  expect(binding.routes).toHaveLength(6)
 })
})
