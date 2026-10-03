import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { clone, factory, type Patch } from './model'

const backend = vi.hoisted(() => ({ request: vi.fn(), telemetry: undefined as undefined | ((data: any) => void) }))
vi.mock('./native/bridge', () => ({ native: () => true, request: backend.request, onNative: (_: string, fn: (data: any) => void) => { backend.telemetry = fn } }))

beforeEach(() => { vi.useFakeTimers(); vi.resetModules(); backend.request.mockReset() })
afterEach(() => { vi.clearAllTimers(); vi.useRealTimers() })

async function setup() {
  backend.request.mockImplementation(async method => method === 'ready' ? { document: factory(), revision: 1, standalone: false } : [])
  const store = await import('./store')
  await store.init()
  return store
}

describe('editor document synchronization', () => {
  it('learns all five global controls from master-channel CCs and follows parameter telemetry', async () => {
    const store = await setup()
    for (let i = 0; i < 5; i++) {
      store.state.learn = i
      backend.telemetry!({ revision: 1, midi: [[0xb1, 74, 127], [0xb0, 74, 64]] })
      expect(store.state.learn).toBe(i)
      backend.telemetry!({ revision: 1, midi: [[0xb0, 20 + i, 64]] })
      expect(store.state.learn).toBe(-1)
      expect(store.state.patch.globalCC[i]).toBe(20 + i)
    }
    backend.telemetry!({ revision: 1, macros: [.1, .2, .3, .4, .5], output: .6, midi: [] })
    expect(store.state.telemetry.macros).toEqual([.1, .2, .3, .4, .5])
    expect(store.state.telemetry.output).toBe(.6)
  })

  it('keeps newer edits when an older sample import finishes', async () => {
    const store = await setup()
    let complete!: (result: unknown) => void
    const sent: { document: Patch; revision: number }[] = []
    backend.request.mockImplementation((method, payload) => {
      if (method !== 'setDocument') return Promise.resolve([])
      sent.push(clone(payload))
      if (sent.length === 1) return new Promise(resolve => { complete = resolve })
      return Promise.resolve({ document: payload.document, revision: 3, conflict: false })
    })
    store.state.patch.keys[0].sound.s.volume = 0.4
    const first = store.flushEdits()
    store.state.patch.keys[0].sound.s.pan = 0.7
    const second = store.flushEdits()
    const imported = clone(sent[0].document)
    imported.keys[0].sound.asset = 'content.wav'
    complete({ document: imported, revision: 2, conflict: false })
    await Promise.all([first, second])
    expect(sent.map(s => s.revision)).toEqual([1, 2])
    expect(sent[1].document.keys[0].sound.asset).toBe('content.wav')
    expect(store.state.patch.keys[0].sound.s.pan).toBe(0.7)
    expect(store.state.patch.keys[0].sound.s.volume).toBe(0.4)
    store.undo()
    await store.flushEdits(false)
    expect(store.state.patch.keys[0].sound.s.pan).toBe(0)
    expect(store.state.patch.keys[0].sound.s.volume).toBe(0.4)
  })

  it('does not overwrite a host recall with queued editor changes', async () => {
    const store = await setup()
    const recalled = factory(); recalled.name = 'Host recall'
    backend.request.mockResolvedValue({ document: recalled, revision: 9, conflict: true })
    store.state.patch.name = 'Stale editor'
    await store.flushEdits()
    expect(store.state.patch.name).toBe('Host recall')
    expect(store.state.history).toEqual([])
    backend.request.mockImplementation(async (_method, payload) => ({ document: payload.document, revision: 10, conflict: false }))
    store.state.patch.name = 'New edit'
    await store.flushEdits()
    expect(backend.request.mock.calls.at(-1)?.[1].revision).toBe(9)
  })

  it('cancels a queued change when the user returns to the in-flight state', async () => {
    const store = await setup()
    let complete!: (result: unknown) => void
    backend.request.mockImplementation(() => new Promise(resolve => { complete = resolve }))
    store.state.patch.name = 'A'
    const saving = store.flushEdits()
    const first = clone(store.state.patch)
    store.state.patch.name = 'B'; void store.flushEdits()
    store.undo()
    complete({ document: first, revision: 2, conflict: false })
    await saving
    expect(store.state.patch.name).toBe('A')
    expect(backend.request.mock.calls.filter(call => call[0] === 'setDocument')).toHaveLength(1)
  })
})
