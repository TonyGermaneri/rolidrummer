import {afterEach,beforeEach,expect,it,vi} from 'vitest'
import {factory,clone} from '../model'
import {event,find,mount,settle} from '../testing/componentHost'
const fake=vi.hoisted(()=>({state:null as any,request:vi.fn(),status:null as any,row:null as any,clip:null as any,replace:vi.fn(),save:vi.fn(),flush:vi.fn()}))
vi.mock('../native/bridge',()=>({native:()=>true,request:fake.request}))
vi.mock('../store',async()=>{
 const {reactive}=await import('vue')
 fake.state=reactive({patch:{},selected:48,error:'',telemetry:{preview:false}})
 return {flushEdits:fake.flush,saveSharedPatch:fake.save,state:fake.state,replace:fake.replace.mockImplementation(async(patch:any)=>{fake.state.patch=patch}),attempt:async(job:()=>Promise<unknown>)=>{try{return await job()}catch(e){fake.state.error=String(e)}}}
})
vi.mock('./DataGrid.vue',async()=>{
 const {defineComponent,h}=await import('vue')
 return {default:defineComponent({props:['rows','columns','chosen'],setup:props=>()=>h('grid-contract',{rows:props.rows})})}
})
import LibraryPanel from './LibraryPanel.vue'
import {matcher} from '../matcher'
let cleanup:(()=>void)[]=[]
beforeEach(()=>{
 vi.useFakeTimers();vi.stubGlobal('document',{activeElement:null});vi.stubGlobal('Document',class{});vi.stubGlobal('ShadowRoot',class{})
 Object.assign(matcher,{autoMode:true,query:'',seed:'0',phraseBars:8,variationCount:0,source:''});fake.save.mockReset().mockResolvedValue({ok:true,name:'Heavy Rock'});fake.flush.mockReset().mockResolvedValue(undefined)
 fake.state.patch=factory();fake.state.selected=48;fake.state.error='';fake.state.telemetry.activationPending=false;fake.replace.mockClear()
 fake.row={id:'binding-1',patternId:'pattern-1',path:'Rock/Groove.mid',name:'Groove.mid',description:'hard rock groove',family:'hard rock',role:'groove',length:8,numerator:4,denominator:4,density:2,coverage:1,features:{notes:16}}
 fake.clip={...clone(factory().clips[0]),id:'pattern-1',name:'Groove.mid',path:'Rock/Groove.mid'}
 fake.status={catalog:{revision:1,patterns:1,sources:[]},progress:{state:'idle',running:false}}
 fake.request.mockReset().mockImplementation(async(method,payload)=>{
  if(method==='libraryStatus')return clone(fake.status)
  if(method==='librarySearch')return {rows:[clone(fake.row)],indexId:'index-7',revision:1}
  if(method==='libraryBrowse')return {rows:[clone(fake.row)],offset:payload.offset,more:payload.offset===0,revision:1,patterns:1}
  if(method==='libraryPattern')return clone(fake.clip)
  if(method==='libraryImport'){fake.status.progress={state:'queued',running:true};return clone(fake.status.progress)}
  if(method==='libraryCancel'){fake.status.progress={state:'paused',running:false};return true}
  if(method==='chooseLibraryArchive')return '/desktop/drums.zip'
  if(method==='libraryErrors')return [{path:'bad.mid',error:'Unsupported header'}]
  return true
 })
})
afterEach(()=>{cleanup.forEach(fn=>fn());cleanup=[];vi.clearAllTimers();vi.useRealTimers();vi.unstubAllGlobals()})
async function render(auto=false){const view=await mount(LibraryPanel);cleanup.push(view.unmount);if(!auto)await event(find(view.root,n=>n.props['aria-label']==='Auto mode'),'click');return view}

it('assigns a cached catalog pattern through the real drop/import path without inspecting a filesystem path',async()=>{
 const {root}=await render(),grid=find(root,n=>n.tag==='grid-contract')
 expect(grid.props.rows[0].coverageLabel).toBe('100%')
 grid.props.onCell('assign',grid.props.rows[0]);await settle()
 expect(fake.request).toHaveBeenCalledWith('libraryPattern','binding-1')
 expect(fake.request.mock.calls.some(c=>c[0]==='inspectMidi'||c[0]==='importMidi')).toBe(false)
 expect(fake.state.patch.keys[0].zones[0].clipId).toBe('pattern-1')
 grid.props.onCell('assign',grid.props.rows[0]);await settle()
 expect(fake.state.patch.clips.filter((c:any)=>c.id==='pattern-1')).toHaveLength(1)
})

