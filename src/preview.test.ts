import {beforeEach,expect,it,vi} from 'vitest'
import {factory,factoryAssets,type Asset} from './model'
const fake=vi.hoisted(()=>({request:vi.fn(),import:vi.fn(),state:{patch:null as any}}))
vi.mock('./native/bridge',()=>({request:fake.request}))
vi.mock('./midiImport',()=>({importPattern:fake.import}))
vi.mock('./store',()=>({state:fake.state}))
import {audition,stopPreview,preview} from './preview'
const sound:Asset={id:'kick',path:'/kick.wav',name:'Kick',folder:false,size:1}
beforeEach(async()=>{fake.request.mockReset().mockResolvedValue(true);fake.import.mockReset();fake.state.patch=factory();await stopPreview();fake.request.mockClear()})
it('toggles a sound preview without changing the kit',async()=>{
 const saved=JSON.stringify(fake.state.patch);await audition(sound,false)
 expect(fake.request).toHaveBeenLastCalledWith('preview',{kind:'sound',path:sound.path});expect(preview.id).toBe(sound.id)
 await audition(sound,false);expect(fake.request).toHaveBeenLastCalledWith('stopPreview');expect(preview.id).toBe('');expect(JSON.stringify(fake.state.patch)).toBe(saved)
})
it('previews known patterns using the kit, with no MIDI reimport',async()=>{
 const clip=fake.state.patch.clips[0];await audition({...sound,id:clip.id},true)
 expect(fake.import).not.toHaveBeenCalled();expect(fake.request).toHaveBeenLastCalledWith('preview',{kind:'pattern',clip})
})
it('stops the old audition before asking for MIDI selection and rejects a late selection after stop',async()=>{
 await audition(sound,false)
 let finish!:(value:any)=>void;fake.import.mockReturnValue(new Promise(resolve=>finish=resolve))
 const work=audition({...sound,id:'new-pattern'},true);await Promise.resolve()
 expect(fake.request).toHaveBeenLastCalledWith('stopPreview')
 await stopPreview();finish(fake.state.patch.clips[0]);await work
 expect(fake.request).toHaveBeenLastCalledWith('stopPreview');expect(preview.id).toBe('')
})

it('auditions a synth preset with its tuning and envelope',async()=>{
 const asset=factoryAssets().find(a=>a.name==='Low tom')!;await audition(asset,false)
 expect(fake.request).toHaveBeenLastCalledWith('preview',{kind:'sound',path:asset.path,sound:asset.preset!.sound})
})
