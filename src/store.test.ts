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
  it('tracks physical parallel-key presses to their editable origins without changing the binding list',async()=>{
    const {state,sendMidi}=await setup(),notes=state.patch.keys.map(key=>key.note)
    state.selected=50;state.trackKeywave=false;sendMidi([0x91,64,100]);expect(state.selected).toBe(50);expect(state.lastTouchedKey).toBe(48)
    state.trackKeywave=true;expect(state.selected).toBe(48)
    backend.telemetry!({midi:[[0x92,47,100]]});expect(state.selected).toBe(63)
    backend.telemetry!({midi:[[0x82,47,100]]});expect(state.selected).toBe(63)
    expect(state.patch.keys.map(key=>key.note)).toEqual(notes)
  })
  function soundBackend(){
    let revision=1
    backend.request.mockImplementation(async(method,payload)=>{
      if(method==='soundMatch')return{assignments:[{note:48,asset:{path:'factory:kick',name:'Preview kick'}}],sounds:16,limited:false}
      if(method==='previewKit'||method==='cancelKitPreview')return true
      if(method==='setDocument')return{document:payload.document,revision:++revision,conflict:false}
      if(method==='sharedCommit')return{ok:true,fields:payload.map((operation:any)=>({...operation,revision:++revision}))}
      return[]
    })
  }
  it('automatically auditions semantic bindings without editing the patch, history, redo or saved patches',async()=>{
    const store=await setup(),kits=await import('./kits');soundBackend()
    const before=clone(store.state.patch);store.state.future=['existing redo']
    await kits.matchSounds('Rock drums')
    expect(store.soundPreview.ready).toBe(true)
    expect(store.soundPreview.kit?.keys[0].sound.s.name).toBe('Preview kick')
    expect(store.state.patch).toEqual(before);expect(store.state.history).toEqual([]);expect(store.state.future).toEqual(['existing redo'])
    expect(backend.request).toHaveBeenCalledWith('previewKit',expect.objectContaining({revision:1,kit:expect.objectContaining({name:'Rock drums'})}))
    expect(backend.request.mock.calls.some(call=>call[0]==='setDocument')).toBe(false)
    await expect(kits.saveSoundKit()).rejects.toThrow('Apply or cancel')
    await store.saveSharedPatch()
    expect(Object.values(store.shared).find(field=>field.key.startsWith('patch/'))!.value.keys[0].sound).toEqual(before.keys[0].sound)
  })
  it('cancels a completed kit preview without a document edit or Undo step',async()=>{
    const store=await setup(),kits=await import('./kits');soundBackend();const before=clone(store.state.patch)
    await kits.matchSounds('Rock drums');kits.cancelSoundSearch()
    expect(store.soundPreview.kit).toBeNull();expect(store.state.patch).toEqual(before);expect(store.state.history).toEqual([])
    expect(backend.request).toHaveBeenCalledWith('cancelKitPreview')
    expect(backend.request.mock.calls.some(call=>call[0]==='setDocument')).toBe(false)
  })
  it('commits the auditioned sounds only on Apply and can undo the kit in one step',async()=>{
    const store=await setup(),kits=await import('./kits');soundBackend();const before=clone(store.state.patch)
    await kits.matchSounds('Rock drums');await kits.applySoundPreview()
    expect(store.soundPreview.kit).toBeNull();expect(store.state.history).toHaveLength(1)
    expect(store.state.patch.keys[0].sound.s.name).toBe('Preview kick');expect(kits.kitState.name).toBe('Rock drums')
    expect(store.state.patch.clips).toEqual(before.clips);expect(store.state.patch.keys[0].zones).toEqual(before.keys[0].zones)
    expect(store.state.patch.keys[0].routes).toEqual(before.keys[0].routes);expect(store.state.patch.keys[0].parts).toEqual(before.keys[0].parts)
    expect(backend.request.mock.calls.filter(call=>call[0]==='setDocument')).toHaveLength(1)
    // Applying must not momentarily cancel the audition while preparing its commit.
    expect(backend.request.mock.calls.some(call=>call[0]==='cancelKitPreview')).toBe(false)
    store.undo();await store.flushEdits(false,0);expect(store.state.patch).toEqual(before)
  })
  it('discards a preview on manual edits and ignores a native preview acknowledgement after Cancel',async()=>{
    const store=await setup(),kits=await import('./kits');soundBackend()
    await kits.matchSounds('Rock drums');store.state.patch.keys[0].sound.s.pitch=7
    expect(store.soundPreview.kit).toBeNull();expect(backend.request).toHaveBeenCalledWith('cancelKitPreview')
    await store.flushEdits();let finish!:(value:any)=>void
    const previous=backend.request.getMockImplementation()!
    backend.request.mockImplementation((method,payload)=>method==='previewKit'?new Promise(resolve=>{finish=resolve}):previous(method,payload))
    const matching=kits.matchSounds('909')
    for(let i=0;i<20;++i)await Promise.resolve()
    expect(store.soundPreview.kit).not.toBeNull();expect(store.soundPreview.ready).toBe(false)
    kits.cancelSoundSearch();finish(true);await matching
    expect(store.soundPreview.kit).toBeNull();expect(store.state.patch.keys[0].sound.s.pitch).toBe(7)
  })
  it('keeps the current kit name when applying a one-key preview',async()=>{
    const store=await setup(),kits=await import('./kits');soundBackend();kits.kitState.id='existing';kits.kitState.name='My kit'
    await kits.matchSounds('Rock kick',48);await kits.applySoundPreview()
    expect(store.state.patch.keys[0].sound.s.name).toBe('Preview kick');expect(kits.kitState.id).toBe('existing');expect(kits.kitState.name).toBe('My kit')
  })
  it('cleans up a failed native audition without changing the document or leaving a matching status',async()=>{
    const store=await setup(),kits=await import('./kits');soundBackend();const before=clone(store.state.patch)
    const previous=backend.request.getMockImplementation()!
    backend.request.mockImplementation((method,payload)=>method==='previewKit'?Promise.reject(Error('Wait for the queued patch')):previous(method,payload))
    await expect(kits.matchSounds('Rock')).rejects.toThrow('Wait for the queued patch')
    expect(store.soundPreview.kit).toBeNull();expect(store.state.patch).toEqual(before);expect(store.state.history).toEqual([])
    expect(kits.kitState.busy).toBe(false);expect(kits.kitState.status).toBe('No preview applied.')
    expect(backend.request).toHaveBeenCalledWith('cancelKitPreview')
  })
  it('does not report a preview applied if the host recalls another patch during Apply',async()=>{
    const store=await setup(),kits=await import('./kits');soundBackend();await kits.matchSounds('Rock drums')
    const restored=factory();restored.name='Host recall'
    backend.request.mockImplementation(async(method)=>method==='setDocument'?{document:restored,revision:9,conflict:true}:true)
    await kits.applySoundPreview()
    expect(store.state.patch.name).toBe('Host recall');expect(store.state.history).toEqual([])
    expect(kits.kitState.status).not.toBe('Applied Rock drums')
  })
  it('saves a kit with a stable identity when renamed',async()=>{
    const store=await setup(),kits=await import('./kits');let revision=1
    backend.request.mockImplementation(async(method,payload)=>{
      if(method==='sharedCommit')return{ok:true,fields:payload.map((operation:any)=>({...operation,revision:++revision}))}
      if(method==='setDocument')return{document:payload.document,revision:++revision,conflict:false}
      return[]
    })
    kits.kitState.name='Dry drums';await kits.saveSoundKit();const id=kits.kitState.id
    kits.kitState.name='Wet drums';await kits.saveSoundKit()
    const calls=backend.request.mock.calls.filter(call=>call[0]==='sharedCommit')
    expect(calls).toHaveLength(2);expect(calls[0][1][0].key).toBe(calls[1][1][0].key)
    expect(kits.kitState.id).toBe(id)
    expect(Object.values(store.shared).filter(field=>field.key.startsWith('kit/'))).toHaveLength(1)
    expect(kits.savedKits.value[0].value.name).toBe('Wet drums')
  })
  it('rejects a late semantic sound result after the user edits the instrument',async()=>{
    const store=await setup(),kits=await import('./kits');let finish!:(value:any)=>void
    backend.request.mockImplementation((method,payload)=>method==='soundMatch'?new Promise(resolve=>{finish=resolve}):Promise.resolve(method==='setDocument'?{document:payload.document,revision:2,conflict:false}:[]))
    const matching=kits.matchSounds('Heavy rock')
    for(let i=0;i<8;++i)await Promise.resolve()
    const original=store.state.patch.keys[0].sound.path
    store.state.patch.keys[0].sound.s.pitch=12
    finish({assignments:[{note:48,asset:{path:'/late-kick.wav',name:'Late kick'}}],sounds:1,limited:false})
    await matching
    expect(store.state.patch.keys[0].sound.path).toBe(original)
    expect(store.state.patch.keys[0].sound.s.pitch).toBe(12)
    expect(kits.kitState.status).toContain('instrument changed')
  })
  it('cancels semantic results when leaving the sound search',async()=>{
    const store=await setup(),kits=await import('./kits');let finish!:(value:any)=>void
    backend.request.mockImplementation(method=>method==='soundMatch'?new Promise(resolve=>{finish=resolve}):Promise.resolve([]))
    const before=clone(store.state.patch),matching=kits.matchSounds('909')
    for(let i=0;i<8;++i)await Promise.resolve()
    kits.cancelSoundSearch();finish({assignments:[{note:48,asset:{path:'/909-kick.wav',name:'909'}}],sounds:1,limited:false})
    await matching;expect(store.state.patch).toEqual(before);expect(kits.kitState.busy).toBe(false)
  })
  it('applies a sound kit in one undo step without changing pattern, filter or controller assignments',async()=>{
    const store=await setup(),{applySoundKit}=await import('./kits'),{captureSoundKit}=await import('./lib/kits')
    let revision=1
    backend.request.mockImplementation(async(method,payload)=>method==='setDocument'?{document:payload.document,revision:++revision,conflict:false}:[])
    const before=clone(store.state.patch),kit=captureSoundKit(before,'New drums','kit-new')
    kit.keys[0].sound.s.pitch=11;kit.keys[1].sound.s.volume=.27
    await applySoundKit(kit)
    expect(store.state.history).toHaveLength(1)
    expect(store.state.patch.keys[0].sound.s.pitch).toBe(11)
    expect(store.state.patch.keys[0].parts).toEqual(before.keys[0].parts)
    expect(store.state.patch.clips).toEqual(before.clips)
    expect(store.state.patch.keys[0].routes).toEqual(before.keys[0].routes)
    store.undo();await store.flushEdits(false,0)
    expect(store.state.patch).toEqual(before)
  })
  it('tracks only the most recent note-on when enabled and leaves selection pinned otherwise',async()=>{
    const {state,sendMidi}=await setup()
    const notes=state.patch.keys.slice(0,4).map(k=>k.note)
    const pinned=state.selected
    backend.telemetry!({midi:[[0x91,notes[1],100],[0x92,notes[2],100]]})
    expect(state.selected).toBe(pinned);expect(state.lastTouchedKey).toBe(notes[2])
    state.trackKeywave=true;expect(state.selected).toBe(notes[2])
    backend.telemetry!({midi:[[0x81,notes[1],100],[0x91,notes[1],0],[0xb1,74,127],[0xd1,127,0]]})
    expect(state.selected).toBe(notes[2])
    sendMidi([0x93,notes[3],100]);expect(state.selected).toBe(notes[3])
    state.trackKeywave=false;backend.telemetry!({midi:[[0x91,notes[0],100]]})
    expect(state.selected).toBe(notes[3]);expect(state.lastTouchedKey).toBe(notes[0])
    expect(state.patch).not.toHaveProperty('trackKeywave')
  })
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

  it('saves a semantic search as an independent, named snapshot with generated notes',async()=>{
    const store=await setup(),saved={...clone(factory()),id:'existing',name:'Heavy Rock'}
    store.shared['patch/existing']={key:'patch/existing',value:clone(saved),revision:4}
    store.state.patch=clone(saved)
    const generated={...clone(saved.clips[0]),id:'generated-fill',generation:{version:1,seed:'812'}} as any
    store.state.patch.clips.push(generated);store.state.patch.keys[0].zones[0].clipId=generated.id
    vi.stubGlobal('crypto',{randomUUID:()=> '00000000-0000-4000-8000-000000000012'})
    backend.request.mockImplementation(async(method,ops)=>{
      if(method!=='sharedCommit')return []
      const op=ops[0];return {ok:true,revision:5,fields:[{key:op.key,value:clone(op.value),revision:5}]}
    })
    expect(await store.saveSharedPatch(false,{newPatch:true,name:'Heavy Rock'})).toMatchObject({ok:true,name:'Heavy Rock 2'})
    const field=store.shared['patch/patch-00000000-0000-4000-8000-000000000012']
    expect(field.value.keys[0].zones[0].clipId).toBe('generated-fill')
    expect(field.value.clips.at(-1)).toEqual(generated)
    store.state.patch.clips.at(-1)!.notes[0]!.velocity=.1
    expect(field.value.clips.at(-1).notes[0].velocity).not.toBe(.1)
    expect(store.shared['patch/existing'].value).toEqual(saved)
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
