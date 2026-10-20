// Explicit bindings take precedence. Everything else repeats the ordered bank
// relative to its lowest pitch, including below it (positive modulo).
export function keyOrigins(keys:readonly {note:number}[]):number[]{
 const notes=[...new Set(keys.map(key=>key.note))].filter(note=>Number.isInteger(note)&&note>=0&&note<128).sort((a,b)=>a-b)
 const bound=new Set(notes),first=notes[0]
 return Array.from({length:128},(_,note)=>bound.has(note)?note:notes.length?notes[((note-first)%notes.length+notes.length)%notes.length]:-1)
}
export function compactKeyNotes(keys:{note:number}[]):Map<number,number>{
 const ordered=[...keys].sort((a,b)=>a.note-b.note),mapping=new Map<number,number>(),first=ordered[0]?.note??0
 for(const [index,key] of ordered.entries()){
  if(!Number.isInteger(key.note)||key.note<0||key.note>127||mapping.has(key.note))throw Error('Invalid or duplicate keywave pitch.')
  mapping.set(key.note,first+index)
 }
 for(const key of keys)key.note=mapping.get(key.note)!
 return mapping
}