it('pages filtered results and ignores a late response after a newer search',async()=>{
 const {root}=await render()
 const input=find(root,n=>n.props['aria-label']==='Filter indexed patterns')
 let resolve!:(value:any)=>void
 fake.request.mockImplementation(async(method,payload)=>{
  if(method==='libraryBrowse'&&payload.query==='old')return new Promise(r=>{resolve=r})
  if(method==='libraryBrowse')return {rows:[{...fake.row,name:'Newest.mid'}],offset:payload.offset,more:false,revision:1,patterns:1}
  if(method==='libraryStatus')return clone(fake.status)
 })
 input.props['onUpdate:modelValue']('old');await event(input,'keydown',{key:'Enter'})
 input.props['onUpdate:modelValue']('new');await event(input,'keydown',{key:'Enter'})
 resolve({rows:[{...fake.row,name:'Old.mid'}],offset:0,more:false,revision:1,patterns:1});await settle()
 expect(find(root,n=>n.tag==='grid-contract').props.rows[0].name).toBe('Newest.mid')
})

it('rejects a catalog assignment that finishes after the host replaced the patch',async()=>{
 const {root}=await render(),grid=find(root,n=>n.tag==='grid-contract')
 let resolve!:(value:any)=>void
 fake.request.mockImplementation(method=>method==='libraryPattern'?new Promise(r=>{resolve=r}):Promise.resolve(true))
 grid.props.onCell('assign',grid.props.rows[0]);await settle()
 fake.state.patch=factory();resolve(fake.clip);await settle()
 expect(fake.state.patch.clips.some((c:any)=>c.id==='pattern-1')).toBe(false)
 expect(fake.state.error).toContain('destination changed')
})

it('searches a pinned semantic index and resolves its query into a self-contained patch',async()=>{
 fake.status.neural={state:'complete',readyIndexId:'index-7',readyRevision:1,stale:false}
 const original=fake.request.getMockImplementation()!
 fake.request.mockImplementation(async(method,payload)=>{
  if(method==='librarySearch')return {rows:[{...fake.row,similarity:.83}],indexId:'index-7',revision:1,provider:'Metal',milliseconds:14}
  if(method==='libraryResolve')return {...clone(payload.document),name:payload.query,neural:{query:payload.query,numerator:4,denominator:4,feel:'straight',keys:[],warnings:[]}}
  return original(method,payload)
 })
 const {root}=await render(),input=find(root,n=>n.props['aria-label']==='Filter indexed patterns')
 input.props['onUpdate:modelValue']('Heavy Rock');await event(input,'keydown',{key:'Enter'})
 expect(fake.request).toHaveBeenCalledWith('librarySearch',{query:'Heavy Rock',role:'',indexId:'index-7',limit:50})
 expect(find(root,n=>n.tag==='grid-contract').props.rows[0].similarityLabel).toBe('0.830')
 await event(find(root,n=>n.tag==='button'&&n.textContent==='RESOLVE PATCH'),'click')
 expect(fake.state.patch.neural.query).toBe('Heavy Rock')
 expect(fake.state.patch.name).toBe('Heavy Rock')
 expect(fake.replace).toHaveBeenCalledWith(expect.objectContaining({name:'Heavy Rock'}),'bar')
})

it('shows a prepared refresh waiting for a musical boundary',async()=>{
 fake.state.telemetry.activationPending=true
 const {root}=await render()
 expect(find(root,n=>n.props.role==='status'&&n.textContent.includes('next bar'))).toBeTruthy()
 fake.state.telemetry.activationPending=false;await settle()
 expect(root.textContent).not.toContain('next bar')
})

it('resolves optional generated variations with the chosen seed and phrase cycle',async()=>{
 fake.status.neural={state:'complete',readyIndexId:'index-7',readyRevision:1,stale:false}
 const original=fake.request.getMockImplementation()!
 fake.request.mockImplementation(async(method,payload)=>method==='libraryResolve'?clone(payload.document):original(method,payload))
 const {root}=await render()
 find(root,n=>n.props['aria-label']==='Filter indexed patterns').props['onUpdate:modelValue']('Heavy Rock')
 find(root,n=>n.props['aria-label']==='Pattern generation').props['onUpdate:modelValue'](2)
 find(root,n=>n.props['aria-label']==='Pattern variation seed').props['onUpdate:modelValue']('my-session-12')
 find(root,n=>n.props['aria-label']==='Phrase cycle bars').props['onUpdate:modelValue'](4)
 await event(find(root,n=>n.tag==='button'&&n.textContent==='RESOLVE PATCH'),'click')
 const payload=fake.request.mock.calls.find(call=>call[0]==='libraryResolve')![1]
 expect(payload).toMatchObject({query:'Heavy Rock',indexId:'index-7',variationCount:2,seed:'my-session-12',phraseBars:4})
 expect(payload.document.clips).toEqual(factory().clips)
})

