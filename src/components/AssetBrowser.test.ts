import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { factory } from '../model'
import { event, find, mount, settle } from '../testing/componentHost'
const fake = vi.hoisted(() => ({ state: null as any, shared: null as any, request: vi.fn(), write: vi.fn(), graphs: [] as any[] }))
vi.mock('../native/bridge', () => ({ native: () => true, request: fake.request }))
vi.mock('../store', async () => {
  const { reactive } = await import('vue')
  fake.state = reactive({ patch: {}, error: '' }); fake.shared = reactive({})
  return {
    state: fake.state, shared: fake.shared, refreshShared: async () => {}, writeShared: fake.write,
    attempt: async (job: () => Promise<unknown>) => { try { return await job() } catch (error) { fake.state.error = String(error) } },
  }
})
vi.mock('./DataGrid.vue', async () => {
  const { defineComponent, h } = await import('vue')
  return { default: defineComponent({ props: ['rows', 'viewState'], setup: props => () => h('grid-contract', { rows: props.rows, viewState: props.viewState }) }) }
})
vi.mock('../canvas/treeGraph.js', () => ({ TreeGraph: class {
  source: any; look = {}; drawn = []; layout: any; destroyed = false
  constructor(_box: unknown, public callbacks: any) { fake.graphs.push(this) }
  setTree(tree: unknown, layout: unknown) { this.source = tree; this.layout = layout }
  setMarked() {}
  arrangement() { return this.layout }
  destroy() { this.destroyed = true }
} }))
import AssetBrowser from './AssetBrowser.vue'
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
const directory = (root: any) => find(root, n => n.props['aria-label'] === 'Library directory')

describe('browser component contracts', () => {
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
