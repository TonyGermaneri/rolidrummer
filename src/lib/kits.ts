import {clone,type Patch,type Sound} from '../model'
export interface SoundKit {version:1;id:string;name:string;keys:{note:number;sound:Sound}[]}
export function captureSoundKit(patch:Patch,name:string,id:string):SoundKit{
 if(!name.trim())throw Error('Give this kit a name.')
 return{version:1,id,name:name.trim(),keys:patch.keys.map(key=>({note:key.note,sound:clone(key.sound)}))}
}
export function withSoundKit(patch:Patch,kit:SoundKit):Patch{
 if(kit.version!==1||!Array.isArray(kit.keys)||!kit.keys.length)throw Error('This is not a supported sound kit.')
 const sounds=new Map<number,Sound>()
 for(const key of kit.keys){
  if(!Number.isInteger(key.note)||key.note<0||key.note>127||sounds.has(key.note)||typeof key.sound?.path!=='string'||!key.sound.s||typeof key.sound.s!=='object')throw Error('This kit contains an invalid keywave sound.')
  sounds.set(key.note,key.sound)
 }
 const next=clone(patch)
 for(const key of next.keys){const sound=sounds.get(key.note);if(sound)key.sound=clone(sound)}
 for(const [note,sound] of sounds)if(!next.keys.some(key=>key.note===note))next.keys.push({note,name:sound.s.name||`Keywave ${note}`,sound:clone(sound),parts:[],zones:[{upper:1,clipId:''}],zoneSource:2,routes:[],mpeDefaultsVersion:1})
 return next
}
