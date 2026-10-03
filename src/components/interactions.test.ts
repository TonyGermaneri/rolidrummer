// @vitest-environment ./src/testing/environment.ts
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { factory, clone } from '../model'
import { event, find, mount, nodes, settle } from '../testing/componentHost'

const backend = vi.hoisted(() => ({ request: vi.fn(), midi: vi.fn(), state: null as any }))
vi.mock('../native/bridge', () => ({ request: backend.request, native: () => true }))
vi.mock('../store', async () => {
  const { reactive } = await import('vue')
  backend.state = reactive({ patch: {}, selected: 48, learn: -1, error: '', telemetry: { macros: [0, 0, 0, 0, 0], contacts: [] } })
  return { state: backend.state, sendMidi: backend.midi, attempt: async (job: () => unknown) => { try { return await job() } catch (error) { backend.state.error = String(error) } } }
})
vi.mock('./Knob.vue', async () => {
  const { defineComponent, h } = await import('vue')
  return { default: defineComponent({ setup: () => () => h('knob-contract') }) }
})
import ZoneEditor from './ZoneEditor.vue'
import Seaboard from './Seaboard.vue'
import MidiImport from './MidiImport.vue'
import SoundPanel from './SoundPanel.vue'
import Waveform from './Waveform.vue'
import { importPattern, midiImport, finishMidiImport } from '../midiImport'

let cleanup: (() => void)[] = []
beforeEach(() => {
  backend.state.patch = factory(); backend.state.selected = 48; backend.state.error = ''
  backend.state.telemetry.contacts = []; backend.state.telemetry.macros = [0, 0, 0, 0, 0]
  backend.request.mockReset().mockImplementation(async (method:string)=>method==='waveform'?{peaks:[],duration:.5}:true); backend.midi.mockReset()
  vi.stubGlobal('window', { addEventListener: vi.fn(), removeEventListener: vi.fn() })
  vi.stubGlobal('document', { activeElement: null })
  vi.stubGlobal('Document', class {}); vi.stubGlobal('ShadowRoot', class {})
})
afterEach(() => { cleanup.forEach(fn => fn()); cleanup = []; finishMidiImport(false); vi.unstubAllGlobals() })
async function render(component: any, props = {}) { const view = await mount(component, props); cleanup.push(view.unmount); return view.root }

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
    backend.state.telemetry.fingers = [{channel: 1, note: 48, slide: 0, glide: .5, pressure: .1}]
    const root = await render(Seaboard, {base: 36})
    const marker = find(root, n => n.props['data-finger'] === 1)
    const before = marker.props.transform
    const radius = nodes(marker, n => n.tag === 'circle')[1].props.r
    backend.state.telemetry.fingers[0].slide = 1
    backend.state.telemetry.fingers[0].pressure = 1
    await settle()
    expect(marker.props.transform).not.toBe(before)
    expect(nodes(marker, n => n.tag === 'circle')[1].props.r).toBeGreaterThan(radius)
    expect(nodes(root, n => n.props.class === 'key-selected')).toHaveLength(1)
    backend.state.telemetry.fingers = []; await settle()
    expect(nodes(root, n => n.props['data-finger'] !== undefined)).toHaveLength(0)
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
