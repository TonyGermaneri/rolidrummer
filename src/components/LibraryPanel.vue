<script setup lang="ts">
import {computed,onBeforeUnmount,onMounted,ref,watch} from 'vue'
import DataGrid from './DataGrid.vue'
import FamilyPanel from './FamilyPanel.vue'
import {request,native} from '../native/bridge'
import {state,attempt,replace} from '../store'
import {clone,type Patch} from '../model'
import {begin,drop} from '../drag'
import {preview,audition,stopPreview} from '../preview'
import {libraryAsset,type LibraryPage,type LibraryRow,type LibrarySource,type LibraryStatus,type NeuralPage,type SourceMapping,type PatternFamily} from '../library'
import maps from '../data/drumMaps.json'

const path=ref(''),query=ref(''),role=ref(''),mapping=ref('auto'),prefix=ref(''),overrides=ref<{note:number;role:string}[]>([]),mappingOpen=ref(false)
const status=ref<LibraryStatus>({catalog:{revision:0,patterns:0,sources:[]},progress:{state:'idle'}})
const page=ref<LibraryPage>({rows:[],offset:0,more:false,revision:0,patterns:0})
const chosen=ref<LibraryRow|null>(null),zone=ref(0),loading=ref(false),report=ref<{path:string;error:string}[]>([]),reportOffset=ref(0),reportOpen=ref(false)
const searchMode=ref('semantic'),searchInfo=ref('')
const resolving=ref(false),phraseBars=ref(state.patch.neural?.phraseBars??8)
const variationCount=ref(state.patch.neural?.variationCount??0),seed=ref(state.patch.neural?.seed??'0')
watch(()=>state.patch.neural,resolution=>{variationCount.value=resolution?.variationCount??0;seed.value=resolution?.seed??'0';phraseBars.value=resolution?.phraseBars??8})
const availableMode=computed({get:()=>status.value.neural?.readyIndexId?searchMode.value:'description',set:(value:string)=>{searchMode.value=value}})
const semantic=computed(()=>searchMode.value==='semantic'&&Boolean(status.value.neural?.readyIndexId)&&Boolean(query.value.trim()))
const selected=computed(()=>state.patch.keys.find(k=>k.note===state.selected))
const rows=computed(()=>page.value.rows.map(row=>({...row,meter:`${row.numerator}/${row.denominator}`,coverageLabel:`${Math.round(row.coverage*100)}%`,similarityLabel:row.similarity===undefined?'—':row.similarity.toFixed(3),preview:preview.id===`library:${row.id}`?'■':'▶',assign:'ASSIGN'})))
const running=computed(()=>Boolean(status.value.progress.running))
const familyWork=computed(()=>status.value.progress.stage==='families'||status.value.progress.stage?.startsWith('family-'))
const searchPaused=computed(()=>status.value.neural?.state==='paused'||['snapshot','search-metadata'].includes(status.value.progress.stage??'')&&status.value.progress.state==='paused')
const progressText=computed(()=>{
 const p=status.value.progress
 if(p.error)return p.error
 if(p.stage==='family-features')return `${p.state==='paused'?'Family analysis paused':'Preparing pattern families'} · ${(p.completed??0).toLocaleString()} performances analyzed`
 if(p.stage==='family-suggestions')return p.state==='paused'?'Family suggestions paused':'Finding names for uncovered pattern families…'
 if(p.stage==='families')return p.state==='complete'?'Pattern families ready':p.state==='paused'?'Family matching paused':p.state==='busy'?'Another instance is updating pattern families':`Matching pattern families · ${p.completed??0} / ${p.groups??0}`
 if(p.stage==='snapshot'||p.stage==='search-metadata')return p.state==='paused'?'Search preparation paused':p.state==='busy'?'Another instance is preparing search…':'Preparing search…'
 if(p.stage==='embeddings')return `${p.state==='complete'?'Search ready':'Embedding patterns'} · ${(p.completed??0).toLocaleString()} / ${(p.groups??0).toLocaleString()} descriptions`
 if(p.stage==='descriptions')return `Preparing search · ${(p.descriptions??0).toLocaleString()} / ${(p.totalDescriptions??0).toLocaleString()} descriptions`
 if(p.stage==='ready')return `Semantic search ready · ${(p.groups??0).toLocaleString()} descriptions`
 if(p.state==='waiting')return 'Waiting for another library operation…'
 if(p.running)return p.total?`${p.state} · ${(100*(p.scanned??0)/p.total).toFixed(1)}% · ${(p.accepted??0).toLocaleString()} MIDI files indexed`:`${p.state}…`
 if(p.state==='complete')return `Import complete · ${(p.accepted??0).toLocaleString()} MIDI files · ${(p.unsupported??0).toLocaleString()} unsupported`
 if(p.state==='paused')return 'Import paused · Choose Resume below to continue'
 return p.error??`${status.value.catalog.patterns.toLocaleString()} distinct patterns indexed`
})
const columns=[{name:'preview',title:'Listen',width:58},{name:'assign',title:'Zone',width:75},{name:'name',title:'Pattern',width:260},{name:'role',title:'Role',width:85},{name:'meter',title:'Meter',width:65},{name:'coverageLabel',title:'Mapped',width:75},{name:'similarityLabel',title:'Similarity',width:80},{name:'description',title:'Musical description',width:530}]
let disposed=false,ticket=0,polling=false,poll:ReturnType<typeof setInterval>|undefined

