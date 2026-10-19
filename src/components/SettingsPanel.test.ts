import {afterEach,beforeEach,expect,it,vi} from 'vitest'
import {factory,clone} from '../model'
import {event,find,mount,settle} from '../testing/componentHost'
const fake=vi.hoisted(()=>({state:null as any,shared:{} as any,request:vi.fn(),replace:vi.fn(),put:vi.fn()}))
vi.mock('../native/bridge',()=>({native:()=>true,request:fake.request}))
vi.mock('../store',async()=>{
 const {reactive}=await import('vue')
 fake.state=reactive({patch:factory(),settingsSection:'libraries',standalone:true})
 return {state:fake.state,shared:fake.shared,putShared:fake.put,replace:fake.replace,attempt:async(job:()=>unknown)=>{try{return await job()}catch(error){fake.state.error=String(error)}}}
})
vi.mock('./LibrarySettings.vue',async()=>{const {defineComponent,h}=await import('vue');return {default:defineComponent({setup:()=>()=>h('library-maintenance')})}})
vi.mock('./AssetBrowser.vue',async()=>{const {defineComponent,h}=await import('vue');return {default:defineComponent({setup:()=>()=>h('sound-browser')})}})
vi.mock('./MidiSettings.vue',async()=>{const {defineComponent,h}=await import('vue');return {default:defineComponent({setup:()=>()=>h('midi-settings')})}})
import SettingsPanel from './SettingsPanel.vue'
let cleanups:(()=>void)[]=[]
beforeEach(()=>{
 fake.state.patch=factory();fake.state.settingsSection='libraries';fake.state.error='';fake.replace.mockReset();fake.put.mockReset().mockResolvedValue(true)
 fake.request.mockReset().mockResolvedValue({directory:'/private/test-data',databases:[{name:'shared.sqlite',bytes:2048},{name:'library.sqlite',bytes:4096}],sounds:12,soundBytes:1048576,patterns:823,shared:{fields:9,patches:3}})
})
afterEach(()=>{cleanups.splice(0).forEach(fn=>fn())})
async function render(){const view=await mount(SettingsPanel);cleanups.push(view.unmount);return view.root}
async function category(root:any,name:string){await event(find(root,n=>n.tag==='button'&&n.textContent===name),'click')}
it('keeps library maintenance and sound management in their Settings categories',async()=>{
 const root=await render();expect(find(root,n=>n.tag==='library-maintenance')).toBeTruthy()
 await category(root,'Sounds');expect(find(root,n=>n.tag==='sound-browser')).toBeTruthy()
 expect(root.textContent).toContain('Assigned sounds')
 await category(root,'MIDI & controllers');expect(find(root,n=>n.tag==='midi-settings')).toBeTruthy()
})
it('edits the existing playback model and exposes native storage statistics',async()=>{
 const root=await render();await category(root,'Playback')
 find(root,n=>n.props['aria-label']==='Pattern launch quantization').props['onUpdate:modelValue'](.5)
 find(root,n=>n.props['aria-label']==='Key release behavior').props['onUpdate:modelValue'](true)
 expect(fake.state.patch.launchGrid).toBe(.5);expect(fake.state.patch.hardRelease).toBe(true)
 find(root,n=>n.props['aria-label']==='Main reverb size').props['onUpdate:modelValue'](4.2)
 find(root,n=>n.props['aria-label']==='REVERB controller').props['onUpdate:modelValue'](2)
 expect(fake.state.patch.fx.reverbSize).toBe(4.2);expect(fake.state.patch.fx.bindings.reverbMix).toBe(2)
 await category(root,'Storage & database')
 expect(fake.request).toHaveBeenCalledWith('systemStatus')
 expect(root.textContent).toContain('823');expect(root.textContent).toContain('1.0 MB');expect(root.textContent).toContain('library.sqlite')
})
it('restores the factory instrument through the existing undoable document path',async()=>{
 const root=await render();fake.state.patch.name='My edited kit';await category(root,'Defaults')
 await event(find(root,n=>n.tag==='button'&&n.textContent==='Restore factory defaults'),'click')
 expect(fake.replace).toHaveBeenCalledWith(factory())
 expect(fake.state.patch.name).toBe('My edited kit')
})
it('rejects late sound relinks after the host recalls another instrument',async()=>{
 fake.state.patch.keys[0].sound.missing=true;let finish!:(value:any)=>void
 fake.request.mockImplementation(()=>new Promise(resolve=>{finish=resolve}))
 const root=await render();await category(root,'Sounds')
 await event(find(root,n=>n.tag==='button'&&n.textContent==='Locate sound…'),'click')
 fake.state.patch={...clone(factory()),name:'Host recalled'};finish('/new/sound.wav');await settle()
 expect(fake.state.patch.keys[0].sound.path).not.toBe('/new/sound.wav')
 expect(fake.state.error).toContain('instrument changed')
})
