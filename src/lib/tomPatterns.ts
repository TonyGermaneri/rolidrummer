import type {Key,Patch} from '../model'
const notes=new Set([41,43,45,47,48,50])
export const isTomKey=(key:Pick<Key,'parts'>)=>key.parts.length>0&&key.parts.every(note=>notes.has(note))
const copy=<T>(value:T):T=>JSON.parse(JSON.stringify(value))
// A zone is one whole tom performance, with each key's existing MIDI mask
// extracting its own drum. Whole-kit and mixed masks remain independent.
export function synchronizeTomZones(patch:Patch,source?:Key){
 const toms=patch.keys.filter(isTomKey)
 if(toms.length<2||source&&!isTomKey(source))return
 source??=toms.slice().sort((a,b)=>Number(Boolean(b.neuralLock))-Number(Boolean(a.neuralLock))||a.note-b.note)[0]
 const zones=copy(source.zones)
 for(const zone of zones)delete zone.candidates
 for(const key of toms){
  if(JSON.stringify(key.zones)!==JSON.stringify(zones))key.zones=copy(zones)
  key.zoneSource=source.zoneSource
  key.neuralLock=Boolean(source.neuralLock)
  if(key.patternIntent)key.patternIntent.enabled=false
 }
}
