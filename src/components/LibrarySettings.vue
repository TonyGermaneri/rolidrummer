<script setup lang="ts">
import {computed,onBeforeUnmount,onMounted,ref} from 'vue'
import FamilyPanel from './FamilyPanel.vue'
import AssetBrowser from './AssetBrowser.vue'
import {request,native} from '../native/bridge'
import {state,attempt,replace} from '../store'
import {clone,type Patch} from '../model'
import type {LibraryStatus,LibrarySource,SourceMapping,PatternFamily} from '../library'
import maps from '../data/drumMaps.json'
const browseFiles=ref(false)
const path=ref(''),mapping=ref('auto'),prefix=ref(''),overrides=ref<{note:number;role:string}[]>([]),mappingOpen=ref(false)
const status=ref<LibraryStatus>({catalog:{revision:0,patterns:0,sources:[]},progress:{state:'idle'}})
const report=ref<{path:string;error:string}[]>([]),reportOffset=ref(0),reportOpen=ref(false),resolving=ref(false)
let disposed=false,polling=false,poll:ReturnType<typeof setInterval>|undefined
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

async function refresh(){
 if(!native()||disposed||polling)return
 polling=true
 try{const result=await request<LibraryStatus>('libraryStatus');if(!disposed)status.value=result}finally{polling=false}
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

async function resolveFamily(family:PatternFamily){
 if(resolving.value||!status.value.neural?.readyIndexId)return
 const before=state.patch,document=clone(before),unchanged=JSON.stringify(document)
 resolving.value=true
 try{
  const result=await request<Patch>('libraryResolve',{document,query:family.query,indexId:status.value.neural.readyIndexId,
   autoMode:false,seed:document.neural?.seed??'0',variationCount:document.neural?.variationCount??0,phraseBars:document.neural?.phraseBars??8,
   familyId:family.id,...(family.numerator?{numerator:family.numerator,denominator:family.denominator}:{}),...(family.feel!=='any'?{feel:family.feel}:{})})
  if(disposed)return
  if(state.patch!==before||JSON.stringify(state.patch)!==unchanged)throw Error('The patch changed during resolution. Resolve again to apply to the current patch.')
  result.name=family.name;await replace(result,'bar');state.tab='Patterns'
 }finally{resolving.value=false}
}
const reportRun=ref('')
async function showReport(offset=0,sourceRun?:string){
 const runId=sourceRun||reportRun.value||status.value.progress.runId;if(!runId)return
 const value=await request<{path:string;error:string}[]>('libraryErrors',{runId,offset,limit:50})
 if(!disposed){report.value=value;reportOffset.value=offset;reportOpen.value=true;reportRun.value=runId}
}

onMounted(()=>{void attempt(refresh);poll=setInterval(()=>void attempt(refresh),1500)})
onBeforeUnmount(()=>{disposed=true;clearInterval(poll)})
</script>
<template><section class="library-settings workspace-ui">
 <p v-if="!native()" class="workspace-empty">Open the native instrument to manage pattern libraries.</p>
 <template v-else>
  <details class="library-source-controls" open><summary>Pattern libraries<span v-if="running"> · {{progressText}}</span></summary>
  <div class="library-import"><button class="ssb-button" :disabled="running" @click="attempt(()=>choose())">IMPORT ZIP…</button><button class="ssb-button" :disabled="running" @click="attempt(()=>choose(true))">IMPORT FOLDER…</button><input v-model="path" aria-label="Pattern library source" placeholder="ZIP archive or library folder path" @keydown.enter="attempt(()=>start())"/><button class="ssb-button" :disabled="running||!path.trim()" @click="attempt(()=>start())">IMPORT</button><button class="ssb-button" :aria-expanded="mappingOpen" @click="mappingOpen=!mappingOpen">SOURCE MAP ▾</button></div>
  <div v-if="mappingOpen" class="library-mapping"><label>Source instrument map<select v-model="mapping" aria-label="Source drum map"><option value="auto">Infer from library</option><option value="gm">General MIDI</option><option value="vdrums">Roland V-Drums</option><option value="custom">Custom notes only</option></select></label><label>Limit mapping to folder<input v-model="prefix" aria-label="Source map folder prefix" placeholder="Optional relative folder prefix"/></label><div v-for="(entry,i) in overrides" :key="i" class="map-row"><input v-model.number="entry.note" type="number" min="0" max="127" aria-label="Source drum note"/><select v-model="entry.role" aria-label="Mapped drum role"><option value="">Leave unmapped</option><option v-for="voice in maps.voices" :key="voice.id" :value="voice.id">{{voice.name}}</option></select><button @click="overrides.splice(i,1)">Remove</button></div><button @click="overrides.push({note:36,role:'kick'})">＋ Note override</button><p>Vendor articulations with uncertain instrument identity stay unmapped. Explicit source maps and note overrides confirm their meaning. Reimport a source to apply a corrected map.</p></div>
  <div class="library-progress" role="status"><span>{{progressText}}</span><button v-if="running" class="ssb-button" @click="attempt(async()=>{await request('libraryCancel');await refresh()})">{{familyWork?'PAUSE FAMILY MATCHING':status.progress.stage?'PAUSE SEARCH BUILD':'PAUSE IMPORT'}}</button><button v-if="!running&&status.catalog.patterns&&(status.neural?.stale||status.neural?.preparationNeeded||searchPaused)" class="ssb-button" @click="attempt(buildSearch)">{{searchPaused?'RESUME SEARCH':status.neural?.readyIndexId?'REFRESH SEARCH':'BUILD SEMANTIC SEARCH'}}</button><button v-if="!running&&familyWork&&(status.progress.state==='paused'||status.progress.state==='failed')" class="ssb-button" @click="attempt(async()=>{await request('libraryFamilyRefresh');await refresh()})">{{status.progress.state==='failed'?'RETRY FAMILIES':'RESUME FAMILIES'}}</button><button v-if="status.progress.runId&&(status.progress.unsupported??0)>0" class="ssb-button" @click="attempt(()=>showReport())">IMPORT REPORT</button></div>
  <div v-if="status.catalog.sources.some(s=>s.state==='paused'||s.state==='failed')" class="library-sources"><template v-for="source in status.catalog.sources" :key="source.id"><button v-if="source.state==='paused'||source.state==='failed'" :disabled="running" :title="source.path" @click="attempt(()=>start(source))">Resume {{source.path.split('/').at(-1)}}</button></template></div>
  <div v-if="!status.progress.runId" class="library-sources"><template v-for="source in status.catalog.sources" :key="source.id"><button v-if="source.unsupported&&source.latestRun" :title="source.path" @click="attempt(()=>showReport(0,source.latestRun))">Import report · {{source.path.split('/').at(-1)}} · {{source.unsupported}} unsupported</button></template></div>
  <div class="library-sources"><template v-for="source in status.catalog.sources" :key="source.id"><button v-if="source.needsReimport" :disabled="running" :title="source.path" @click="attempt(()=>start(source))">Update map analysis · {{source.path.split('/').at(-1)}}</button></template></div>
  </details>

 <FamilyPanel v-if="status.families&&status.neural?.readyIndexId" :status="status.families" :progress="status.progress" :resolving="resolving" @refresh="attempt(refresh)" @resolve="family=>attempt(()=>resolveFamily(family))"/>
  <section v-if="reportOpen" class="library-report"><button @click="reportOpen=false">Close report</button><div v-for="item in report" :key="item.path"><b>{{item.error}}</b><span>{{item.path}}</span></div><button :disabled="reportOffset===0" @click="attempt(()=>showReport(Math.max(0,reportOffset-50)))">Previous</button><button :disabled="report.length<50" @click="attempt(()=>showReport(reportOffset+50))">Next</button></section>
 <details class="midi-file-browser" @toggle="browseFiles=($event.target as HTMLDetailsElement).open"><summary>Browse MIDI files and patch patterns</summary><AssetBrowser v-if="browseFiles" patterns/></details>
 </template>
</section></template>
<style scoped>
.midi-file-browser{font-size:12px}.midi-file-browser>summary{cursor:pointer;padding:8px 0}.midi-file-browser :deep(.asset-browser){height:280px}
.library-settings{display:flex;flex-direction:column;gap:14px;min-height:0}.library-source-controls{font-size:12px}.library-source-controls>summary{font-weight:600;cursor:pointer;padding:4px 0 10px}.library-import,.library-progress,.library-sources,.library-mapping,.map-row{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.library-import input{flex:1;min-width:180px}.library-progress{margin:12px 0;font-size:12px;color:var(--text-dim)}.library-progress>span{flex:1}.library-sources{margin-top:8px}.library-mapping{margin:10px 0;padding:12px;border:1px solid var(--line);background:var(--lcd-bg)}.library-mapping label{display:flex;gap:8px;align-items:center}.library-mapping p{flex-basis:100%;font-size:11px;color:var(--text-dim)}.map-row input{width:65px}.library-report{max-height:220px;overflow:auto;border:1px solid var(--line);padding:12px;font-size:11px}.library-report div{display:flex;gap:10px;padding:6px 0;overflow-wrap:anywhere}.library-report b{min-width:140px}
</style>
