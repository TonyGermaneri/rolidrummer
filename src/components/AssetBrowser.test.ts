import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { factory } from '../model'
import {toRaw} from 'vue'
import { event, find, mount, settle } from '../testing/componentHost'
const fake = vi.hoisted(() => ({ state: null as any, shared: null as any, request: vi.fn(), write: vi.fn(), graphs: [] as any[] }))
vi.mock('../native/bridge', () => ({ native: () => true, request: fake.request }))
vi.mock('../store', async () => {
  const { reactive } = await import('vue')
  fake.state = reactive({ patch: {}, error: '' }); fake.shared = reactive({})
  return {
    state: fake.state, shared: fake.shared, refreshShared: async () => {}, writeShared: fake.write,
    soundPreview: reactive({kit:null,ready:false}),
    attempt: async (job: () => Promise<unknown>) => { try { return await job() } catch (error) { fake.state.error = String(error) } },
  }
})
vi.mock('./DataGrid.vue', async () => {
  const { defineComponent, h } = await import('vue')
  return { default: defineComponent({ props: ['rows', 'viewState'], setup: props => () => h('grid-contract', { rows: props.rows, viewState: props.viewState }) }) }
})
vi.mock('../canvas/treeGraph.js', () => ({ TreeGraph: class {
  source: any; look = {}; drawn = []; layout: any; destroyed = false
  constructor(public box: unknown, public callbacks: any) { fake.graphs.push(this) }
  setTree(tree: unknown, layout: unknown) { this.source = tree; this.layout = layout }
  setMarked() {}
  arrangement() { return this.layout }
  destroy() { this.destroyed = true }
} }))
import AssetBrowser from './AssetBrowser.vue'
import {factoryAssets} from '../model'
let cleanup: (() => void)[] = []
beforeEach(() => {
  vi.useFakeTimers(); vi.stubGlobal('document', { activeElement: null })
  vi.stubGlobal('Document', class {}); vi.stubGlobal('ShadowRoot', class {})
  vi.stubGlobal('requestAnimationFrame', () => 1); vi.stubGlobal('cancelAnimationFrame', () => {})
  fake.state.patch = factory(); fake.state.error = ''; fake.graphs.length = 0
  for (const key of Object.keys(fake.shared)) delete fake.shared[key]
  fake.request.mockReset().mockResolvedValue([])
  fake.write.mockReset().mockImplementation(async (key, value, revision) => {
    const current = fake.shared[key]
    if (revision !== (current?.revision ?? 0)) return { ok: false, field: current }
    const field = { key, value, revision: revision + 1 }; fake.shared[key] = field
    return { ok: true, field }
  })
})
afterEach(() => { cleanup.forEach(fn => fn()); cleanup = []; vi.clearAllTimers(); vi.useRealTimers(); vi.unstubAllGlobals() })
async function render(patterns = false) { const view = await mount(AssetBrowser, { patterns }); cleanup.push(view.unmount); return view }
it('assigns a selected browser sound directly to the sidebar target keywave',async()=>{
 const view=await mount(AssetBrowser,{assignTo:50,soundOnly:true});cleanup.push(view.unmount)
 const asset=factoryAssets().find(asset=>asset.name==='Snare')!
 const grid=find(view.root,n=>n.tag==='grid-contract')
 grid.props.onPick(asset);await settle()
 await event(find(view.root,n=>n.tag==='button'&&n.textContent.includes('ASSIGN TO')),'click')
 expect(fake.state.patch.keys.find((key:any)=>key.note===50).sound.s.name).toBe('Snare')
 expect(fake.state.selected).toBe(50)
 expect(fake.state.patch.keys.find((key:any)=>key.note===50).parts).toEqual([38,40])
})
const directory = (root: any) => find(root, n => n.props['aria-label'] === 'Library directory')