it('protects edits made while neural resolution is pending',async()=>{
 fake.status.neural={state:'complete',readyIndexId:'index-7',readyRevision:1,stale:false}
 const original=fake.request.getMockImplementation()!
 let resolve!:(value:any)=>void
 fake.request.mockImplementation((method,payload)=>method==='libraryResolve'?new Promise(r=>{resolve=r}):original(method,payload))
 const {root}=await render(),input=find(root,n=>n.props['aria-label']==='Filter indexed patterns')
 input.props['onUpdate:modelValue']('Heavy Rock');await settle()
 await event(find(root,n=>n.tag==='button'&&n.textContent==='RESOLVE PATCH'),'click')
 fake.state.patch.name='My edited kit'
 resolve({...factory(),name:'Heavy Rock'});await settle()
 expect(fake.state.patch.name).toBe('My edited kit')
 expect(fake.state.error).toContain('patch changed during resolution')
})


function ready(){fake.status.neural={state:'complete',readyIndexId:'index-7',readyRevision:1,stale:false}}
function autoResults(){
 const original=fake.request.getMockImplementation()!
 fake.request.mockImplementation(async(method,payload)=>method==='libraryResolve'?resolved(payload):original(method,payload))
}
function resolved(payload:any){return {...clone(payload.document),name:payload.query,neural:{version:1,query:payload.query,seed:payload.seed,autoMode:payload.autoMode,variationCount:payload.variationCount??0,phraseBars:payload.phraseBars??8,numerator:4,denominator:4,feel:'straight',keys:[],warnings:[]}}}
function input(root:any,label:string,value:string){find(root,n=>n.props['aria-label']===label).props['onUpdate:modelValue'](value)}

it('defaults to Auto and resolves only semantic intent without exposing manual or maintenance controls',async()=>{
 ready();autoResults()
 const {root}=await render(true)
 expect(find(root,n=>n.props['aria-label']==='Auto mode').props['aria-pressed']).toBe(true)
 expect(root.textContent).not.toContain('Semantic similarity')
 expect(root.textContent).not.toContain('All roles')
 expect(root.textContent).not.toContain('IMPORT ZIP')
 input(root,'Filter indexed patterns','Heavy Rock')
 await vi.advanceTimersByTimeAsync(350);await settle()
 expect(fake.request).toHaveBeenCalledWith('libraryResolve',expect.objectContaining({query:'Heavy Rock',seed:'0',autoMode:true,variationCount:0,indexId:'index-7'}))
 expect(fake.request.mock.calls.some(call=>['librarySearch','libraryBrowse'].includes(call[0]))).toBe(false)
 expect(fake.state.patch.name).toBe('Heavy Rock')
 await event(find(root,n=>n.props['aria-label']==='Auto mode'),'click')
 expect(find(root,n=>n.props['aria-label']==='Assignment zone')).toBeTruthy()
 expect(find(root,n=>n.props['aria-label']==='Pattern generation')).toBeTruthy()
})

it('rerolls immediately and coalesces rapid seed changes to the latest audible result',async()=>{
 ready();const original=fake.request.getMockImplementation()!,pending:{payload:any;finish:(p:any)=>void}[]=[]
 fake.request.mockImplementation((method,payload)=>method==='libraryResolve'?new Promise(finish=>pending.push({payload:clone(payload),finish})):original(method,payload))
 vi.stubGlobal('crypto',{getRandomValues:(values:Uint32Array)=>{values[0]=12345;return values}})
 const {root}=await render(true)
 input(root,'Filter indexed patterns','Heavy Rock');await event(find(root,n=>n.props['aria-label']==='Filter indexed patterns'),'keydown',{key:'Enter'})
 expect(pending).toHaveLength(1)
 await event(find(root,n=>n.props['aria-label']==='Reroll seed'),'click')
 expect(matcher.seed).toBe('12345')
 input(root,'Pattern variation seed','session-final');await vi.advanceTimersByTimeAsync(350)
 pending[0]!.finish(resolved(pending[0]!.payload));await settle()
 expect(fake.replace).not.toHaveBeenCalled();expect(pending).toHaveLength(2)
 expect(pending[1]!.payload.seed).toBe('session-final')
 pending[1]!.finish(resolved(pending[1]!.payload));await settle()
 expect(fake.replace).toHaveBeenCalledTimes(1)
 expect(fake.state.patch.neural.seed).toBe('session-final')
})

