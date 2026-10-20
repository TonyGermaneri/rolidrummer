import {computed,reactive,watch} from 'vue'
import {state,shared,flushEdits,writeShared,refreshShared,soundPreview,previewSoundKit,cancelSoundPreview,commitSoundPreview,report} from './store'
import {clone,factoryAssets,partName,type Asset} from './model'
import {defaultSettings} from './types'
import {request,native} from './native/bridge'
import {captureSoundKit,withSoundKit,type SoundKit} from './lib/kits'
export const kitState=reactive({id:'',name:'',query:'',busy:false,saving:false,scope:'all',status:'',previewAll:true})
watch(()=>soundPreview.kit,(kit,previous)=>{if(!kit&&previous&&kitState.status.startsWith('Previewing'))kitState.status='Kit preview ended. Current patch sounds are active.'},{flush:'sync'})
export const savedKits=computed(()=>Object.values(shared).filter(field=>field.key.startsWith('kit/')&&field.value?.version===1).sort((a,b)=>a.value.name.localeCompare(b.value.name)))
export const soundRoots=computed(()=>[...new Set([...(shared['sounds/libraries']?.value as string[]??[]),String(shared['browser/sounds/navigation']?.value.path??'')].filter(path=>path&&path!=='/'))])
export async function rememberSoundRoot(path:string){
 if(!path||path==='/')return
 const roots=soundRoots.value.filter(root=>root!==path)
 const result=await writeShared('sounds/libraries',[...roots,path].slice(-32),shared['sounds/libraries']?.revision??0)
 if(!result.ok)await refreshShared()
}
export async function applySoundKit(kit:SoundKit,guard=()=>true){
 const checked=withSoundKit(state.patch,kit)
 await flushEdits()
 if(!guard())return false
 await cancelSoundPreview()
 // Native acknowledgement may have normalized the document in the meantime.
 if(state.patch.id!==checked.id)throw Error('The patch changed while preparing this kit. Select the kit again.')
 state.patch=withSoundKit(state.patch,kit);await flushEdits()
 kitState.id=kit.id==='search'?'':kit.id;kitState.name=kit.name;kitState.status=`Applied ${kit.name}`
 return true
}
export async function saveSoundKit(copy=false){
 if(soundPreview.kit)throw Error('Apply or cancel the kit preview before saving a kit.')
 if(kitState.saving)return
 kitState.saving=true
 try{
  await flushEdits();await refreshShared()
  let name=kitState.name.trim();if(!name)throw Error('Give this kit a name.')
  const selectionId=kitState.id,requestedName=kitState.name
  const existing=copy?undefined:selectionId?savedKits.value.find(field=>field.value.id===selectionId):savedKits.value.find(field=>field.value.name===name)
  if(copy){const base=name;let suffix=1;do{name=`${base} copy${suffix>1?` ${suffix}`:''}`;++suffix}while(savedKits.value.some(field=>field.value.name===name))}
  const id=existing?.value.id??`kit-${crypto.randomUUID()}`,kit=captureSoundKit(state.patch,name,id),key=existing?.key??`kit/${id}`
  const result=await writeShared(key,kit,shared[key]?.revision??0)
  if(!result.ok)throw Error('Another instance saved this kit first. Review the current kit and save again.')
  if(kitState.id===selectionId&&kitState.name===requestedName){kitState.id=id;kitState.name=name}kitState.status=`Saved ${name}`
 }finally{kitState.saving=false}
}
let generation=0
export function cancelSoundSearch(){
 ++generation
 if(soundPreview.kit)kitState.status='Preview cancelled. Previous sounds restored.'
 else if(kitState.busy)kitState.status=''
 kitState.busy=false
 void cancelSoundPreview().catch(report)
}
export async function applySoundPreview(){
 if(!soundPreview.ready||!soundPreview.kit)return
 const kit=clone(soundPreview.kit),all=kitState.previewAll
 if(!await commitSoundPreview())return
 if(all){kitState.id='';kitState.name=kit.name}
 kitState.status=`Applied ${kit.name}`
}
function fingerprint(){return JSON.stringify([state.patch.id,soundRoots.value,state.patch.keys.map(key=>[key.note,key.name,key.parts,key.sound])])}
export async function matchSounds(query:string,note?:number){
 if(!query.trim())return
 if(!native())throw Error('Semantic sound matching is available in the native instrument.')
 cancelSoundSearch()
 const ticket=++generation;kitState.busy=true;kitState.status='Matching sounds…'
 try{
  await flushEdits();if(ticket!==generation)return
  const before=fingerprint(),keys=state.patch.keys.filter(key=>note===undefined||key.note===note)
  const assets:Asset[]=[...factoryAssets()]
  for(const field of savedKits.value)for(const key of (field.value as SoundKit).keys)assets.push({id:`${field.key}/${key.note}`,path:key.sound.path,name:`${field.value.name} ${key.sound.s.name}`,folder:false,size:0,preset:{sound:clone(key.sound),parts:[],routes:[]}})
  const result=await request<{assignments:{note:number;asset:Asset}[];sounds:number;limited:boolean}>('soundMatch',{query:query.trim(),roots:soundRoots.value,assets,keys:keys.map(key=>({note:key.note,name:key.name,part:partName(key.parts),parts:key.parts}))})
  if(ticket!==generation)return
  if(before!==fingerprint()){kitState.status='The instrument changed. Search again to match its current keywaves.';return}
  const kit:SoundKit={version:1,id:'search',name:query.trim(),keys:result.assignments.map(({note,asset})=>({note,sound:asset.preset?clone(asset.preset.sound):{path:asset.path,s:{...defaultSettings(asset.name),reverse:false}}}))}
  if(!kit.keys.length)throw Error('No matching sounds were found. Add a sound library folder or choose a saved kit.')
  kitState.previewAll=note===undefined
  if(!await previewSoundKit(kit,()=>ticket===generation&&before===fingerprint())){
   if(ticket===generation)kitState.status='The instrument changed. Search again to preview its current keywaves.'
   return
  }
  if(ticket===generation)kitState.status=`Previewing ${kit.keys.length} keywaves from ${result.sounds} sounds${result.limited?' · narrow the sound library to search additional files':''}`
 }catch(error){if(ticket===generation){kitState.status='No preview applied.';throw error}}finally{if(ticket===generation)kitState.busy=false}
}
