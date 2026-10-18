import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { clone, factory, type Patch } from './model'

const backend = vi.hoisted(() => ({ request: vi.fn(), telemetry: undefined as undefined | ((data: any) => void) }))
vi.mock('./native/bridge', () => ({ native: () => true, request: backend.request, onNative: (_: string, fn: (data: any) => void) => { backend.telemetry = fn } }))

beforeEach(() => { vi.useFakeTimers(); vi.resetModules(); backend.request.mockReset() })
afterEach(() => { vi.clearAllTimers(); vi.useRealTimers(); vi.unstubAllGlobals() })

async function setup() {
  backend.request.mockImplementation(async method => method === 'ready' ? { document: factory(), revision: 1, standalone: false } : [])
  const store = await import('./store')
  await store.init()
  return store
}

describe('editor document synchronization', () => {
  it('queues a resolved odd-meter patch for a bar and preserves it through ordinary edits',async()=>{
    const store=await setup(),patch=clone(factory());patch.name='Seven';patch.neural={version:1,numerator:7,denominator:8} as any
    backend.request.mockImplementation(async(_method,payload)=>({document:payload.document,revision:2,conflict:false,activationPending:true}))
    await store.replace(patch,'bar')
    expect(backend.request).toHaveBeenLastCalledWith('setDocument',expect.objectContaining({activationGrid:3.5,preserveActivation:false}))
    expect(store.state.telemetry.activationPending).toBe(true)
    store.state.patch.volume=.6;await store.flushEdits()
    expect(backend.request).toHaveBeenLastCalledWith('setDocument',expect.objectContaining({activationGrid:0,preserveActivation:true}))
    backend.telemetry!({activationPending:false,revision:2})
    expect(store.state.telemetry.activationPending).toBe(false)
  })

  it('retains a queued bar policy when knob edits coalesce behind an older request',async()=>{
    const store=await setup();let finish!:(value:any)=>void;let first:any
    backend.request.mockImplementation(async(_method,payload)=>{
      if(!first){first=clone(payload);return new Promise(resolve=>{finish=resolve})}
      return {document:payload.document,revision:3,conflict:false,activationPending:true}
    })
    store.state.patch.name='Older edit';const saving=store.flushEdits()
    const patch=clone(factory());patch.name='Resolved family';patch.neural={version:1,numerator:4,denominator:4} as any
    const resolving=store.replace(patch,'bar');store.state.patch.volume=.4;const editing=store.flushEdits()
    finish({document:first.document,revision:2,conflict:false,activationPending:false});await Promise.all([saving,resolving,editing])
    expect(backend.request.mock.calls.filter(call=>call[0]==='setDocument')).toHaveLength(2)
    expect(backend.request).toHaveBeenLastCalledWith('setDocument',expect.objectContaining({revision:2,activationGrid:4,preserveActivation:false,document:expect.objectContaining({name:'Resolved family',volume:.4})}))
  })

  it('allows an explicit load of the same document to cancel a prepared refresh',async()=>{
    const store=await setup(),patch=clone(factory());patch.name='Waiting'
    backend.request.mockImplementation(async(_method,payload)=>({document:payload.document,revision:2,conflict:false,activationPending:payload.activationGrid>0}))
    await store.replace(patch,'bar');await store.replace(clone(store.state.patch))
    expect(backend.request.mock.calls.filter(call=>call[0]==='setDocument')).toHaveLength(2)
    expect(backend.request).toHaveBeenLastCalledWith('setDocument',expect.objectContaining({activationGrid:0,preserveActivation:false}))
    expect(store.state.telemetry.activationPending).toBe(false)
  })

  it('undoes an in-flight neural resolution with an immediate replacement',async()=>{
    const store=await setup(),patch=clone(factory());patch.name='Resolved';let finish!:(value:any)=>void;let first:any
    backend.request.mockImplementation(async(_method,payload)=>{
      if(!first){first=clone(payload);return new Promise(resolve=>{finish=resolve})}
      return {document:payload.document,revision:3,conflict:false,activationPending:false}
    })
    const resolving=store.replace(patch,'bar');store.undo()
    finish({document:first.document,revision:2,conflict:false,activationPending:true});await resolving
    expect(store.state.patch.name).toBe(factory().name)
    expect(backend.request).toHaveBeenLastCalledWith('setDocument',expect.objectContaining({activationGrid:0,preserveActivation:false}))
    expect(store.state.telemetry.activationPending).toBe(false)
  })

  it('cancels a boundary inherited by an in-flight ordinary edit when that same patch is explicitly loaded',async()=>{
    const store=await setup(),patch=clone(factory());patch.name='Waiting'
    backend.request.mockImplementation(async(_method,payload)=>({document:payload.document,revision:2,conflict:false,activationPending:true}))
    await store.replace(patch,'bar')
    let finish!:(value:any)=>void;let first:any
    backend.request.mockImplementation(async(_method,payload)=>{
      if(!first){first=clone(payload);return new Promise(resolve=>{finish=resolve})}
      return {document:payload.document,revision:4,conflict:false,activationPending:false}
    })
    store.state.patch.volume=.4;const editing=store.flushEdits(),loading=store.replace(clone(store.state.patch))
    finish({document:first.document,revision:3,conflict:false,activationPending:true});await Promise.all([editing,loading])
    expect(backend.request).toHaveBeenLastCalledWith('setDocument',expect.objectContaining({revision:3,activationGrid:0,preserveActivation:false}))
    expect(store.state.telemetry.activationPending).toBe(false)
  })

  it('keeps an old session linked to its saved identity when it is renamed before the first bank poll',async()=>{
    const legacy=clone(factory());delete legacy.id;legacy.name='Legacy original'
    const saved={...clone(legacy),id:'migrated-legacy'}
    backend.request.mockImplementation(async(method,payload)=>{
      if(method==='ready')return {document:legacy,revision:1,standalone:false}
      if(method==='sharedSnapshot')return [{key:'patch/Legacy original',value:saved,revision:4},{key:'midi/program-bank',value:{version:1,slots:['builtin:factory','migrated-legacy']},revision:4}]
      if(method==='sharedCommit')return {ok:true,revision:5,fields:[{key:payload[0].key,value:payload[0].value,revision:5}]}
      return []
    })
    const store=await import('./store');await store.init();store.state.patch.name='Renamed legacy'
    expect(await store.saveSharedPatch()).toMatchObject({ok:true,program:1})
    expect(backend.request).toHaveBeenLastCalledWith('sharedCommit',[{key:'patch/Legacy original',value:expect.objectContaining({id:'migrated-legacy',name:'Renamed legacy'}),revision:4}])
  })
  it('adopts canonical saved identities and companion program metadata in one acknowledgement',async()=>{
    const store=await setup(),legacy=clone(factory());delete legacy.id
    const canonical={...legacy,id:'server-assigned'},slots=['builtin:factory','server-assigned']
    backend.request.mockResolvedValue({ok:true,revision:7,fields:[{key:'patch/Legacy',value:canonical,revision:7},{key:'midi/program-bank',value:{version:1,slots},revision:7}]})
    const result=await store.writeShared('patch/Legacy',legacy,0)
    expect(result.field.value.id).toBe('server-assigned')
    expect(store.shared['patch/Legacy'].value.id).toBe('server-assigned')
    expect(store.shared['midi/program-bank'].value.slots).toEqual(slots)
  })

  it('renames the same saved identity and gives an explicit copy a new identity and slot',async()=>{
    const store=await setup(),original={...clone(factory()),id:'stable-kit',name:'Original name'}
    store.shared['patch/Original name']={key:'patch/Original name',value:clone(original),revision:4}
    store.shared['midi/program-bank']={key:'midi/program-bank',value:{version:1,slots:['builtin:factory','stable-kit']},revision:4}
    store.state.patch=clone(original);store.state.patch.name='Renamed / instrument'
    backend.request.mockImplementation(async(method,ops)=>{
      if(method!=='sharedCommit')return []
      const op=ops[0],slots=['builtin:factory','stable-kit',...(op.value.id==='stable-kit'?[]:[op.value.id])]
      return {ok:true,revision:5,fields:[{key:op.key,value:clone(op.value),revision:5},{key:'midi/program-bank',value:{version:1,slots},revision:5}]}
    })
    expect(await store.saveSharedPatch()).toMatchObject({ok:true,program:1})
    expect(backend.request).toHaveBeenLastCalledWith('sharedCommit',[{key:'patch/Original name',value:expect.objectContaining({id:'stable-kit',name:'Renamed / instrument'}),revision:4}])
    vi.stubGlobal('crypto',{randomUUID:()=> '00000000-0000-4000-8000-000000000001'})
    expect(await store.saveSharedPatch(true)).toMatchObject({ok:true,program:2})
    expect(store.state.patch.id).toBe('patch-00000000-0000-4000-8000-000000000001')
    expect(store.state.patch.name).toBe('Renamed / instrument copy')
    expect(store.shared['patch/Original name'].value.id).toBe('stable-kit')
    expect(store.shared['patch/Original name'].value.name).toBe('Renamed / instrument')
  })

  it('does not replace a host-recalled document when saving a copy finishes late',async()=>{
    const store=await setup();let finish!:(value:any)=>void
    vi.stubGlobal('crypto',{randomUUID:()=> '00000000-0000-4000-8000-000000000001'})
    backend.request.mockImplementation(()=>new Promise(resolve=>{finish=resolve}))
    const saving=store.saveSharedPatch(true),restored=clone(factory());restored.name='Host recall';store.state.patch=restored
    const op=backend.request.mock.calls.find(call=>call[0]==='sharedCommit')![1][0]
    finish({ok:true,revision:3,fields:[{key:op.key,value:op.value,revision:3}]});await saving
    expect(store.state.patch.name).toBe('Host recall');expect(store.state.patch.id).toBe('builtin:factory')
  })

  it('learns a pedal separately, reserves slide/channel modes, and keeps it out of global learn', async () => {
    const store=await setup();store.state.learn=5
    backend.telemetry!({revision:1,midi:[[0xb0,74,127],[0xb0,123,0]]})
    expect(store.state.learn).toBe(5);expect(store.state.patch.sustainCC).toBe(64)
    backend.telemetry!({revision:1,midi:[[0xb2,66,127]]})
    expect(store.state.learn).toBe(-1);expect(store.state.patch.sustainCC).toBe(66)
    store.state.learn=0;backend.telemetry!({revision:1,midi:[[0xb0,66,0]]})
    expect(store.state.learn).toBe(0);expect(store.state.patch.globalCC[0]).toBe(107)
  })

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
