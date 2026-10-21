import {afterEach,beforeEach,expect,it,vi} from 'vitest'
import {event,find,mount,nodes,settle} from '../testing/componentHost'
import type {FamilyStatus,PatternFamily} from '../library'
const fake=vi.hoisted(()=>({request:vi.fn(),error:'',hidden:false}))
vi.mock('../native/bridge',()=>({request:fake.request}))
vi.mock('../store',()=>({attempt:async(job:()=>Promise<unknown>)=>{try{return await job()}catch(error){fake.error=String(error)}}}))
import FamilyPanel from './FamilyPanel.vue'
const rock:PatternFamily={id:'family-rock',name:'Heavy Rock',query:'Heavy Rock',canonicalQuery:'hard rock drum patterns',numerator:4,denominator:4,feel:'straight',revision:3,members:3,ambiguous:1,threshold:.8}
const bossa:PatternFamily={...rock,id:'family-bossa',name:'Bossa Nova',query:'Bossa Nova'}
const member={patternId:'pattern-1',bindingId:'binding-1',path:'Hard Rock/Fills/New.mid',numerator:4,denominator:4,role:'fill',score:.85,state:'ambiguous',decision:'auto'}
const suggestion={id:'suggestion-1',name:'Bossa Nova',query:'bossa nova',numerator:4,denominator:4,feel:'straight',patterns:3,supportedPatterns:4,cohesion:.91,overlap:[{familyId:rock.id,patterns:1}],examples:[{patternId:'bossa-1',bindingId:'binding-bossa',path:'Bossa Nova/Fills/1.mid'}]}
let cleanup:(()=>void)[]=[]
beforeEach(()=>{
 vi.stubGlobal('document',{activeElement:null});vi.stubGlobal('Document',class{});vi.stubGlobal('ShadowRoot',class{})
 fake.error='';fake.hidden=false;let revision=3
 fake.request.mockReset().mockImplementation(async(method,payload)=>{
  if(method==='libraryFamilyMembers')return {rows:[member],offset:payload.offset,more:payload.offset===0}
  if(method==='libraryFamilyDecisions')return {rows:[{...member,decision:'exclude'}],offset:payload.offset,more:false}
  if(method==='libraryFamilySuggestions')return {rows:fake.hidden?[]:[suggestion],buildId:'build-1',offset:payload.offset,more:payload.offset===0}
  if(method==='libraryFamilyUpdate')return {revision:++revision,family:{...rock,name:payload.name??rock.name}}
  if(method==='libraryFamilyDecision'){fake.hidden=true;return {revision:++revision,family:payload.action==='accept'?{...bossa,name:payload.name}:rock}}
  if(method==='libraryFamilyRemove')return {revision:++revision,removed:payload.familyIds.length}
  if(method==='libraryFamilyRefresh')return {state:'queued',running:true}
  throw Error(`Unexpected family request ${method}`)
 })
})
afterEach(()=>{cleanup.forEach(fn=>fn());cleanup=[];vi.unstubAllGlobals()})
function status(patch:Partial<FamilyStatus>={}):FamilyStatus{return {revision:3,buildId:'build-1',indexId:'index-1',policyVersion:4,state:'complete',stale:false,families:[rock],suggestions:1,...patch}}
async function render(value=status(),progress?:Record<string,unknown>){
 const resolve=vi.fn(),refresh=vi.fn(),props={status:value,progress,onResolve:resolve,onRefresh:refresh}
 const view=await mount(FamilyPanel,props);cleanup.push(view.unmount)
 return {...view,resolve,refresh,props,open:()=>event(find(view.root,n=>n.tag==='details'),'toggle',{target:{open:true}})}
}

it('loads bounded family pages only when opened and emits an explicit resolution',async()=>{
 const view=await render();expect(fake.request).not.toHaveBeenCalled();await view.open()
 expect(fake.request).toHaveBeenCalledTimes(1)
 expect(fake.request).toHaveBeenLastCalledWith('libraryFamilyMembers',{familyId:rock.id,buildId:'build-1',offset:0,limit:25})
 expect(view.root.textContent).toContain('Hard Rock/Fills/New.mid')
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='RESOLVE FAMILY'),'click')
 expect(view.resolve).toHaveBeenCalledWith(expect.objectContaining({id:rock.id,query:'Heavy Rock'}))
 expect(fake.request.mock.calls.some(([method])=>method==='setDocument'||method==='sharedCommit')).toBe(false)
})

it('opens the family editor in a dialog and closes it without another catalog request',async()=>{
 const view=await render();await view.open();const modal=find(view.root,n=>n.tag==='dialog')
 expect(modal.open).toBe(true);const before=fake.request.mock.calls.length
 await event(find(view.root,n=>n.props['aria-label']==='Close pattern families'),'click')
 expect(modal.open).toBe(false);expect(fake.request).toHaveBeenCalledTimes(before)
})