it('resolves a dirty search before saving an independent named patch',async()=>{
 ready();autoResults();const {root}=await render(true)
 input(root,'Filter indexed patterns','Heavy Rock');input(root,'Pattern variation seed','812')
 await event(find(root,n=>n.props['aria-label']==='Save search as patch'),'click')
 expect(fake.replace).toHaveBeenCalledWith(expect.objectContaining({neural:expect.objectContaining({query:'Heavy Rock',seed:'812',autoMode:true})}),'bar')
 expect(fake.save).toHaveBeenCalledWith(false,{newPatch:true,name:'Heavy Rock'})
 expect(fake.flush).toHaveBeenCalledTimes(2)
 expect(fake.request).toHaveBeenCalledWith('libraryFamilyRegister',expect.objectContaining({query:'Heavy Rock',numerator:4,denominator:4,feel:'straight',origin:'saved'}))
 await vi.advanceTimersByTimeAsync(350)
 expect(fake.request.mock.calls.filter(call=>call[0]==='libraryResolve')).toHaveLength(1)
})

it('registers a family only when a searched groove is saved, not while searching',async()=>{
 ready();autoResults();const {root}=await render(true)
 input(root,'Filter indexed patterns','Heavy');await vi.advanceTimersByTimeAsync(350);input(root,'Filter indexed patterns','Heavy Rock');await vi.advanceTimersByTimeAsync(350)
 expect(fake.request.mock.calls.filter(call=>call[0]==='libraryResolve')).toHaveLength(2)
 expect(fake.request.mock.calls.some(call=>call[0]==='libraryFamilyRegister')).toBe(false)
 fake.state.patch={...fake.state.patch,neural:{...fake.state.patch.neural,familyId:'family-rock',familyBuildId:'build-1'}}
 await event(find(root,n=>n.props['aria-label']==='Save search as patch'),'click')
 expect(fake.save).toHaveBeenCalled();expect(fake.request.mock.calls.some(call=>call[0]==='libraryFamilyRegister')).toBe(false)
})

it('restores saved Auto intent without rerunning or replacing its embedded notes',async()=>{
 ready();fake.state.patch={...factory(),id:'saved-groove',name:'Saved groove',neural:{version:1,autoMode:true,query:'Heavy Rock',seed:'947',numerator:4,denominator:4,feel:'straight',keys:[],warnings:[]}}
 const {root}=await render(true);await vi.advanceTimersByTimeAsync(500)
 expect(matcher).toMatchObject({autoMode:true,query:'Heavy Rock',seed:'947'})
 expect(fake.request.mock.calls.some(call=>call[0]==='libraryResolve')).toBe(false)
 expect(root.textContent).toContain('Ready to play')
})

it('discards pending Auto work when the host recalls another patch',async()=>{
 ready();const original=fake.request.getMockImplementation()!;let finish!:(p:any)=>void;let payload:any
 fake.request.mockImplementation((method,p)=>method==='libraryResolve'?(payload=p,new Promise(resolve=>{finish=resolve})):original(method,p))
 const {root}=await render(true);input(root,'Filter indexed patterns','Heavy Rock')
 await event(find(root,n=>n.props['aria-label']==='Filter indexed patterns'),'keydown',{key:'Enter'})
 fake.state.patch={...factory(),id:'host-recall',name:'Recalled'};await settle()
 finish(resolved(payload));await settle();await vi.advanceTimersByTimeAsync(350)
 expect(fake.state.patch.name).toBe('Recalled');expect(fake.replace).not.toHaveBeenCalled()
})

it('opens legacy resolved patches in Auto without regenerating their saved notes',async()=>{
 ready();fake.state.patch={...factory(),neural:{version:1,query:'Heavy Rock',seed:'legacy',numerator:4,denominator:4,feel:'straight',keys:[],warnings:[]}}
 await render(true);await vi.advanceTimersByTimeAsync(500)
 expect(matcher.autoMode).toBe(true)
 expect(fake.request.mock.calls.some(call=>call[0]==='libraryResolve')).toBe(false)
})

