import { reactive, ref, onMounted, onBeforeUnmount } from 'vue'

export interface RollNote { beat:number;duration:number;note:number;velocity:number }
export interface PlayedNote extends RollNote { key:number;owner:string;token:string;end:number }
export interface PatternView { identity:string;key:number;known:boolean;name:string;length:number;notes:RollNote[];totalNotes?:number }
export interface PatternPosition { identity:string;key:number;phase:number;playing:boolean;zone:number;candidate:number }
export interface PlayedEvent { type:number;epoch:number;beat:number;duration:number;key:number;note:number;velocity:number;owner:string;token:string }
export const performanceView=reactive({epoch:-1,beat:0,bpm:120,received:0,dropped:0,notes:[] as PlayedNote[],patterns:{} as Record<number,{pattern:PatternView;position:PatternPosition;received:number}>})
export const historyLimit=8192
const now=()=>typeof performance==='undefined'?Date.now():performance.now()
let patternVersion=''
export function clearPerformance(){performanceView.notes=[]}
export function acceptPerformance(data:{performance?:{epoch:number;beat:number;bpm:number;dropped:number;events:PlayedEvent[]};pattern?:PatternView;patternPosition?:PatternPosition}){
 const frame=data.performance
 const received=now()
 if(frame&&Number.isFinite(frame.beat)&&frame.epoch>=performanceView.epoch){
  if(frame.epoch!==performanceView.epoch){clearPerformance();performanceView.epoch=frame.epoch;performanceView.patterns={}}
  performanceView.beat=frame.beat;performanceView.bpm=frame.bpm;performanceView.received=received;performanceView.dropped=frame.dropped
  // Index only potentially active notes. Dense output must not scan the entire
  // retained history once for every note-off in a telemetry batch.
  let earliest=frame.beat
  for(const event of frame.events??[])if(event.epoch===frame.epoch&&Number.isFinite(event.beat))earliest=Math.min(earliest,event.beat)
  const tokens=new Map<string,PlayedNote[]>(),owners=new Map<string,PlayedNote[]>()
  function index(note:PlayedNote){for(const [map,id] of [[tokens,note.token],[owners,note.owner]] as const){const list=map.get(id);if(list)list.push(note);else map.set(id,[note])}}
  for(const note of performanceView.notes)if(note.end>=earliest)index(note)
  for(const event of frame.events??[]){
   if(event.epoch!==frame.epoch||!Number.isFinite(event.beat))continue
   if(event.type===0){
    if(!Number.isInteger(event.note)||event.note<0||event.note>127||!Number.isFinite(event.duration)||event.duration<=0)continue
    performanceView.notes.push({...event,duration:event.duration,end:event.beat+event.duration})
    index(performanceView.notes[performanceView.notes.length-1])
   }else {
    const ending=event.type===1?tokens.get(event.token):event.type===2?owners.get(event.owner):event.type===3?performanceView.notes:undefined
    for(const note of ending??[])note.end=Math.min(note.end,Math.max(note.beat,event.beat))
   }
  }
  performanceView.notes=performanceView.notes.filter(note=>note.end>frame.beat-2048).slice(-historyLimit)
 }
 const position=data.patternPosition,pattern=data.pattern
 if(position){
  const version=position.identity.split(':')[0]
  if(version!==patternVersion){performanceView.patterns={};patternVersion=version}
  if(pattern&&pattern.identity===position.identity)performanceView.patterns[pattern.key]={pattern,position,received}
  else {const previous=performanceView.patterns[position.key];if(previous?.pattern.identity===position.identity){previous.position=position;previous.received=received}}
 }
}
// One shared animation clock, including all miniature views. Stop when unused.
const animationTime=ref(0)
let users=0,animation=0
function animate(time:number){animationTime.value=time;animation=requestAnimationFrame(animate)}
export function useRollClock(){
 onMounted(()=>{if(++users===1&&typeof requestAnimationFrame==='function')animation=requestAnimationFrame(animate)})
 onBeforeUnmount(()=>{if(--users===0&&typeof cancelAnimationFrame==='function')cancelAnimationFrame(animation)})
 return animationTime
}
export function elapsedBeats(time:number,received:number,bpm:number){return Math.min(.1,Math.max(0,(time-received)/1000))*bpm/60}
