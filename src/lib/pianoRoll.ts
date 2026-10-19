import type { RollNote } from '../performance'
export const blackPitch=(note:number)=>[1,3,6,8,10].includes(note%12)
export function pitchRange(notes:RollNote[]){
 let low=127,high=0
 for(const n of notes){low=Math.min(low,n.note);high=Math.max(high,n.note)}
 if(!notes.length)return{low:36,high:47}
 const missing=Math.max(0,11-(high-low));low=Math.max(0,low-Math.floor(missing/2));high=Math.min(127,Math.max(high,low+11));low=Math.min(low,high-11)
 return{low,high}
}
export function patternGeometry(notes:RollNote[],length:number,parts?:number[]){
 const visible=notes.filter(n=>Number.isInteger(n.note)&&n.note>=0&&n.note<=127&&Number.isFinite(n.beat)&&Number.isFinite(n.duration)&&n.duration>0&&(!parts||parts.includes(n.note)))
 const range=pitchRange(visible),row=100/(range.high-range.low+1),paths=Array.from({length:8},()=> '')
 if(!Number.isFinite(length)||length<=0)return{...range,row,paths,count:0}
 function rectangle(n:RollNote,start:number,duration:number){
  if(duration<=0)return
  const x=start/length*1000,y=(range.high-n.note)*row+row*.15,w=Math.max(.8,duration/length*1000),h=row*.7
  const bucket=Math.max(0,Math.min(7,Math.floor(n.velocity*8)))
  paths[bucket]+=`M${x} ${y}h${w}v${h}h${-w}Z`
 }
 for(const n of visible){const start=((n.beat%length)+length)%length,duration=Math.min(n.duration,length);rectangle(n,start,Math.min(duration,length-start));if(start+duration>length)rectangle(n,0,start+duration-length)}
 return{...range,row,paths,count:visible.length}
}
export function liveRectangle(note:{beat:number;end:number;note:number},origin:number,scale:number){return{x:(note.beat-origin)*scale,y:(127-note.note)*12+2,width:Math.max(.7,(note.end-note.beat)*scale),height:8}}