it('does not save a recalled host patch under an earlier search name',async()=>{
 ready();autoResults();const {root}=await render(true)
 input(root,'Filter indexed patterns','Heavy Rock')
 await event(find(root,n=>n.props['aria-label']==='Filter indexed patterns'),'keydown',{key:'Enter'})
 fake.flush.mockImplementationOnce(async()=>{fake.state.patch={...factory(),id:'recalled',name:'Host recalled'}})
 await event(find(root,n=>n.props['aria-label']==='Save search as patch'),'click')
 expect(fake.save).not.toHaveBeenCalled();expect(fake.state.error).toContain('search changed before saving')
})

it('defaults to Patterns Only and keeps the generation budget explicit across seed rerolls',async()=>{
 ready();autoResults();vi.stubGlobal('crypto',{getRandomValues:(values:Uint32Array)=>{values[0]=2468;return values}})
 const {root}=await render(true),selector=find(root,n=>n.props['aria-label']==='Pattern generation')
 expect(matcher.variationCount).toBe(0)
 expect(root.textContent).toContain('Patterns Only')
 expect(root.textContent).toContain('4 Generative')
 input(root,'Filter indexed patterns','Heavy Rock')
 await event(find(root,n=>n.props['aria-label']==='Reroll seed'),'click')
 expect(fake.request).toHaveBeenLastCalledWith('libraryResolve',expect.objectContaining({variationCount:0,seed:'2468'}))
 selector.props['onUpdate:modelValue'](3);await vi.advanceTimersByTimeAsync(350)
 expect(fake.request).toHaveBeenLastCalledWith('libraryResolve',expect.objectContaining({variationCount:3,seed:'2468'}))
 await event(find(root,n=>n.props['aria-label']==='Reroll seed'),'click')
 expect(fake.request).toHaveBeenLastCalledWith('libraryResolve',expect.objectContaining({variationCount:3,seed:'2469'}))
 selector.props['onUpdate:modelValue'](0);await vi.advanceTimersByTimeAsync(350)
 expect(fake.state.patch.neural.variationCount).toBe(0)
 expect(fake.request).toHaveBeenLastCalledWith('libraryResolve',expect.objectContaining({variationCount:0}))
})

it('recalls a saved generation count and saves a change back to Patterns Only',async()=>{
 ready();autoResults();fake.state.patch={...factory(),id:'generated-groove',neural:{version:1,autoMode:true,query:'Heavy Rock',seed:'72',variationCount:4,numerator:4,denominator:4,feel:'straight',keys:[],warnings:[]}}
 const {root}=await render(true),selector=find(root,n=>n.props['aria-label']==='Pattern generation')
 expect(matcher.variationCount).toBe(4)
 expect(fake.request.mock.calls.some(call=>call[0]==='libraryResolve')).toBe(false)
 selector.props['onUpdate:modelValue'](0)
 await event(find(root,n=>n.props['aria-label']==='Save search as patch'),'click')
 expect(fake.state.patch.neural.variationCount).toBe(0)
 expect(fake.save).toHaveBeenCalledWith(false,{newPatch:true,name:'Heavy Rock'})
})

it('discards a late generated result after Patterns Only is selected',async()=>{
 ready();const original=fake.request.getMockImplementation()!,pending:{payload:any;finish:(p:any)=>void}[]=[]
 fake.request.mockImplementation((method,payload)=>method==='libraryResolve'?new Promise(finish=>pending.push({payload:clone(payload),finish})):original(method,payload))
 const {root}=await render(true),selector=find(root,n=>n.props['aria-label']==='Pattern generation')
 input(root,'Filter indexed patterns','Heavy Rock');selector.props['onUpdate:modelValue'](3)
 await event(find(root,n=>n.props['aria-label']==='Filter indexed patterns'),'keydown',{key:'Enter'})
 selector.props['onUpdate:modelValue'](0);await vi.advanceTimersByTimeAsync(350)
 pending[0]!.finish(resolved(pending[0]!.payload));await settle()
 expect(fake.replace).not.toHaveBeenCalled()
 expect(pending[1]!.payload.variationCount).toBe(0)
 pending[1]!.finish(resolved(pending[1]!.payload));await settle()
 expect(fake.state.patch.neural.variationCount).toBe(0)
})