async function load(offset=0){
 if(!native())return
 const current=++ticket;loading.value=true
 try{
  const useSemantic=semantic.value
  const result=useSemantic?await request<NeuralPage>('librarySearch',{query:query.value,role:role.value,indexId:status.value.neural?.readyIndexId,limit:50}):await request<LibraryPage>('libraryBrowse',{query:query.value,role:role.value,offset,limit:50})
  if(disposed||current!==ticket)return
  if(useSemantic){const neural=result as NeuralPage;page.value={...neural,offset:0,more:false,patterns:status.value.catalog.patterns};searchInfo.value=`${neural.provider} · ${Math.round(neural.milliseconds)} ms · Library revision ${neural.revision}${neural.interpretedQuery?` · Matched as: ${neural.interpretedQuery}`:''}`}
  else{page.value=result as LibraryPage;searchInfo.value=query.value.trim()?'Description filter':'Browse indexed patterns'}
  chosen.value=result.rows.find(r=>r.id===chosen.value?.id)??null
 }finally{if(current===ticket)loading.value=false}
}
async function refresh(){
 if(!native()||disposed||polling)return
 polling=true
 try{
  const result=await request<LibraryStatus>('libraryStatus')
  if(disposed)return
  const changed=result.catalog.revision!==status.value.catalog.revision||result.neural?.readyIndexId!==status.value.neural?.readyIndexId
  status.value=result
  if(changed)await load()
 }finally{polling=false}
}
function policy():SourceMapping{
 const notes:Record<string,string|null>={}
 for(const row of overrides.value){if(!Number.isInteger(row.note)||row.note<0||row.note>127)throw Error('Source notes must be whole numbers from 0 to 127.');notes[row.note]=row.role||null}
 const value={id:mapping.value,notes}
 return prefix.value.trim()?{id:'auto',rules:[{prefix:prefix.value.trim(),...value}]}:value
}
async function start(source?:LibrarySource){
 const target=source?.path??path.value.trim();if(!target||running.value)return
 const result=await request<LibraryStatus['progress']>('libraryImport',{path:target,mapping:source?.mapping??policy()})
 if(!disposed){status.value.progress=result;path.value=target;await refresh()}
}
async function choose(folder=false){
 const result=await request<string|null>(folder?'chooseFolder':'chooseLibraryArchive')
 if(result&&!disposed){path.value=result;await start()}
}
async function buildSearch(){if(running.value)return;status.value.progress=await request<LibraryStatus['progress']>('libraryIndex');await refresh()}
async function resolvePatch(family?:PatternFamily){
 const musicalQuery=family?.query??query.value
 if(resolving.value||!musicalQuery.trim()||!status.value.neural?.readyIndexId)return
 const before=state.patch,document=clone(before),unchanged=JSON.stringify(document)
 resolving.value=true
 try{
  const familyOptions=family?{familyId:family.id,...(family.numerator?{numerator:family.numerator,denominator:family.denominator}:{}),...(family.feel!=='any'?{feel:family.feel}:{})}:{}
  const result=await request<Patch>('libraryResolve',{document,query:musicalQuery,indexId:status.value.neural.readyIndexId,seed:seed.value,phraseBars:phraseBars.value,variationCount:variationCount.value,...familyOptions})
  if(disposed)return
  if(state.patch!==before||JSON.stringify(state.patch)!==unchanged)throw Error('The patch changed during resolution. Resolve again to apply to the current patch.')
  if(family)result.name=family.name
  await replace(result,'bar')
 }finally{resolving.value=false}
}
async function assign(row=chosen.value){if(row&&selected.value)await drop(libraryAsset(row),true,`zone:${selected.value.note}:${Math.min(zone.value,selected.value.zones.length-1)}`)}
const reportRun=ref('')
async function showReport(offset=0,sourceRun?:string){
 const runId=sourceRun||reportRun.value||status.value.progress.runId;if(!runId)return
 const value=await request<{path:string;error:string}[]>('libraryErrors',{runId,offset,limit:50})
 if(!disposed){report.value=value;reportOffset.value=offset;reportOpen.value=true;reportRun.value=runId}
}
onMounted(()=>{void attempt(async()=>{await refresh();if(!disposed&&!page.value.rows.length)await load()});poll=setInterval(()=>void attempt(refresh),1000)})
onBeforeUnmount(()=>{disposed=true;++ticket;clearInterval(poll);if(preview.id.startsWith('library:'))void attempt(stopPreview)})
</script>
<template><section class="indexed-library">
 <p v-if="!native()" class="library-empty">Open the native instrument to import your ZIP archive and play its patterns.</p>
 <template v-else>
  <details class="library-source-controls" :open="!status.catalog.patterns||running"><summary>Pattern libraries<span v-if="running"> · {{progressText}}</span></summary>
  <div class="library-import"><button class="ssb-button" :disabled="running" @click="attempt(()=>choose())">IMPORT ZIP…</button><button class="ssb-button" :disabled="running" @click="attempt(()=>choose(true))">IMPORT FOLDER…</button><input v-model="path" aria-label="Pattern library source" placeholder="ZIP archive or library folder path" @keydown.enter="attempt(()=>start())"/><button class="ssb-button" :disabled="running||!path.trim()" @click="attempt(()=>start())">IMPORT</button><button class="ssb-button" :aria-expanded="mappingOpen" @click="mappingOpen=!mappingOpen">SOURCE MAP ▾</button></div>
  <div v-if="mappingOpen" class="library-mapping"><label>Source instrument map<select v-model="mapping" aria-label="Source drum map"><option value="auto">Infer from library</option><option value="gm">General MIDI</option><option value="vdrums">Roland V-Drums</option><option value="custom">Custom notes only</option></select></label><label>Limit mapping to folder<input v-model="prefix" aria-label="Source map folder prefix" placeholder="Optional relative folder prefix"/></label><div v-for="(entry,i) in overrides" :key="i" class="map-row"><input v-model.number="entry.note" type="number" min="0" max="127" aria-label="Source drum note"/><select v-model="entry.role" aria-label="Mapped drum role"><option value="">Leave unmapped</option><option v-for="voice in maps.voices" :key="voice.id" :value="voice.id">{{voice.name}}</option></select><button @click="overrides.splice(i,1)">Remove</button></div><button @click="overrides.push({note:36,role:'kick'})">＋ Note override</button><p>Vendor articulations with uncertain instrument identity stay unmapped. Explicit source maps and note overrides confirm their meaning. Reimport a source to apply a corrected map.</p></div>
  <div class="library-progress" role="status"><span>{{progressText}}</span><button v-if="running" class="ssb-button" @click="attempt(async()=>{await request('libraryCancel');await refresh()})">{{familyWork?'PAUSE FAMILY MATCHING':status.progress.stage?'PAUSE SEARCH BUILD':'PAUSE IMPORT'}}</button><button v-if="!running&&status.catalog.patterns&&(status.neural?.stale||status.neural?.preparationNeeded||searchPaused)" class="ssb-button" @click="attempt(buildSearch)">{{searchPaused?'RESUME SEARCH':status.neural?.readyIndexId?'REFRESH SEARCH':'BUILD SEMANTIC SEARCH'}}</button><button v-if="!running&&familyWork&&status.progress.state==='paused'" class="ssb-button" @click="attempt(async()=>{await request('libraryFamilyRefresh');await refresh()})">RESUME FAMILIES</button><button v-if="status.progress.runId&&(status.progress.unsupported??0)>0" class="ssb-button" @click="attempt(()=>showReport())">IMPORT REPORT</button></div>
  <div v-if="status.catalog.sources.some(s=>s.state==='paused'||s.state==='failed')" class="library-sources"><template v-for="source in status.catalog.sources" :key="source.id"><button v-if="source.state==='paused'||source.state==='failed'" :disabled="running" :title="source.path" @click="attempt(()=>start(source))">Resume {{source.path.split('/').at(-1)}}</button></template></div>
  <div v-if="!status.progress.runId" class="library-sources"><template v-for="source in status.catalog.sources" :key="source.id"><button v-if="source.unsupported&&source.latestRun" :title="source.path" @click="attempt(()=>showReport(0,source.latestRun))">Import report · {{source.path.split('/').at(-1)}} · {{source.unsupported}} unsupported</button></template></div>
  <div class="library-sources"><template v-for="source in status.catalog.sources" :key="source.id"><button v-if="source.needsReimport" :disabled="running" :title="source.path" @click="attempt(()=>start(source))">Update map analysis · {{source.path.split('/').at(-1)}}</button></template></div>
  </details>
  <div class="library-search"><input v-model="query" aria-label="Filter indexed patterns" :placeholder="status.neural?.readyIndexId?'Patch name or musical intent…':'Filter musical descriptions…'" @keydown.enter="attempt(()=>load())"/><select v-model="availableMode" aria-label="Pattern search mode" @change="attempt(()=>load())"><option value="semantic" :disabled="!status.neural?.readyIndexId">Semantic similarity</option><option value="description">Description filter</option></select><select v-model="role" aria-label="Pattern role" @change="attempt(()=>load())"><option value="">All roles</option><option value="groove">Grooves</option><option value="sparse">Sparse</option><option value="fill">Fills</option><option value="intro">Intros</option><option value="ending">Endings</option></select><button class="ssb-button" :disabled="loading" @click="attempt(()=>load())">SEARCH</button><label>ZONE<select v-model.number="zone" aria-label="Assignment zone"><option v-for="(_,i) in selected?.zones??[]" :key="i" :value="i">{{i+1}}</option></select></label></div>
  <p v-if="searchInfo" class="library-search-info">{{searchInfo}}<span v-if="semantic&&status.neural?.stale"> · New imports await a search refresh</span></p>
  <div v-if="status.neural?.readyIndexId" class="library-resolve"><button class="ssb-button" :disabled="resolving||!query.trim()" @click="attempt(resolvePatch)">{{resolving?'RESOLVING…':'RESOLVE PATCH'}}</button><label>PHRASE CYCLE<select v-model.number="phraseBars" aria-label="Phrase cycle bars"><option v-for="bars in [1,2,3,4,6,8,12,16]" :key="bars" :value="bars">{{bars}} bars</option></select></label><span>Assign sparse, steady and fill regions to unlocked keys.</span></div>
  <details v-if="status.neural?.readyIndexId" class="library-generation"><summary>Generated variations<span v-if="state.patch.neural?.generatedCount"> · {{state.patch.neural.generatedCount}} saved</span></summary><label>Variations per region<select v-model.number="variationCount" aria-label="Generated variations per region"><option :value="0">Original patterns only</option><option v-for="count in [1,2,3,4]" :key="count" :value="count">Up to {{count}} generated</option></select></label><label>Seed<input v-model="seed" aria-label="Pattern variation seed" maxlength="128"/></label><p>Resolve to recombine compatible beat fragments. Accents, source timing and drum assignments are preserved; the resulting notes are saved in the patch.</p></details>
  <p v-if="state.patch.neural" class="library-search-info">{{state.patch.neural.query}} · {{state.patch.neural.numerator}}/{{state.patch.neural.denominator}} · {{state.patch.neural.feel}} · {{state.patch.neural.keys.length}} keys resolved<span v-if="state.patch.neural.warnings.length"> · {{state.patch.neural.warnings.length}} part fallbacks</span></p>
  <p v-if="state.telemetry.activationPending" class="library-search-info" role="status">Prepared patterns are waiting for the next bar. Release all notes and the sustain pedal to apply immediately.</p>
  <FamilyPanel v-if="status.families&&status.neural?.readyIndexId" :status="status.families" :resolving="resolving" :candidate="chosen" @refresh="attempt(refresh)" @resolve="family=>attempt(()=>resolvePatch(family))"/>
  <details v-if="state.patch.neural?.warnings.length" class="library-fallbacks"><summary>Part fallbacks</summary><p v-for="(warning,i) in state.patch.neural.warnings" :key="i">{{state.patch.keys.find(key=>key.note===warning.key)?.name??warning.key}} · {{warning.reason}}</p></details>
  <div class="library-grid"><DataGrid :rows="rows" :columns="columns" :chosen="chosen||undefined" @pick="chosen=$event" @use="row=>attempt(()=>assign(row))" @cell="(column,row)=>column==='preview'?attempt(()=>audition(libraryAsset(row),true)):column==='assign'?attempt(()=>assign(row)):undefined" @drag="(row,event)=>begin(libraryAsset(row),true,event)"/><div v-if="loading" class="library-busy">Searching library…</div><div v-else-if="!rows.length" class="library-busy">{{status.catalog.patterns?'No matching patterns':'Import a ZIP archive or folder to build your pattern library.'}}</div></div>
  <footer><span>{{status.catalog.patterns.toLocaleString()}} distinct patterns · Drag a result to a key zone · Mapped shows recognized source notes</span><button :disabled="loading||page.offset===0" @click="attempt(()=>load(Math.max(0,page.offset-50)))">Previous</button><span>{{page.offset+Number(rows.length>0)}}–{{page.offset+rows.length}}</span><button :disabled="loading||!page.more" @click="attempt(()=>load(page.offset+50))">Next</button></footer>
  <section v-if="reportOpen" class="library-report"><button @click="reportOpen=false">Close report</button><div v-for="item in report" :key="item.path"><b>{{item.error}}</b><span>{{item.path}}</span></div><button :disabled="reportOffset===0" @click="attempt(()=>showReport(Math.max(0,reportOffset-50)))">Previous</button><button :disabled="report.length<50" @click="attempt(()=>showReport(reportOffset+50))">Next</button></section>
 </template>
