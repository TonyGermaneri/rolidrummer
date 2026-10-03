import { reactive, watch } from 'vue'
import { state } from './store'
import { request } from './native/bridge'
import { importPattern } from './midiImport'
import type { Asset } from './model'
export const preview=reactive({id:'',loading:false})
let generation=0
watch(()=>state.telemetry?.preview,(playing,wasPlaying)=>{if(wasPlaying&&!playing&&!preview.loading)preview.id=''})
export async function stopPreview(){++generation;preview.id='';preview.loading=false;await request('stopPreview')}
export async function audition(asset:Asset,patterns:boolean){
 if(asset.folder)return
 if(preview.id===asset.id){await stopPreview();return}
 const ticket=++generation;preview.id=asset.id;preview.loading=true
 try{
  await request('stopPreview');if(ticket!==generation)return
  if(patterns){const clip=state.patch.clips.find(c=>c.id===asset.id)??await importPattern(asset);if(ticket!==generation)return;if(!clip){preview.id='';return}await request('preview',{kind:'pattern',clip})}
  else await request('preview',{kind:'sound',path:asset.path,...(asset.preset?{sound:asset.preset.sound}: {})})
 }catch(error){if(ticket===generation)preview.id='';throw error}
 finally{if(ticket===generation)preview.loading=false}
}
