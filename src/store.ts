import { reactive, watch } from 'vue'
import { factory, normalize, clone, type Patch } from './model'
import { request, native, onNative } from './native/bridge'
export interface Finger {channel:number;note:number;zone:number;slide:number;glide:number;pressure:number}
export interface Playhead {id:number;note:number;pos:number;dur:number;velocity:number;grains:boolean}
export interface GrainMark {voice:number;when:number;pos:number;len:number;dur:number;pan:number;gain:number;reverse:boolean}
export const state = reactive({ patch: factory(), selected: 48, tab: 'Key', status: '', error: '', ready: false, standalone: false, saving: false, learn: -1, bindSource: -1, telemetry: { bpm: 120, ppq: 0, playing: false, freePlaying: false, revision: 0, voices: 0, peak: 0, clips: 0, overflow: false, macros: [0,0,0,0,0], output: .8, contacts: [] as number[], fingers: [] as Finger[], playheads: [] as Playhead[], grainMarks: [] as GrainMark[], audioTime: 0, slideChannel: -1, slideValue: 0, preview: false }, history: [] as string[], future: [] as string[] })
let applying = false, lastSaved = '', lastCheckpoint = '', inFlight = '', generation = 0, nativeRevision = 0, loading = false
let timer: ReturnType<typeof setTimeout> | undefined, draining: Promise<void> | undefined
type Pending = { document: Patch; generation: number }
let pending: Pending | undefined
export function report(e: unknown) { state.error = e instanceof Error ? e.message : String(e) }
export async function attempt<T>(job: () => Promise<T>): Promise<T | undefined> { try { return await job() } catch (e) { report(e) } }
function adopt(document: Patch) {
 applying = true
 try { state.patch = normalize(clone(document)); lastSaved = JSON.stringify(state.patch); lastCheckpoint = lastSaved } finally { applying = false }
}
export async function init() {
 if (native()) { const info = await request('ready'); adopt(info.document); state.standalone = info.standalone; nativeRevision = info.revision }
 else { state.status = 'Editor preview · audio and filesystem browsing run in the native instrument'; lastSaved = JSON.stringify(state.patch); lastCheckpoint = lastSaved }
 state.ready = true
}
watch(() => state.patch, () => {
 if (!state.ready || applying) return
 ++generation
 clearTimeout(timer)
 timer = setTimeout(() => void attempt(flushEdits), 200)
}, { deep: true, flush: 'sync' })
export async function flushEdits(recordHistory = true): Promise<void> {
 clearTimeout(timer); timer = undefined
 const snapshot = clone(state.patch), json = JSON.stringify(snapshot)
 if (json !== (pending ? JSON.stringify(pending.document) : inFlight || lastSaved)) {
  if (recordHistory && json !== lastCheckpoint) { state.history.push(lastCheckpoint); state.future = [] }
  lastCheckpoint = json
  pending = json === inFlight ? undefined : { document: snapshot, generation }
 }
 if (!draining && pending) {
  state.saving = true
  draining = drain().finally(() => { draining = undefined; inFlight = ''; state.saving = false })
 }
 await draining
}
async function drain() {
 while (pending) {
  const sent = pending; pending = undefined; inFlight = JSON.stringify(sent.document)
  const ack = native() ? await request('setDocument', { document: sent.document, revision: nativeRevision }) : { document: sent.document, revision: nativeRevision, conflict: false }
  nativeRevision = ack.revision
  if (ack.conflict) {
   clearTimeout(timer); timer = undefined; pending = undefined; ++generation
   adopt(ack.document); state.history = []; state.future = []
   state.status = 'The host restored a different patch. Its current state is now loaded.'
   return
  }
  lastSaved = JSON.stringify(normalize(clone(ack.document)))
  if (generation === sent.generation) adopt(ack.document)
  else {
   // Metadata from sample import can safely accompany newer knob edits.
   applying = true
   try {
    mergeAssets(state.patch, ack.document)
    const queued = pending as Pending | undefined
    if (queued) mergeAssets(queued.document, ack.document)
   } finally { applying = false }
  }
 }
}
function mergeAssets(target: Patch, canonical: Patch) {
 for (const key of target.keys) {
  const saved = canonical.keys.find(k => k.note === key.note && k.sound.path === key.sound.path)
  if (saved && (!key.sound.asset || key.sound.asset === saved.sound.asset)) {
   key.sound.asset = saved.sound.asset; key.sound.missing = saved.sound.missing
  }
 }
}
export async function replace(p: Patch) {
 if (!p || p.version !== 1 || !Array.isArray(p.keys) || !Array.isArray(p.clips)) throw Error('This is not a supported Roli Drummer patch')
 state.patch = normalize(clone(p)); state.selected = p.keys[0]?.note ?? 48
 await flushEdits()
}
export function undo(redo = false) {
 const from = redo ? state.future : state.history, to = redo ? state.history : state.future
 if (!from.length) return
 clearTimeout(timer); timer = undefined
 const json = from.pop()!; to.push(JSON.stringify(state.patch))
 applying = true
 try { state.patch = normalize(JSON.parse(json)); lastCheckpoint = json; ++generation } finally { applying = false }
 void attempt(() => flushEdits(false))
}
onNative('rdTelemetry', data => {
 const grains=state.telemetry.grainMarks
 Object.assign(state.telemetry, data)
 state.telemetry.grainMarks=[...grains,...(data.grainMarks??[])].filter(g=>state.telemetry.audioTime<=g.when+g.dur+.35).slice(-800)
 for (const m of data.midi ?? []) if (state.learn >= 0 && (m[0] & 240) === 176 && (m[0] & 15) === 0 && m[1] !== 74) { state.patch.globalCC[state.learn] = m[1]; state.learn = -1 }
 if (state.ready && !state.saving && !timer && data.revision > nativeRevision && !loading) {
  loading = true
  const before = generation
  void attempt(async () => {
   try {
    const info = await request('ready')
    if (generation === before && !state.saving) { nativeRevision = info.revision; adopt(info.document); state.history = []; state.future = [] }
   } finally { loading = false }
  })
 }
})
export function sendMidi(bytes: number[]) { if (native()) void request('midi', bytes).catch(report) }
export interface SharedField{key:string;value:any;revision:number}
export const shared=reactive<Record<string,SharedField>>({})
let revision=0,refreshing:Promise<void>|undefined
export function refreshShared():Promise<void>{
 if(!native())return Promise.resolve()
 if(refreshing)return refreshing
 refreshing=request<SharedField[]>('sharedSnapshot',revision).then(fields=>{
  for(const field of fields){if(field.revision>(shared[field.key]?.revision??0))shared[field.key]=field;revision=Math.max(revision,field.revision)}
 }).finally(()=>{refreshing=undefined})
 return refreshing
}
export async function writeShared<T>(key:string,value:T,expected:number){
 if(!native()){
  const current=shared[key]
  if((current?.revision??0)!==expected)return{ok:false,field:current as SharedField}
  const field={key,value,revision:expected+1};shared[key]=field;return{ok:true,field}
 }
 const result=await request('sharedCommit',[{key,value,revision:expected}])
 if(result.ok){
  const field={key,value,revision:result.revision}
  if(field.revision>(shared[key]?.revision??0))shared[key]=field
  return{ok:true,field}
 }
 // A previous poll may have started before the conflicting commit.
 await refreshShared();await refreshShared()
 state.status='Another instance updated this view; loaded its latest position.'
 return{ok:false,field:shared[key]}
}
export async function putShared(key:string,value:unknown){return(await writeShared(key,value,shared[key]?.revision??0)).ok}
setInterval(()=>void refreshShared().catch(report),350)
