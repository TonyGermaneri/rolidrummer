import {afterEach,beforeEach,expect,it,vi} from 'vitest'
import {factory,clone} from '../model'
import {event,find,mount,settle} from '../testing/componentHost'
const fake=vi.hoisted(()=>({state:null as any,request:vi.fn(),status:null as any,row:null as any,clip:null as any,replace:vi.fn()}))
vi.mock('../native/bridge',()=>({native:()=>true,request:fake.request}))
vi.mock('../store',async()=>{
 const {reactive}=await import('vue')
 fake.state=reactive({patch:{},selected:48,error:'',telemetry:{preview:false}})
 return {state:fake.state,replace:fake.replace.mockImplementation(async(patch:any)=>{fake.state.patch=patch}),attempt:async(job:()=>Promise<unknown>)=>{try{return await job()}catch(e){fake.state.error=String(e)}}}
})
vi.mock('./DataGrid.vue',async()=>{
 const {defineComponent,h}=await import('vue')
 return {default:defineComponent({props:['rows','columns','chosen'],setup:props=>()=>h('grid-contract',{rows:props.rows})})}
})
import LibraryPanel from './LibraryPanel.vue'
let cleanup:(()=>void)[]=[]
beforeEach(()=>{
 vi.useFakeTimers();vi.stubGlobal('document',{activeElement:null});vi.stubGlobal('Document',class{});vi.stubGlobal('ShadowRoot',class{})
 fake.state.patch=factory();fake.state.selected=48;fake.state.error='';fake.state.telemetry.activationPending=false;fake.replace.mockClear()
 fake.row={id:'binding-1',patternId:'pattern-1',path:'Rock/Groove.mid',name:'Groove.mid',description:'hard rock groove',family:'hard rock',role:'groove',length:8,numerator:4,denominator:4,density:2,coverage:1,features:{notes:16}}
 fake.clip={...clone(factory().clips[0]),id:'pattern-1',name:'Groove.mid',path:'Rock/Groove.mid'}
 fake.status={catalog:{revision:1,patterns:1,sources:[]},progress:{state:'idle',running:false}}
 fake.request.mockReset().mockImplementation(async(method,payload)=>{
  if(method==='libraryStatus')return clone(fake.status)
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
async function render(){const view=await mount(LibraryPanel);cleanup.push(view.unmount);return view}

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

it('starts archive imports and leaves their worker running when the browser unmounts',async()=>{
 const view=await render()
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='IMPORT ZIP…'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryImport',{path:'/desktop/drums.zip',mapping:{id:'auto',notes:{}}})
 expect(find(view.root,n=>n.tag==='button'&&n.textContent==='PAUSE IMPORT')).toBeTruthy()
 view.unmount();cleanup=[]
 expect(fake.request.mock.calls.some(c=>c[0]==='libraryCancel')).toBe(false)
})

it('resumes an interrupted source with its saved mapping and supports explicit pause',async()=>{
 const mapping={id:'custom',notes:{96:'timbaleHigh'}}
 fake.status.catalog.sources=[{id:'source',path:'/drums.zip',publishedRun:'',state:'paused',scanned:256,total:1000,mapping}]
 const {root}=await render()
 await event(find(root,n=>n.tag==='button'&&n.textContent==='Resume drums.zip'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryImport',{path:'/drums.zip',mapping})
 await event(find(root,n=>n.tag==='button'&&n.textContent==='PAUSE IMPORT'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryCancel')
})

it('resumes paused family analysis independently of the completed search index',async()=>{
 fake.status.neural={state:'complete',readyIndexId:'index-7',stale:false}
 fake.status.progress={state:'paused',stage:'family-features',completed:123,running:false}
 const {root}=await render()
 expect(root.textContent).toContain('Family analysis paused')
 await event(find(root,n=>n.tag==='button'&&n.textContent==='RESUME FAMILIES'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryFamilyRefresh')
})

it('shows family worker failures as actionable errors',async()=>{
 fake.status.progress={state:'failed',stage:'families',error:'Catalog disk is full',running:false}
 const {root}=await render()
 expect(root.textContent).toContain('Catalog disk is full')
})

it.each(['snapshot','search-metadata'])('resumes %s preparation while the previous complete index stays searchable',async(stage)=>{
 fake.status.neural={state:'complete',readyIndexId:'index-7',stale:false}
 fake.status.progress={state:'paused',stage,running:false}
 const {root}=await render()
 expect(root.textContent).toContain('Search preparation paused')
 await event(find(root,n=>n.tag==='button'&&n.textContent==='RESUME SEARCH'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryIndex')
})

it('offers background preparation for an unchanged complete index',async()=>{
 fake.status.neural={state:'complete',readyIndexId:'index-7',stale:false,preparationNeeded:true}
 const {root}=await render()
 await event(find(root,n=>n.tag==='button'&&n.textContent==='REFRESH SEARCH'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryIndex')
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
 expect(find(root,n=>n.props.role==='status'&&n.textContent.includes('waiting for the next bar'))).toBeTruthy()
 fake.state.telemetry.activationPending=false;await settle()
 expect(root.textContent).not.toContain('waiting for the next bar')
})

it('resolves optional generated variations with the chosen seed and phrase cycle',async()=>{
 fake.status.neural={state:'complete',readyIndexId:'index-7',readyRevision:1,stale:false}
 const original=fake.request.getMockImplementation()!
 fake.request.mockImplementation(async(method,payload)=>method==='libraryResolve'?clone(payload.document):original(method,payload))
 const {root}=await render()
 find(root,n=>n.props['aria-label']==='Filter indexed patterns').props['onUpdate:modelValue']('Heavy Rock')
 find(root,n=>n.props['aria-label']==='Generated variations per region').props['onUpdate:modelValue'](2)
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

it('resolves a selected family with its structural constraints and editable display name',async()=>{
 fake.status.neural={state:'complete',readyIndexId:'index-7',readyRevision:1,stale:false}
 fake.status.families={revision:3,buildId:'build-1',policyVersion:4,state:'complete',stale:false,suggestions:0,families:[{id:'family-bossa',name:'My Bossa',query:'Bossa Nova',numerator:7,denominator:8,feel:'triplet',revision:3,members:3,ambiguous:0,threshold:.8}]}
 const original=fake.request.getMockImplementation()!
 fake.request.mockImplementation(async(method,payload)=>{
  if(method==='libraryFamilyMembers')return {rows:[],offset:0,more:false}
  if(method==='libraryResolve')return {...clone(payload.document),name:payload.query,neural:{version:1,query:payload.query,numerator:7,denominator:8,feel:'triplet',keys:[],warnings:[]}}
  return original(method,payload)
 })
 const {root}=await render();await event(find(root,n=>n.tag==='details'&&n.props.class==='family-panel'),'toggle',{target:{open:true}})
 await event(find(root,n=>n.tag==='button'&&n.textContent==='RESOLVE FAMILY'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryResolve',expect.objectContaining({query:'Bossa Nova',indexId:'index-7',familyId:'family-bossa',numerator:7,denominator:8,feel:'triplet'}))
 expect(fake.state.patch.name).toBe('My Bossa')
 expect(fake.replace).toHaveBeenCalledWith(expect.objectContaining({name:'My Bossa'}),'bar')
})
