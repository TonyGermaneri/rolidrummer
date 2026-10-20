const accidentals=new Set([1,3,6,8,10])
export const keyBed={left:150,top:175,width:572,height:97,surfaceTop:163,surfaceBottom:288}
export type RibbonAction='top'|'stop'
export function ribbonAtPoint(x:number,y:number):RibbonAction|undefined{
 if(x<keyBed.left||x>=keyBed.left+keyBed.width)return
 if(y>=keyBed.surfaceTop&&y<keyBed.top)return 'top'
 if(y>keyBed.top+keyBed.height&&y<=keyBed.surfaceBottom)return 'stop'
}
export function seaboardKeys(base:number){
 const notes=Array.from({length:Math.min(49,128-base)},(_,i)=>base+i)
 const whiteWidth=keyBed.width/notes.filter(note=>!accidentals.has(note%12)).length
 let white=0
 return notes.map(note=>{
  const black=accidentals.has(note%12),x=keyBed.left+(black?white-.31:white)*whiteWidth
  const key={note,black,x,w:black?.62*whiteWidth:whiteWidth,h:black?keyBed.height*.57:keyBed.height}
  if(!black)++white
  return key
 })
}
export type SeaboardKey=ReturnType<typeof seaboardKeys>[number]
export function keyAtPoint(keys:SeaboardKey[],x:number,y:number){
 if(y<keyBed.top||y>keyBed.top+keyBed.height)return
 return keys.find(key=>key.black&&x>=key.x&&x<key.x+key.w&&y>=keyBed.top&&y<=keyBed.top+key.h)
  ??keys.find(key=>!key.black&&x>=key.x&&x<key.x+key.w)
}
export function keySlide(key:SeaboardKey,y:number){
 if(y<=keyBed.top)return 1
 if(y>=keyBed.top+key.h)return 0
 return (keyBed.top+key.h-y)/key.h
}

export function ribbonLanes(keys:SeaboardKey[]){
 const ordered=[...keys].sort((a,b)=>a.note-b.note)
 return ordered.map((key,i)=>{const center=key.x+key.w/2,previous=ordered[i-1],next=ordered[i+1];const x=previous?(previous.x+previous.w/2+center)/2:keyBed.left;const right=next?(center+next.x+next.w/2)/2:keyBed.left+keyBed.width;return {...key,x,w:right-x}})
}
export function surfaceAtPoint(keys:SeaboardKey[],x:number,y:number){
 const action=ribbonAtPoint(x,y)
 if(action){const lane=ribbonLanes(keys).find(key=>x>=key.x&&x<key.x+key.w);if(lane)return {key:keys.find(key=>key.note===lane.note)!,area:action}}
 const key=keyAtPoint(keys,x,y);if(key)return {key,area:'key' as const}
}
