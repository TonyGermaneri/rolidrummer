import {afterEach,beforeEach,expect,it,vi} from 'vitest'
import {factory,clone} from '../model'
import {event,find,mount} from '../testing/componentHost'
const fake=vi.hoisted(()=>({state:null as any,request:vi.fn(),status:null as any,row:null as any,clip:null as any,replace:vi.fn(),save:vi.fn(),flush:vi.fn()}))
vi.mock('../native/bridge',()=>({native:()=>true,request:fake.request}))
vi.mock('../store',async()=>{
 const {reactive}=await import('vue')
 fake.state=reactive({patch:{},selected:48,error:'',telemetry:{preview:false}})
 return {soundPreview:reactive({kit:null,ready:false}),flushEdits:fake.flush,saveSharedPatch:fake.save,state:fake.state,replace:fake.replace.mockImplementation(async(patch:any)=>{fake.state.patch=patch}),attempt:async(job:()=>Promise<unknown>)=>{try{return await job()}catch(e){fake.state.error=String(e)}}}
})
vi.mock('./DataGrid.vue',async()=>{
 const {defineComponent,h}=await import('vue')
 return {default:defineComponent({props:['rows','columns','chosen'],setup:props=>()=>h('grid-contract',{rows:props.rows})})}
})
import LibrarySettings from './LibrarySettings.vue'
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
async function render(){const view=await mount(LibrarySettings);cleanup.push(view.unmount);return view}

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