it('uses acknowledged revisions for sequential manual decisions without changing playback',async()=>{
 const view=await render();await view.open()
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='Include'),'click')
 expect(find(view.root,n=>n.tag==='button'&&n.textContent==='Include').props.disabled).toBe(true)
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='Exclude'),'click')
 const writes=fake.request.mock.calls.filter(call=>call[0]==='libraryFamilyUpdate')
 expect(writes.map(call=>call[1])).toEqual([{familyId:rock.id,patternId:'pattern-1',action:'include',revision:3},{familyId:rock.id,patternId:'pattern-1',action:'exclude',revision:4}])
 expect(view.refresh).toHaveBeenCalledTimes(2);expect(view.resolve).not.toHaveBeenCalled()
})

it('keeps draft names through an unrelated poll and renames the same family identity',async()=>{
 const view=await render();await view.open()
 find(view.root,n=>n.props['aria-label']==='Family display name').props['onUpdate:modelValue']('My rock family');await settle()
 await view.update({...view.props,status:status({revision:4,families:[{...rock,name:'Another editor renamed it'}]})})
 expect(find(view.root,n=>n.props['aria-label']==='Family display name').value).toBe('My rock family')
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='RENAME'),'click')
 expect(fake.request).toHaveBeenLastCalledWith('libraryFamilyUpdate',{familyId:rock.id,action:'rename',name:'My rock family',revision:4})
})

it('lists exclusions so they can be returned to automatic matching',async()=>{
 const view=await render();await view.open()
 find(view.root,n=>n.props['aria-label']==='Family pattern view').props['onUpdate:modelValue']('decisions');await settle()
 expect(fake.request).toHaveBeenLastCalledWith('libraryFamilyDecisions',expect.objectContaining({familyId:rock.id,offset:0,limit:25}))
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='Auto'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryFamilyUpdate',expect.objectContaining({familyId:rock.id,patternId:'pattern-1',action:'auto'}))
})

it('includes a library result outside the current family using its normalized pattern identity',async()=>{
 const view=await render();await view.open()
 await view.update({...view.props,candidate:{id:'different-binding',patternId:'outside-pattern',path:'Unlabeled/1.mid',name:'1.mid'}})
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='INCLUDE LIBRARY RESULT'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryFamilyUpdate',{familyId:rock.id,patternId:'outside-pattern',action:'include',revision:3})
 expect(view.resolve).not.toHaveBeenCalled()
})

it('accepts an editable suggestion with its source evidence and keeps MIDI storage untouched',async()=>{
 const view=await render();await view.open();await event(find(view.root,n=>n.tag==='button'&&n.textContent==='NAME SUGGESTIONS'),'click')
 expect(view.root.textContent).toContain('Bossa Nova/Fills/1.mid');expect(view.root.textContent).toContain('1 shared')
 find(view.root,n=>n.props['aria-label']==='Suggested family name: Bossa Nova').props['onUpdate:modelValue']('My Bossa');await settle()
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='ACCEPT'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryFamilyDecision',expect.objectContaining({id:'suggestion-1',buildId:'build-1',action:'accept',name:'My Bossa',revision:3}))
 expect(view.resolve).not.toHaveBeenCalled()
 expect(fake.request.mock.calls.some(([method])=>method==='setDocument'||method==='sharedCommit')).toBe(false)
})

it('merges only into compatible families and persists dismissal separately',async()=>{
 const view=await render(status({families:[rock,{...bossa,id:'odd-family',numerator:7,denominator:8}]}));await view.open();await event(find(view.root,n=>n.tag==='button'&&n.textContent==='NAME SUGGESTIONS'),'click')
 const target=find(view.root,n=>n.props['aria-label']==='Merge target: Bossa Nova')
 expect(target.options.map(option=>option.props.value)).toEqual(['',rock.id])
 expect(find(view.root,n=>n.tag==='button'&&n.textContent==='MERGE').props.disabled).toBe(true)
 target.props['onUpdate:modelValue'](rock.id);await settle()
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='MERGE'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryFamilyDecision',expect.objectContaining({familyId:rock.id,action:'merge',buildId:'build-1'}))
 fake.hidden=false;await view.update({...view.props,status:status({revision:5})})
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='DISMISS'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryFamilyDecision',expect.objectContaining({action:'dismiss',revision:5}))
})

it('resolves from the last matched membership during matching and reports revision conflicts with a refresh',async()=>{
 const view=await render(status({stale:true,staleReason:'matching'}),{state:'building',stage:'families',running:true,completed:2,groups:5});await view.open()
 expect(find(view.root,n=>n.tag==='summary').textContent).toContain('Matching updates… 2/5')
 expect(find(view.root,n=>n.tag==='button'&&n.textContent==='RESOLVE FAMILY').props.disabled).toBe(false)
 const original=fake.request.getMockImplementation()!;fake.request.mockImplementation((method,payload)=>method==='libraryFamilyUpdate'?Promise.reject(Error('Another instance changed the families')):original(method,payload))
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='Include'),'click')
 expect(fake.error).toContain('Another instance changed');expect(view.refresh).toHaveBeenCalled()
})

