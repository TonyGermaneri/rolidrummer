import {beforeEach,afterEach,expect,it,vi} from 'vitest'
import {event,find,mount} from '../testing/componentHost'
const fake=vi.hoisted(()=>({state:null as any,preview:null as any,kit:null as any,save:vi.fn(),apply:vi.fn(),commit:vi.fn(),match:vi.fn(),cancel:vi.fn()}))
vi.mock('../store',async()=>{const {reactive}=await import('vue');fake.state=reactive({selected:48,saving:false});fake.preview=reactive({kit:null,ready:false});return{state:fake.state,soundPreview:fake.preview,attempt:async(job:any)=>job()}})
vi.mock('../kits',async()=>{const {reactive,ref}=await import('vue');fake.kit=reactive({name:'Saved drums',query:'Heavy rock',scope:'all',busy:false,saving:false,status:''});return{kitState:fake.kit,savedKits:ref([{key:'kit/a',value:{version:1,id:'a',name:'Acoustic',keys:[{note:48,sound:{}}]}}]),soundRoots:ref(['/samples']),saveSoundKit:fake.save,applySoundKit:fake.apply,applySoundPreview:fake.commit,matchSounds:fake.match,cancelSoundSearch:fake.cancel}})
vi.mock('./AssetBrowser.vue',async()=>{const {defineComponent,h}=await import('vue');return{default:defineComponent({props:['assignTo'],setup:props=>()=>h('sound-browser-contract',{assignTo:props.assignTo})})}})
import KitPanel from './KitPanel.vue'
let cleanup:(()=>void)|undefined
beforeEach(()=>{fake.save.mockReset();fake.apply.mockReset();fake.commit.mockReset();fake.match.mockReset();fake.cancel.mockReset();fake.kit.scope='all';fake.preview.kit=null;fake.preview.ready=false})
afterEach(()=>{cleanup?.();cleanup=undefined})
it('loads and saves sound kits and submits semantic matching for all or one keywave',async()=>{
 const view=await mount(KitPanel);cleanup=view.unmount
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='SAVE KIT'),'click');expect(fake.save).toHaveBeenCalledWith()
 await event(find(view.root,n=>n.tag==='button'&&n.textContent.includes('Acoustic')),'click');expect(fake.apply).toHaveBeenCalledWith(expect.objectContaining({id:'a'}))
 await event(find(view.root,n=>n.tag==='form'),'submit');expect(fake.match).toHaveBeenLastCalledWith('Heavy rock',undefined)
 fake.kit.scope='selected';await event(find(view.root,n=>n.tag==='form'),'submit');expect(fake.match).toHaveBeenLastCalledWith('Heavy rock',48)
 expect(find(view.root,n=>n.tag==='sound-browser-contract').props.assignTo).toBe(48)
})
it('shows audition bindings and explicit Apply/Cancel while preventing an uncommitted kit save',async()=>{
 fake.preview.kit={name:'Heavy rock',keys:[{note:48,sound:{s:{name:'Rock kick'}}},{note:50,sound:{s:{name:'Rock snare'}}}]};fake.preview.ready=true
 const view=await mount(KitPanel);cleanup=view.unmount
 expect(find(view.root,n=>n.props['aria-label']==='Preview bindings').textContent).toContain('Rock snare')
 expect(find(view.root,n=>n.tag==='button'&&n.textContent==='SAVE KIT').props.disabled).toBe(true)
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='APPLY KIT'),'click');expect(fake.commit).toHaveBeenCalledOnce()
 await event(find(view.root,n=>n.tag==='button'&&n.textContent==='CANCEL'),'click');expect(fake.cancel).toHaveBeenCalledOnce()
})