</section></template>
<style scoped>
.indexed-library{height:100%;min-height:0;display:flex;flex-direction:column;gap:7px}.library-import,.library-search,.library-progress,.library-sources{display:flex;gap:6px;align-items:center;flex-wrap:wrap;flex:none}.library-import input,.library-search>input{flex:1;min-width:180px}.indexed-library input,.indexed-library select{background:var(--lcd-bg);color:var(--text);border:1px solid #ffffff20;border-radius:3px;padding:5px 7px;font:11px monospace}.library-progress{font:11px monospace;color:var(--text-dim)}.library-progress span{flex:1}.library-search label{display:flex;gap:6px;align-items:center;font:9px monospace}.library-grid{position:relative;flex:1;min-height:140px;display:flex;overflow:hidden}.library-busy{position:absolute;inset:0;display:grid;place-items:center;pointer-events:none;background:var(--bg);color:var(--text-dim);font:12px monospace}.indexed-library footer{display:flex;gap:8px;align-items:center;font:10px monospace;color:var(--text-dim);flex-wrap:wrap}.indexed-library footer>span:first-child{flex:1;min-width:230px}.library-mapping{max-height:180px;overflow:auto;display:flex;gap:9px;align-items:center;flex-wrap:wrap;border:1px solid #ffffff20;padding:7px;font:11px monospace}.library-mapping label{display:flex;gap:7px;align-items:center}.library-mapping p{width:100%;margin:0;color:var(--text-dim)}.map-row{display:flex;gap:5px}.map-row input{width:60px}.library-report{max-height:170px;overflow:auto;border:1px solid #ffffff20;padding:7px;font:10px monospace}.library-report div{display:flex;gap:8px;padding:4px 0;overflow-wrap:anywhere}.library-report b{min-width:170px}.library-empty{color:var(--text-dim);padding:20px}
.library-source-controls{flex:none;font:11px monospace}.library-source-controls summary{cursor:pointer;color:var(--text-dim);padding:4px 0}.library-source-controls[open] summary{margin-bottom:6px}.library-source-controls .library-progress{margin:6px 0}.indexed-library{overflow:auto}.library-grid{flex:1 0 140px}
.library-search-info{margin:0;font:10px monospace;color:var(--text-dim)}
.library-fallbacks{font:10px monospace;color:var(--text-dim);max-height:90px;overflow:auto;flex:none}.library-fallbacks summary{cursor:pointer}.library-fallbacks p{margin:5px 0}
.library-resolve{display:flex;align-items:center;gap:9px;flex-wrap:wrap;font:10px monospace;color:var(--text-dim)}
.library-generation{flex:none;font:10px monospace;color:var(--text-dim)}.library-generation summary{cursor:pointer;padding:3px 0}.library-generation label{display:inline-flex;align-items:center;gap:7px;margin:5px 12px 0 0}.library-generation input{width:150px}.library-generation p{margin:7px 0;max-width:850px;line-height:1.5}
</style>