describe('browser component contracts', () => {
  it('creates a graph only after its conditional container mounts and recreates it after grid/graph switches',async()=>{
    const view=await render()
    expect(fake.graphs).toHaveLength(0)
    const button=(label:string)=>find(view.root,n=>n.tag==='button'&&n.textContent===label)
    await event(button('Graph'),'click')
    expect(fake.graphs).toHaveLength(1);const first=fake.graphs[0]
    expect(toRaw(first.box)).toBe(find(view.root,n=>n.props.class==='graph-box'))
    expect(first.source.nodes.filter((n:any)=>n.leaf).length).toBeGreaterThan(0)
    await event(button('Grid'),'click');expect(first.destroyed).toBe(true)
    await event(button('Graph'),'click');expect(fake.graphs).toHaveLength(2)
    expect(toRaw(fake.graphs[1].box)).toBe(find(view.root,n=>n.props.class==='graph-box'))
  })
  it('browses patch rows, filters and loads by stable ID without audio or file requests',async()=>{
    const patches=[{id:'patch/A',path:'patch/A',name:'Alpha',folder:false,size:0,keyCount:16,patternCount:3},{id:'patch/B',path:'patch/B',name:'Beta',folder:false,size:0,keyCount:4,patternCount:2}]
    const load=vi.fn(),view=await mount(AssetBrowser,{patches,currentId:'patch/A',onLoadPatch:load});cleanup.push(view.unmount)
    const grid=find(view.root,n=>n.tag==='grid-contract')
    expect(grid.props.rows).toHaveLength(2);expect(grid.props.rows[0].load).toBe('●')
    grid.props.onUse(patches[1]);await settle();expect(load).toHaveBeenLastCalledWith('patch/B')
    const filter=find(view.root,n=>n.props['aria-label']==='Filter patches');filter.props['onUpdate:modelValue']('beta');await settle()
    expect(grid.props.rows.map((r:any)=>r.id)).toEqual(['patch/B'])
    await vi.advanceTimersByTimeAsync(200);expect(fake.shared['browser/patches/navigation'].value.query).toBe('beta')
    expect(fake.request).not.toHaveBeenCalled()
    await view.update({patches,disabled:true,onLoadPatch:load});load.mockClear();grid.props.onUse(patches[0]);await settle();expect(load).not.toHaveBeenCalled()
  })

  it('updates the patch library without losing a graph gesture or accepting stale layout writes',async()=>{
    const a={id:'patch/A/B',path:'patch/A/B',name:'A/B',folder:false,size:0,keyCount:16,patternCount:3}
    const b={...a,id:'patch/C',path:'patch/C',name:'C'}
    fake.shared['browser/patches/navigation']={revision:1,value:{path:'',query:'',view:'graph'}}
    const key='graph/patches//layout'
    fake.shared[key]={revision:1,value:{camera:{x:0,y:0,k:1},open:['Patches'],places:{}}}
    const view=await mount(AssetBrowser,{patches:[a]});cleanup.push(view.unmount)
    let graph=fake.graphs.at(-1)
    expect(graph.source.nodes.filter((n:any)=>n.browserId).map((n:any)=>n.label)).toEqual(['A/B'])
    await event(find(view.root,n=>n.props.class==='graph-box'),'pointerdownCapture')
    graph.layout={camera:{x:10,y:20,k:2},open:[0],places:[[1,11,22]]}
    fake.shared[key]={revision:2,value:{camera:{x:100,y:200,k:1},open:['Patches'],places:{}}};await settle()
    await view.update({patches:[a,b]});graph=fake.graphs.at(-1)
    expect(graph.layout.camera.x).toBe(10)
    graph.callbacks.onArrange();await settle()
    expect(graph.layout.camera.x).toBe(100)
    expect(fake.shared[key].revision).toBe(2)
    const writes=fake.write.mock.calls.length;view.unmount();cleanup.pop();expect(fake.write).toHaveBeenCalledTimes(writes)
  })

  it('keeps typing separate from the loaded directory and accepts remote updates after a failed read', async () => {
    fake.shared['browser/sounds/navigation'] = { revision: 1, value: { path: '', query: '', view: 'grid' } }
    const { root } = await render()
    const input = directory(root)
    input.props['onUpdate:modelValue']('/missing'); await settle()
    expect(fake.request).not.toHaveBeenCalled()
    expect(fake.write).not.toHaveBeenCalled()
    fake.request.mockRejectedValueOnce(Error('Folder does not exist'))
    await event(input, 'keydown', { key: 'Enter' })
    expect(fake.state.error).toContain('Folder does not exist')
    fake.request.mockResolvedValue([])
    fake.shared['browser/sounds/navigation'] = { revision: 2, value: { path: '/remote', query: '', view: 'grid' } }
    await settle()
    expect(fake.request).toHaveBeenLastCalledWith('listDirectory', { path: '/remote', patterns: false })
    expect(directory(root).value).toBe('/remote')
  })

  it('ignores a sound-folder dialog result after switching to the pattern tab', async () => {
    let choose!: (value: string) => void
    fake.request.mockImplementation((method: string) => method === 'chooseFolder' ? new Promise(resolve => { choose = resolve }) : Promise.resolve([]))
    const view = await render()
    await event(find(view.root, n => n.tag === 'button' && n.textContent.includes('Folder')), 'click')
    await view.update({ patterns: true })
    choose('/old-sounds'); await settle()
    expect(fake.request.mock.calls.some(c => c[0] === 'listDirectory')).toBe(false)
    expect(directory(view.root).value).toBe('')
  })

  it('ignores an older directory response that finishes after the latest navigation', async () => {
    let old!: (value: unknown[]) => void
    fake.request.mockImplementation((_method: string, payload: any) => payload.path === '/old' ? new Promise(resolve => { old = resolve }) : Promise.resolve([]))
    const { root } = await render()
    const input = directory(root)
    input.props['onUpdate:modelValue']('/old'); await event(input, 'keydown', { key: 'Enter' })
    input.props['onUpdate:modelValue']('/new'); await event(input, 'keydown', { key: 'Enter' })
    old([]); await settle(); await vi.advanceTimersByTimeAsync(200)
    expect(directory(root).value).toBe('/new')
    expect(fake.shared['browser/sounds/navigation'].value.path).toBe('/new')
  })

  it('communicates graph camera and positions between two mounted editors and rejects stale gestures without unload writes', async () => {
    fake.shared['browser/sounds/navigation'] = { revision: 1, value: { path: '', query: '', view: 'graph' } }
    const key = 'graph/sounds//layout'
    fake.shared[key] = { revision: 1, value: { camera: { x: 0, y: 0, k: 1 }, open: ['Library'], places: {} } }
    const first = await render(), second = await render()
    const [a, b] = fake.graphs
    expect(fake.graphs).toHaveLength(2)
    await event(find(second.root, n => n.props.class === 'graph-box'), 'pointerdownCapture')
    await event(find(first.root, n => n.props.class === 'graph-box'), 'pointerdownCapture')
    a.layout = { camera: { x: 20, y: 40, k: 2 }, open: [0], places: [[1, 80, 90]] }
    a.callbacks.onArrange(); await settle()
    expect(fake.shared[key].value.places['Library/Clap']).toEqual([80, 90])
    // Second editor is mid-gesture, so its displayed camera stays put.
    expect(b.layout.camera.x).toBe(0)
    b.layout = { camera: { x: 999, y: 0, k: 1 }, open: [0], places: [] }
    b.callbacks.onArrange(); await settle()
    expect(b.layout.camera).toEqual(a.layout.camera)
    expect(fake.shared[key].value.camera.x).toBe(20)
    const writes = fake.write.mock.calls.length
    cleanup.forEach(fn => fn()); cleanup = []
    expect(fake.write).toHaveBeenCalledTimes(writes)
    expect(a.destroyed && b.destroyed).toBe(true)
  })
})
