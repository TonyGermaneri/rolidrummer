<script setup lang="ts">
import {computed,onBeforeUnmount,onMounted,ref,watch} from 'vue'
import DataGrid from './DataGrid.vue'
import {request,native} from '../native/bridge'
import {state,attempt,replace,flushEdits,saveSharedPatch} from '../store'
import {clone,type Patch} from '../model'
import {matcher as draft} from '../matcher'
import {begin,drop} from '../drag'
import {preview,audition,stopPreview} from '../preview'
import {libraryAsset,type LibraryPage,type LibraryRow,type LibraryStatus,type NeuralPage} from '../library'
const status=ref<LibraryStatus>({catalog:{revision:0,patterns:0,sources:[]},progress:{state:'idle'}})
const page=ref<LibraryPage>({rows:[],offset:0,more:false,revision:0,patterns:0})
const chosen=ref<LibraryRow|null>(null),zone=ref(0),role=ref(''),searchMode=ref('semantic')
const searchInfo=ref(''),loading=ref(false),resolving=ref(false),saving=ref(false),failure=ref('')
const ready=computed(()=>Boolean(status.value.neural?.readyIndexId))
const availableMode=computed({get:()=>ready.value?searchMode.value:'description',set:(value:string)=>{searchMode.value=value}})
const selected=computed(()=>state.patch.keys.find(k=>k.note===state.selected))
const rows=computed(()=>page.value.rows.map(row=>({...row,meter:`${row.numerator}/${row.denominator}`,coverageLabel:`${Math.round(row.coverage*100)}%`,similarityLabel:row.similarity===undefined?'—':row.similarity.toFixed(3),preview:preview.id===`library:${row.id}`?'■':'▶',assign:'ASSIGN'})))
const columns=[{name:'preview',title:'Listen',width:58},{name:'assign',title:'Zone',width:75},{name:'name',title:'Pattern',width:230},{name:'role',title:'Role',width:80},{name:'meter',title:'Meter',width:65},{name:'coverageLabel',title:'Mapped',width:75},{name:'similarityLabel',title:'Similarity',width:80},{name:'description',title:'Musical description',width:420}]
const clipNames=computed(()=>new Map(state.patch.clips.map(clip=>[clip.id,clip.name])))
const boundKeys=computed(()=>state.patch.keys.filter(key=>key.parts.length&&key.zones.some(zone=>zone.clipId)))
const currentResolution=computed(()=>{
 const value=state.patch.neural
 return value?.query===draft.query.trim()&&String(value.seed??'0')===draft.seed&&(value.autoMode??true)===draft.autoMode
  &&(value.variationCount??0)===draft.variationCount&&(draft.autoMode||(value.phraseBars??8)===draft.phraseBars)
})
const resultText=computed(()=>failure.value|| (resolving.value?'Finding your next groove…':state.telemetry.activationPending?'Ready · plays from the next bar':currentResolution.value?'Ready to play':draft.query.trim()?'Search ready':'Describe a style or groove to begin'))
let disposed=false,polling=false,applying=false,readTicket=0,resolveTicket=0,queued=false
let poll:ReturnType<typeof setInterval>|undefined,debounce:ReturnType<typeof setTimeout>|undefined,work:Promise<void>|undefined
function source(){const n=state.patch.neural;return JSON.stringify([state.patch.id??state.patch.name,n?.query,n?.seed,n?.autoMode,n?.variationCount,n?.phraseBars])}
function adoptDraft(){
 if(applying||draft.source===source())return
 const n=state.patch.neural
 applying=true
 try{Object.assign(draft,{source:source(),autoMode:n?.autoMode??true,query:n?.query??'',seed:String(n?.seed??'0'),phraseBars:n?.phraseBars??8,variationCount:n?.variationCount??0})}finally{applying=false}
}
adoptDraft()
watch(source,()=>{if(!applying){++resolveTicket;clearTimeout(debounce);adoptDraft()}},{flush:'sync'})
function intent(){return JSON.stringify([draft.query.trim(),draft.seed,draft.autoMode,draft.phraseBars,draft.variationCount])}
function schedule(){
 if(applying||disposed)return
 ++resolveTicket;failure.value='';clearTimeout(debounce)
 if(draft.autoMode&&draft.query.trim()&&ready.value)debounce=setTimeout(()=>void attempt(resolveLatest),350)
}
watch(()=>[draft.query,draft.seed,draft.autoMode,draft.variationCount],schedule,{flush:'sync'})
async function load(offset=0){
 if(!native()||draft.autoMode)return
 const ticket=++readTicket;loading.value=true
 try{
  const semantic=searchMode.value==='semantic'&&ready.value&&Boolean(draft.query.trim())
  const result=semantic?await request<NeuralPage>('librarySearch',{query:draft.query,role:role.value,indexId:status.value.neural?.readyIndexId,limit:50}):await request<LibraryPage>('libraryBrowse',{query:draft.query,role:role.value,offset,limit:50})
  if(disposed||ticket!==readTicket)return
  if(semantic){const found=result as NeuralPage;page.value={...found,offset:0,more:false,patterns:status.value.catalog.patterns};searchInfo.value=`${found.rows.length} matches${found.interpretedQuery?` · ${found.interpretedQuery}`:''}`}
  else{page.value=result as LibraryPage;searchInfo.value='Description filter'}
  chosen.value=result.rows.find(row=>row.id===chosen.value?.id)??null
 }finally{if(ticket===readTicket)loading.value=false}
}
async function refresh(){
 if(!native()||disposed||polling)return
 polling=true
 try{
  const result=await request<LibraryStatus>('libraryStatus');if(disposed)return
  const changed=result.catalog.revision!==status.value.catalog.revision||result.neural?.readyIndexId!==status.value.neural?.readyIndexId
  status.value=result
  if(changed){if(draft.autoMode&&!currentResolution.value)schedule();else if(!draft.autoMode)await load()}
 }finally{polling=false}
}
async function drain(){
 resolving.value=true
 try{while(queued&&!disposed){
  queued=false
  const ticket=resolveTicket,stamp=intent(),before=state.patch,document=clone(before),unchanged=JSON.stringify(document)
  const options={document,query:draft.query.trim(),indexId:status.value.neural!.readyIndexId,seed:draft.seed,autoMode:draft.autoMode,variationCount:draft.variationCount,...(draft.autoMode?{}:{phraseBars:draft.phraseBars})}
  let result:Patch
  try{result=await request<Patch>('libraryResolve',options)}catch(error){if(disposed||ticket!==resolveTicket||stamp!==intent())continue;throw error}
  if(disposed||ticket!==resolveTicket||stamp!==intent())continue
  if(state.patch!==before||JSON.stringify(state.patch)!==unchanged)throw Error('The patch changed during resolution. Resolve again to apply to the current patch.')
  applying=true
  try{await replace(result,'bar');draft.source=source()}finally{applying=false}
 }}catch(error){failure.value=error instanceof Error?error.message:String(error);throw error}
 finally{resolving.value=false}
}
function resolveLatest():Promise<void>{
 clearTimeout(debounce)
 if(disposed||!draft.query.trim()||!ready.value)return Promise.resolve()
 failure.value='';++resolveTicket;queued=true
 if(!work)work=drain().finally(()=>{work=undefined})
 return work
}
function search(){return draft.autoMode?resolveLatest():load()}
function toggleAuto(){draft.autoMode=!draft.autoMode}
function reroll(){
 const value=crypto.getRandomValues(new Uint32Array(1))[0]!
 draft.seed=String(value)===draft.seed?String((value+1)>>>0):String(value)
 return resolveLatest()
}
async function save(){
 if(saving.value||!draft.query.trim())return
 saving.value=true
 try{
  if(draft.autoMode&&!currentResolution.value){await resolveLatest();if(!currentResolution.value)throw Error('Wait for the current search before saving.')}
  const stamp=intent(),origin=source()
  await flushEdits()
  if(stamp!==intent()||origin!==source())throw Error('The search changed before saving. Save the current groove again.')
  applying=true
  try{const result=await saveSharedPatch(false,{newPatch:true,name:draft.query.trim()});if(!result.ok)throw Error('Could not save the patch. Try again.');draft.source=source();await flushEdits();state.status=`Saved “${result.name}”`}
  finally{applying=false}
 }finally{saving.value=false}
}
async function assign(row=chosen.value){if(row&&selected.value)await drop(libraryAsset(row),true,`zone:${selected.value.note}:${Math.min(zone.value,selected.value.zones.length-1)}`)}
watch(()=>draft.autoMode,auto=>{if(!auto)void attempt(()=>load())})
onMounted(()=>{void attempt(async()=>{await refresh();if(!disposed&&!draft.autoMode&&!page.value.rows.length)await load()});poll=setInterval(()=>void attempt(refresh),1500)})
onBeforeUnmount(()=>{disposed=true;++readTicket;++resolveTicket;clearInterval(poll);clearTimeout(debounce);if(preview.id.startsWith('library:'))void attempt(stopPreview)})
</script>
<template><section class="matcher-workspace workspace-ui">
 <div class="matcher-toolbar">
  <button class="auto-switch" aria-label="Auto mode" :aria-pressed="draft.autoMode" @click="toggleAuto"><span class="switch-light"/>AUTO</button>
  <label class="seed-control">SEED<input v-model="draft.seed" aria-label="Pattern variation seed" maxlength="128" spellcheck="false" @keydown.enter="attempt(resolveLatest)"/></label>
  <button class="icon-button die-button" aria-label="Reroll seed" title="New seed and groove" :disabled="saving" @click="attempt(reroll)"><svg viewBox="0 0 24 24" aria-hidden="true"><rect x="4" y="4" width="16" height="16" rx="3"/><circle cx="8" cy="8" r="1"/><circle cx="16" cy="8" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="8" cy="16" r="1"/><circle cx="16" cy="16" r="1"/></svg></button>
  <div class="query-control"><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="10" cy="10" r="6"/><path d="m15 15 5 5"/></svg><input v-model="draft.query" aria-label="Filter indexed patterns" maxlength="256" placeholder="Describe a groove…" spellcheck="false" @keydown.enter="attempt(search)"/></div>
  <select v-model.number="draft.variationCount" class="generation-choice" aria-label="Pattern generation" :title="draft.autoMode?'Generated tracks for the whole kit':'Generated variations per region'"><option :value="0">Patterns Only</option><option v-for="count in [1,2,3,4]" :key="count" :value="count">{{count}} Generative</option></select>
  <button class="icon-button save-search" aria-label="Save search as patch" title="Save this groove as a patch" :disabled="saving||resolving||!draft.query.trim()||(draft.autoMode&&!ready)" @click="attempt(save)"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 3h13l4 4v14H3V3h1Z"/><path d="M7 3v6h9V3M7 21v-8h10v8"/></svg><span>{{saving?'SAVING':'SAVE'}}</span></button>
 </div>
 <div v-if="!draft.autoMode" class="manual-toolbar">
  <select v-model="availableMode" aria-label="Pattern search mode" @change="attempt(()=>load())"><option value="semantic" :disabled="!ready">Semantic similarity</option><option value="description">Description filter</option></select>
  <select v-model="role" aria-label="Pattern role" @change="attempt(()=>load())"><option value="">All roles</option><option value="groove">Grooves</option><option value="sparse">Sparse</option><option value="fill">Fills</option><option value="intro">Intros</option><option value="ending">Endings</option></select>
  <label>ZONE<select v-model.number="zone" aria-label="Assignment zone"><option v-for="(_,i) in selected?.zones??[]" :key="i" :value="i">{{i+1}}</option></select></label>
  <button :disabled="loading" @click="attempt(()=>load())">SEARCH</button>
  <span class="toolbar-spacer"/>
  <label>PHRASE<select v-model.number="draft.phraseBars" aria-label="Phrase cycle bars"><option v-for="bars in [1,2,3,4,6,8,12,16]" :key="bars" :value="bars">{{bars}} bars</option></select></label>
  <button :disabled="resolving||!draft.query.trim()||!ready" @click="attempt(resolveLatest)">{{resolving?'RESOLVING…':'RESOLVE PATCH'}}</button>
 </div>
 <p v-if="!native()" class="workspace-empty">Open the native instrument to match your pattern library.</p>
 <div v-else-if="draft.autoMode" class="auto-workspace">
  <div v-if="!ready" class="workspace-empty"><strong>Prepare your pattern library</strong><button @click="state.settingsSection='libraries';state.tab='Settings'">Open Settings</button></div>
  <div v-else-if="!state.patch.neural" class="workspace-empty"><strong>Your next groove starts here</strong><span>Enter a style, then play. Change the seed to explore.</span></div>
  <template v-else>
   <div class="groove-heading"><h2>{{state.patch.name}}</h2><span>{{state.patch.neural.numerator}}/{{state.patch.neural.denominator}}</span><span>{{state.patch.neural.feel}}</span><span class="toolbar-spacer"/><small>{{boundKeys.length}} keys</small></div>
   <div class="auto-zone-map" aria-label="Automatic zone assignments"><div class="auto-zone-head"><span>DRUM PART</span><span>SPARSE</span><span>STEADY</span><span>FILL</span></div><div v-for="key in boundKeys" :key="key.note" class="auto-zone-row"><strong>{{key.name}}</strong><span v-for="(zone,i) in key.zones" :key="i" :title="clipNames.get(zone.clipId)??'Silent'" :class="`region-${i}`">{{clipNames.get(zone.clipId)??'Silent'}}</span></div></div>
  </template>
 </div>
 <template v-else>
  <div class="library-grid"><DataGrid :rows="rows" :columns="columns" :chosen="chosen||undefined" @pick="chosen=$event" @use="row=>attempt(()=>assign(row))" @cell="(column,row)=>column==='preview'?attempt(()=>audition(libraryAsset(row),true)):column==='assign'?attempt(()=>assign(row)):undefined" @drag="(row,event)=>begin(libraryAsset(row),true,event)"/><div v-if="loading" class="library-busy">Searching…</div><div v-else-if="!rows.length" class="library-busy">{{status.catalog.patterns?'No matching patterns':'Add pattern libraries in Settings'}}</div></div>
  <footer class="manual-footer"><span>{{searchInfo||`${status.catalog.patterns.toLocaleString()} patterns`}}</span><button :disabled="loading||page.offset===0" @click="attempt(()=>load(Math.max(0,page.offset-50)))">Previous</button><span>{{page.offset+Number(rows.length>0)}}–{{page.offset+rows.length}}</span><button :disabled="loading||!page.more" @click="attempt(()=>load(page.offset+50))">Next</button></footer>
  <details v-if="state.patch.neural?.warnings.length" class="part-fallbacks"><summary>Part fallbacks · {{state.patch.neural.warnings.length}}</summary><p v-for="(warning,i) in state.patch.neural.warnings" :key="i">{{state.patch.keys.find(key=>key.note===warning.key)?.name??warning.key}} · {{warning.reason}}</p></details>
 </template>
 <footer class="matcher-status" role="status" :class="{failed:failure}"><i :class="{busy:resolving}"/>{{resultText}}</footer>
