import { reactive, watch } from 'vue'
import { factory, normalize, clone, type Patch } from './model'
import { request, native, onNative } from './native/bridge'
import { acceptPerformance } from './performance'
export interface Finger {owner?:string;sustained?:boolean;toggled?:boolean;playing?:boolean;channel:number;note:number;zone:number;slide:number;glide:number;pressure:number;candidate?:number}
export interface Playhead {id:number;note:number;pos:number;dur:number;velocity:number;grains:boolean}
export interface GrainMark {voice:number;when:number;pos:number;len:number;dur:number;pan:number;gain:number;reverse:boolean}
export const state = reactive({ patch: factory(), selected: 48, trackKeywave: false, lastTouchedKey: -1, tab: 'Key', settingsSection: 'libraries', status: '', error: '', ready: false, standalone: false, saving: false, learn: -1, bindSource: -1, telemetry: { bpm: 120, ppq: 0, playing: false, freePlaying: false, revision: 0, voices: 0, peak: 0, clips: 0, overflow: false, macros: [0,0,0,0,0], output: .8, contacts: [] as number[], fingers: [] as Finger[], playheads: [] as Playhead[], grainMarks: [] as GrainMark[], audioTime: 0, slideChannel: -1, slideValue: 0, preview: false, program: -1, programFailed: false, programUnassigned: false, activationPending: false }, history: [] as string[], future: [] as string[] })
let applying = false, lastSaved = '', lastCheckpoint = '', inFlight = '', generation = 0, nativeRevision = 0, loading = false, legacyIdentityName = ''
let timer: ReturnType<typeof setTimeout> | undefined, draining: Promise<void> | undefined
type Pending = { document: Patch; generation: number; activationGrid: number; preserveActivation: boolean }
let inFlightBoundary = false
let pending: Pending | undefined
export function report(e: unknown) { state.error = e instanceof Error ? e.message : String(e) }
export async function attempt<T>(job: () => Promise<T>): Promise<T | undefined> { try { return await job() } catch (e) { report(e) } }
function adoptLegacyIdentity(){
 if(state.patch.id||!legacyIdentityName)return
 const matches=Object.values(shared).filter(field=>field.key.startsWith('patch/')&&field.value?.id&&field.value.name===legacyIdentityName)
 if(matches.length===1)state.patch.id=matches[0].value.id
}
function adopt(document: Patch,keepIdentityName=false) {
 applying = true
 try { legacyIdentityName=document.id?'':keepIdentityName&&legacyIdentityName?legacyIdentityName:document.name;state.patch = normalize(clone(document));adoptLegacyIdentity();lastSaved = JSON.stringify(state.patch); lastCheckpoint = lastSaved; if(!state.patch.keys.some(k=>k.note===state.selected))state.selected=state.patch.keys[0]?.note??48 } finally { applying = false }
}
export async function init() {
 if (native()) { const info = await request('ready'); adopt(info.document); state.standalone = info.standalone; nativeRevision = info.revision; state.telemetry.activationPending = Boolean(info.activationPending) }
 else { state.status = 'Editor preview · audio and filesystem browsing run in the native instrument'; lastSaved = JSON.stringify(state.patch); lastCheckpoint = lastSaved }
 state.ready = true
}
watch(() => state.patch, () => {
 if (!state.ready || applying) return
 ++generation
 clearTimeout(timer)
 timer = setTimeout(() => void attempt(flushEdits), 200)
}, { deep: true, flush: 'sync' })
export async function flushEdits(recordHistory = true, activationGrid?: number): Promise<void> {
 clearTimeout(timer); timer = undefined
 const snapshot = clone(state.patch), json = JSON.stringify(snapshot)
 const explicit = activationGrid !== undefined
 const policy = explicit ? { activationGrid, preserveActivation: false } : pending ? { activationGrid: pending.activationGrid, preserveActivation: pending.preserveActivation } : { activationGrid: 0, preserveActivation: true }
 // An explicit load also cancels a prepared refresh with the same document.
 // Returning to an immediate in-flight edit can still discard a queued edit.
 const force = explicit && (activationGrid > 0 || !inFlight || inFlightBoundary)
 if (json !== (pending ? JSON.stringify(pending.document) : inFlight || lastSaved) || force) {
  if (recordHistory && json !== lastCheckpoint) { state.history.push(lastCheckpoint); state.future = [] }
  lastCheckpoint = json
  pending = json === inFlight && !force ? undefined : { document: snapshot, generation, ...policy }
 }
 if (!draining && pending) {
  state.saving = true
  draining = drain().finally(() => { draining = undefined; inFlight = ''; inFlightBoundary = false; state.saving = false })
 }
 await draining
}
async function drain() {
 while (pending) {
  const sent = pending; pending = undefined; inFlight = JSON.stringify(sent.document); inFlightBoundary = sent.activationGrid > 0 || sent.preserveActivation && state.telemetry.activationPending
  const ack = native() ? await request('setDocument', { document: sent.document, revision: nativeRevision, activationGrid: sent.activationGrid, preserveActivation: sent.preserveActivation }) : { document: sent.document, revision: nativeRevision, conflict: false, activationPending: false }
  nativeRevision = ack.revision
  if (typeof ack.activationPending === 'boolean') state.telemetry.activationPending = ack.activationPending
  if (ack.conflict) {
   clearTimeout(timer); timer = undefined; pending = undefined; ++generation
   adopt(ack.document); state.history = []; state.future = []
   state.status = 'The host restored a different patch. Its current state is now loaded.'
   return
  }
  lastSaved = JSON.stringify(normalize(clone(ack.document)))
  if (generation === sent.generation) adopt(ack.document,true)
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
export async function replace(p: Patch, activation: 'bar' | 'immediate' = 'immediate') {
 if (!p || p.version !== 1 || !Array.isArray(p.keys) || !Array.isArray(p.clips)) throw Error('This is not a supported Roli Drummer patch')
 if(p.neural&&p.neural.version!==1)throw Error('This neural patch version is not supported')
 legacyIdentityName=p.id?'':p.name;state.patch = normalize(clone(p)); state.selected = p.keys[0]?.note ?? 48
 const numerator=p.neural?.numerator??4,denominator=p.neural?.denominator??4
 await flushEdits(true,activation==='bar'?numerator*4/denominator:0)
}
export function undo(redo = false) {
 const from = redo ? state.future : state.history, to = redo ? state.history : state.future
 if (!from.length) return
 clearTimeout(timer); timer = undefined
 const json = from.pop()!; to.push(JSON.stringify(state.patch))
 applying = true
 try { state.patch = normalize(JSON.parse(json));legacyIdentityName=state.patch.id?'':state.patch.name;lastCheckpoint = json; ++generation } finally { applying = false }
 void attempt(() => flushEdits(false,0))
}
export function observeKeyTouch(bytes:number[]){
 if((bytes[0]&240)!==144||!(bytes[2]>0)||!state.patch.keys.some(key=>key.note===bytes[1]))return
 state.lastTouchedKey=bytes[1]
 if(state.trackKeywave)state.selected=bytes[1]
}
watch(()=>state.trackKeywave,enabled=>{if(enabled&&state.lastTouchedKey>=0&&state.patch.keys.some(key=>key.note===state.lastTouchedKey))state.selected=state.lastTouchedKey},{flush:'sync'})
onNative('rdTelemetry', data => {
 const {performance,pattern,patternPosition,...telemetry}=data
 acceptPerformance({performance,pattern,patternPosition})
 for(const bytes of data.midi??[])observeKeyTouch(bytes)
 const grains=state.telemetry.grainMarks
 if(data.programFailed&&!state.telemetry.programFailed)state.error='Could not load the MIDI-selected patch. Check the saved patch or select it again.'
 if(data.programUnassigned&&!state.telemetry.programUnassigned)state.status=`MIDI program ${data.program} is unassigned; the current patch is still playing.`
 Object.assign(state.telemetry, telemetry)
 state.telemetry.grainMarks=[...grains,...(data.grainMarks??[])].filter(g=>state.telemetry.audioTime<=g.when+g.dur+.35).slice(-800)
 for (const m of data.midi ?? []) if (state.learn >= 0 && (m[0] & 240) === 176 && m[1] !== 74 && m[1] < 120) {
  if(state.learn===5)state.patch.sustainCC=m[1]
  else if((m[0]&15)===0 && m[1]!==state.patch.sustainCC)state.patch.globalCC[state.learn]=m[1]
  else continue
  state.learn=-1
 }
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
export function sendMidi(bytes: number[]) { observeKeyTouch(bytes); if (native()) void request('midi', bytes).catch(report) }
export interface SharedField{key:string;value:any;revision:number}
export const shared=reactive<Record<string,SharedField>>({})
let revision=0,refreshing:Promise<void>|undefined
export function refreshShared():Promise<void>{
 if(!native())return Promise.resolve()
 if(refreshing)return refreshing
 refreshing=request<SharedField[]>('sharedSnapshot',revision).then(fields=>{
  for(const field of fields){if(field.revision>(shared[field.key]?.revision??0))shared[field.key]=field;revision=Math.max(revision,field.revision)}
  adoptLegacyIdentity()
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
  const fields:SharedField[]=result.fields??[{key,value,revision:result.revision}]
  for(const field of fields)if(field.revision>=(shared[field.key]?.revision??0))shared[field.key]=field
  return{ok:true,field:fields.find(field=>field.key===key)??shared[key]}
 }
 // A previous poll may have started before the conflicting commit.
 await refreshShared();await refreshShared()
 state.status='Another instance updated this view; loaded its latest position.'
 return{ok:false,field:shared[key]}
}
export async function putShared(key:string,value:unknown){return(await writeShared(key,value,shared[key]?.revision??0)).ok}
export async function saveSharedPatch(copy=false,options:{newPatch?:boolean;name?:string}={}){
 if(!copy&&!state.patch.id){const before=state.patch;await refreshShared();if(state.patch!==before)throw Error('The patch changed while preparing its saved identity. Save the current patch again.')}
 const before=state.patch,document=clone(before),originalName=before.name
 if(options.name?.trim())document.name=options.name.trim()
 const saved=Object.values(shared).filter(field=>field.key.startsWith('patch/')&&field.value)
 const matches=saved.filter(field=>field.value.name===document.name)
 const existing=copy||options.newPatch?undefined:document.id&&document.id!=='builtin:factory'?saved.find(field=>field.value.id===document.id):matches.length===1?matches[0]:undefined
 document.id=existing?.value.id??(!copy&&!options.newPatch&&document.id&&document.id!=='builtin:factory'?document.id:`patch-${crypto.randomUUID()}`)
 if(options.newPatch){const name=document.name;let suffix=2;while(saved.some(field=>field.value.name===document.name))document.name=`${name} ${suffix++}`}
 if(copy){const name=document.name;let suffix=1;do{document.name=`${name} copy${suffix>1?` ${suffix}`:''}`;++suffix}while(saved.some(field=>field.value.name===document.name))}
 const key=existing?.key??`patch/${document.id}`
 const result=await writeShared(key,document,shared[key]?.revision??0)
 if(result.ok&&state.patch===before){state.patch.id=result.field.value.id;if((copy||options.newPatch)&&state.patch.name===originalName)state.patch.name=document.name}
 const slots=shared['midi/program-bank']?.value.slots as (string|null)[]|undefined
 return{ok:result.ok,program:result.ok?slots?.indexOf(result.field.value.id)??-1:-1,name:document.name}
}
setInterval(()=>void refreshShared().catch(report),350)