it('discards a late member page after selecting a different family',async()=>{
 const original=fake.request.getMockImplementation()!;let finish!:(value:unknown)=>void
 fake.request.mockImplementation((method,payload)=>method==='libraryFamilyMembers'&&payload.familyId===rock.id?new Promise(resolve=>{finish=resolve}):method==='libraryFamilyMembers'?Promise.resolve({rows:[{...member,path:'Bossa Nova/Groove.mid'}],offset:0,more:false}):original(method,payload))
 const view=await render(status({families:[rock,bossa]}));await view.open()
 find(view.root,n=>n.props['aria-label']==='Pattern family').props['onUpdate:modelValue'](bossa.id);await settle()
 finish({rows:[member],offset:0,more:false});await settle()
 expect(view.root.textContent).toContain('Bossa Nova/Groove.mid');expect(view.root.textContent).not.toContain('Hard Rock/Fills/New.mid')
})

it('does not follow a late mutation with new requests after unmount',async()=>{
 let finish!:(value:unknown)=>void;const original=fake.request.getMockImplementation()!
 fake.request.mockImplementation((method,payload)=>method==='libraryFamilyDecision'?new Promise(resolve=>{finish=resolve}):original(method,payload))
 const view=await render();await view.open();await event(find(view.root,n=>n.tag==='button'&&n.textContent==='NAME SUGGESTIONS'),'click')
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='ACCEPT'),'click');const before=fake.request.mock.calls.length
 view.unmount();cleanup=[];finish({revision:4,family:bossa});await settle()
 expect(fake.request).toHaveBeenCalledTimes(before)
 expect(nodes(view.root,n=>n.tag==='article')).toHaveLength(0)
})

it('says what out-of-date membership waits for and matches on request',async()=>{
 const summary=(view:{root:any})=>find(view.root,n=>n.tag==='summary').textContent
 const idle=await render(status({stale:true,staleReason:'matching'}),{state:'complete',stage:'ready',running:false})
 expect(summary(idle)).toContain('Out of date');expect(summary(idle)).not.toContain('Matching updates')
 await event(find(idle.root,n=>n.tag==='button'&&n.textContent==='MATCH NOW'),'click')
 expect(fake.request).toHaveBeenCalledWith('libraryFamilyRefresh');expect(idle.refresh).toHaveBeenCalled()
 const failed=await render(status({stale:true,staleReason:'matching'}),{state:'failed',stage:'families',running:false,error:'database is locked'})
 expect(summary(failed)).toContain('Matching failed');expect(nodes(failed.root,n=>n.tag==='button'&&n.textContent==='MATCH NOW')).toHaveLength(0)
 await failed.open();expect(failed.root.textContent).toContain('Matching failed: database is locked')
 const search=await render(status({stale:true,staleReason:'search'}),{state:'complete',running:false})
 expect(summary(search)).toContain('Waiting for search to be refreshed');expect(nodes(search.root,n=>n.tag==='button'&&n.textContent==='MATCH NOW')).toHaveLength(0)
 const another=await render(status({stale:true,staleReason:'matching'}),{state:'busy',stage:'families',running:true})
 expect(summary(another)).toContain('Another instance is matching')
})

it('removes a family, or every family searches registered, only after confirmation',async()=>{
 const searched={...rock,id:'family-heavy',name:'heavy',query:'heavy'},saved={...searched,id:'family-saved',name:'Saved',query:'Saved',origin:'saved' as const}
 const edited={...searched,id:'family-edited',name:'edited',query:'edited',decisions:1},accepted={...searched,id:'family-accepted',name:'accepted',query:'accepted',suggested:true}
 const view=await render(status({families:[rock,searched,saved,edited,accepted]}));await view.open()
 const bulk=()=>find(view.root,n=>n.tag==='button'&&String(n.textContent).includes('SEARCH FAMILIES')||n.textContent==='CONFIRM REMOVE 2')
 expect(view.root.textContent).toContain('2 families were registered by searches')
 await event(bulk(),'click');expect(fake.request.mock.calls.some(call=>call[0]==='libraryFamilyRemove')).toBe(false)
 await event(bulk(),'click')
 expect(fake.request).toHaveBeenLastCalledWith('libraryFamilyRemove',{familyIds:['family-rock','family-heavy'],revision:3})
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='REMOVE'),'click')
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='CONFIRM REMOVE'),'click')
 expect(fake.request).toHaveBeenLastCalledWith('libraryFamilyRemove',{familyIds:['family-rock'],revision:4})
})