</section></template>
<style scoped>
.matcher-workspace{height:100%;min-height:0;display:flex;flex-direction:column;gap:0;overflow:hidden;border:1px solid var(--line);background:var(--panel-lo);border-radius:4px}.matcher-toolbar{display:flex;align-items:center;gap:6px;padding:8px 12px;border-bottom:1px solid var(--line);flex:none}.matcher-toolbar .auto-switch{height:30px;display:flex;gap:6px;align-items:center;font-size:10px;font-weight:600}.switch-light{width:6px;height:6px;border-radius:50%;background:var(--text-mute)}.auto-switch[aria-pressed=true]{color:var(--c-primary);border-color:var(--c-primary)}.auto-switch[aria-pressed=true] .switch-light{background:var(--c-primary)}.seed-control{display:flex;align-items:center;gap:7px;margin-left:7px;color:var(--text-mute);font-size:9px;letter-spacing:.08em}.seed-control input{width:95px;min-width:0;height:30px;font-variant-numeric:tabular-nums}.query-control{display:flex;align-items:center;gap:7px;flex:1;min-width:120px;margin-left:6px;padding-left:8px;border:1px solid var(--line);border-radius:3px;background:var(--lcd-bg)}.query-control svg{width:16px;height:16px;stroke:var(--text-mute);fill:none;stroke-width:1.7}.query-control input{height:30px;width:100%;min-width:0;border:0;background:transparent}.generation-choice{height:32px;width:128px;flex:none}.icon-button{display:inline-flex;align-items:center;justify-content:center;gap:7px;height:32px;padding:5px 9px!important;flex:none}.icon-button svg{width:17px;height:17px;fill:none;stroke:currentColor;stroke-width:1.6;stroke-linejoin:round}.die-button circle{fill:currentColor;stroke:none}.save-search{color:var(--c-primary)!important;font-size:10px!important;font-weight:600}.manual-toolbar{display:flex;gap:7px;align-items:center;flex-wrap:wrap;padding:8px 12px;border-bottom:1px solid var(--line);flex:none}.manual-toolbar label{display:flex;align-items:center;gap:6px;color:var(--text-mute);font-size:9px}.toolbar-spacer{flex:1}.auto-workspace>.workspace-empty{min-height:90px;padding:12px;gap:6px}.auto-workspace>.workspace-empty strong{font-size:15px}.auto-workspace{flex:1;min-height:0;display:flex;flex-direction:column;overflow:auto}.groove-heading{display:flex;align-items:center;gap:10px;padding:16px 18px 12px}.groove-heading h2{font-size:20px;font-weight:500;margin:0 8px 0 0}.groove-heading>span:not(.toolbar-spacer){font-size:10px;text-transform:capitalize;border:1px solid var(--line);border-radius:3px;padding:3px 6px;color:var(--text-dim)}.groove-heading small{font-size:10px;color:var(--text-mute)}.auto-zone-map{padding:0 12px 14px}.auto-zone-head,.auto-zone-row{display:grid;grid-template-columns:minmax(110px,.8fr) repeat(3,minmax(0,1fr));gap:1px}.auto-zone-head{padding:4px 8px;color:var(--text-mute);font-size:9px;letter-spacing:.08em}.auto-zone-row{border-top:1px solid color-mix(in srgb,var(--line) 55%,transparent);font-size:11px;min-height:29px;align-items:center}.auto-zone-row strong{font-weight:500;padding:5px 8px}.auto-zone-row>span{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin:3px 2px;padding:4px 7px;border-left:2px solid var(--zone-2);background:#0002;color:var(--text-dim)}.auto-zone-row .region-0{border-color:var(--zone-0)}.auto-zone-row .region-2{border-color:var(--zone-4)}.library-grid{position:relative;display:flex;flex:1;min-height:0;overflow:hidden}.library-busy{position:absolute;inset:0;display:grid;place-items:center;background:var(--panel-lo);color:var(--text-mute);font-size:12px;pointer-events:none}.manual-footer{display:flex;align-items:center;gap:8px;padding:6px 12px;font-size:10px;color:var(--text-mute)}.manual-footer>span:first-child{flex:1}.matcher-status{display:flex;align-items:center;gap:7px;padding:7px 12px;min-height:29px;border-top:1px solid var(--line);font-size:11px;color:var(--text-dim);flex:none}.matcher-status i{height:5px;width:5px;background:var(--text-mute);border-radius:50%}.matcher-status i.busy{background:var(--c-primary);animation:pulse 1s infinite}.matcher-status.failed{color:var(--c-danger)}.part-fallbacks{padding:6px 12px;font-size:11px;color:var(--text-mute);max-height:100px;overflow:auto}.part-fallbacks summary{cursor:pointer}.part-fallbacks p{margin:4px 0}@keyframes pulse{50%{opacity:.3}}@media(max-width:760px){.matcher-toolbar{flex-wrap:wrap;padding:8px}.query-control{order:4;flex-basis:calc(100% - 215px);margin-left:0}.generation-choice{order:5}.save-search{order:6}.seed-control{flex:1}.seed-control input{flex:1;width:90px;max-width:170px}.groove-heading{padding:10px}.groove-heading h2{font-size:16px}.auto-zone-head,.auto-zone-row{grid-template-columns:100px repeat(3,minmax(0,1fr))}.manual-toolbar .toolbar-spacer{display:none}}
</style>
