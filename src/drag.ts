import {reactive} from 'vue'
import {state,attempt} from './store'
import {clone,factory,type Asset,type Key} from './model'
import {importPattern} from './midiImport'
import {synchronizeTomZones} from './lib/tomPatterns'
export const drag=reactive({asset:null as Asset|null,patterns:false,x:0,y:0,active:false})
export function begin(asset:Asset,patterns:boolean,event:MouseEvent,soundOnly=false){if(asset.folder||event.button!==0)return;drag.asset=asset;drag.patterns=patterns;drag.x=event.clientX;drag.y=event.clientY;const x=event.clientX,y=event.clientY;
 const move=(e:MouseEvent)=>{drag.x=e.clientX;drag.y=e.clientY;if(Math.hypot(e.clientX-x,e.clientY-y)>6)drag.active=true}
 const end=(e:MouseEvent)=>{window.removeEventListener('mousemove',move,true);window.removeEventListener('mouseup',end,true);if(drag.active){const el=document.elementFromPoint(e.clientX,e.clientY)?.closest('[data-drop]') as HTMLElement|null;if(el)void attempt(()=>drop(asset,patterns,el.dataset.drop!,soundOnly))}drag.active=false;drag.asset=null}
 window.addEventListener('mousemove',move,true);window.addEventListener('mouseup',end,true)
}
export async function drop(asset:Asset,patterns:boolean,destination:string,soundOnly=false){
 const [kind,id,index]=destination.split(':')
 if(patterns){
  if(kind!=='zone')throw Error('Drop MIDI patterns onto a colored zone.')
  const patch=state.patch,zones=id==='template'?patch.templateZones:patch.keys.find(k=>k.note===Number(id))?.zones,zone=zones?.[Number(index)]
  if(!zone)return
  const known=patch.clips.find(c=>c.id===asset.id)
  const imported=known ?? await importPattern(asset)
  if(!imported)return
  if(state.patch!==patch||!zones?.includes(zone))throw Error('The destination changed while choosing the pattern. Drop it onto a zone again.')
  // Reuse only an identical selection and content. Different tracks from the
  // same file remain separate choices, and edited MIDI files can be reimported.
  let clip=known || patch.clips.find(c=>c.id===imported.id||(c.path===imported.path&&c.channel===imported.channel&&c.track===imported.track&&c.length===imported.length&&JSON.stringify(c.notes)===JSON.stringify(imported.notes)))
  if(!clip){clip=imported;patch.clips.push(clip)}zone.clipId=clip.id
  delete zone.candidates
  if(patch.neural&&id!=='template'){const key=patch.keys.find(k=>k.note===Number(id));if(key)key.neuralLock=true;delete zone.role}
  const key=patch.keys.find(k=>k.note===Number(id));if(key)synchronizeTomZones(patch,key)
 }
 else{if(kind!=='key')throw Error('Drop sounds onto a Seaboard key.');const note=Number(id);let key=state.patch.keys.find(k=>k.note===note);const created=!key;if(!key){key=clone(state.patch.keys[0]||factory().keys[0]) as Key;key.note=note;key.name=asset.name;key.zones=clone(state.patch.templateZones);state.patch.keys.push(key)}key.sound.path=asset.path;delete key.sound.asset;delete key.sound.missing;key.sound.s.name=asset.name;if(asset.preset){key.sound=clone(asset.preset.sound);if(!soundOnly||created){key.routes=clone(asset.preset.routes);key.parts=clone(asset.preset.parts);key.mpeDefaultsVersion=1;key.name=asset.name}}state.selected=note}
}
export function htmlDrag(e:DragEvent,asset:Asset,patterns:boolean,soundOnly=false){e.dataTransfer?.setData('application/x-rolidrummer',JSON.stringify({asset,patterns,soundOnly}));if(e.dataTransfer)e.dataTransfer.effectAllowed='copy'}
export function htmlDrop(e:DragEvent,destination:string){e.preventDefault();const data=e.dataTransfer?.getData('application/x-rolidrummer');if(data)void attempt(async()=>{const {asset,patterns,soundOnly}=JSON.parse(data);await drop(asset,patterns,destination,Boolean(soundOnly))})}
