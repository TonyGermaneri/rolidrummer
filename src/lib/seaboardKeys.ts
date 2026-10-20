const accidentals=new Set([1,3,6,8,10])
export const keyBed={left:150,top:173,width:572,height:106,ribbon:10}
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
 if(y<keyBed.top-keyBed.ribbon||y>keyBed.top+keyBed.height+keyBed.ribbon)return
 return keys.find(key=>key.black&&x>=key.x&&x<key.x+key.w&&y>=keyBed.top&&y<=keyBed.top+key.h)
  ??keys.find(key=>!key.black&&x>=key.x&&x<key.x+key.w)
}
export function keySlide(key:SeaboardKey,y:number){
 if(y<=keyBed.top)return 1
 if(y>=keyBed.top+key.h)return 0
 return (keyBed.top+key.h-y)/key.h
}
