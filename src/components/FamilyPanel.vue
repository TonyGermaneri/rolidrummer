<script setup lang="ts">
import {computed,nextTick,onBeforeUnmount,ref,watch} from 'vue'
import {request} from '../native/bridge'
import {attempt} from '../store'
import type {FamilyStatus,PatternFamily,FamilyMember,FamilyDecision,FamilySuggestion,LibraryProgress,LibraryRow} from '../library'

const props=defineProps<{status:FamilyStatus;progress?:LibraryProgress;resolving?:boolean;candidate?:LibraryRow|null}>()
const emit=defineEmits<{refresh:[];resolve:[family:PatternFamily]}>()
const open=ref(false),mode=ref<'families'|'suggestions'>(props.status.families.length?'families':'suggestions')
const dialog=ref<HTMLDialogElement>()
const selected=ref(props.status.families[0]?.id??''),view=ref('patterns'),name=ref(''),busy=ref(false),loading=ref(false)
const revision=ref(props.status.revision),offset=ref(0),more=ref(false),rows=ref<(FamilyMember|FamilyDecision)[]>([])
const suggestionOffset=ref(0),suggestionMore=ref(false),suggestions=ref<FamilySuggestion[]>([]),suggestionBuild=ref('')
const names=ref<Record<string,string>>({}),targets=ref<Record<string,string>>({}),overrides=ref<Record<string,string>>({})
const family=computed(()=>props.status.families.find(f=>f.id===selected.value))
const sorted=computed(()=>[...props.status.families].sort((a,b)=>a.name.localeCompare(b.name)||a.id.localeCompare(b.id)))
// What out-of-date membership is waiting for: only a running worker is "matching".
const familyWork=computed(()=>props.progress?.stage==='families'||Boolean(props.progress?.stage?.startsWith('family-')))
const staleText=computed(()=>{
 const p=props.progress
 if(!props.status.stale)return ''
 if(p?.running&&familyWork.value)return p.state==='busy'?'Another instance is matching…':`Matching updates…${p.stage==='families'&&p.groups?` ${p.completed??0}/${p.groups}`:''}`
 if(p?.running)return 'Waiting for the library…'
 if(familyWork.value&&p?.state==='failed')return 'Matching failed'
 if(familyWork.value&&p?.state==='paused')return 'Matching paused'
 return props.status.staleReason==='search'?'Waiting for search to be refreshed':'Out of date'
})
// Paused or failed matching is resumed from the library's progress line above.
const canMatch=computed(()=>props.status.stale&&!props.progress?.running&&props.status.staleReason!=='search'&&!(familyWork.value&&['paused','failed'].includes(props.progress?.state??'')))
// Families searches registered before saving did: never saved, renamed, edited or accepted.
const searchFamilies=computed(()=>props.status.families.filter(f=>!f.origin&&f.name===f.query&&!f.queries?.length&&!f.decisions&&!f.suggested))
const confirming=ref('')
let disposed=false,ticket=0,suggestionTicket=0
watch(open,async shown=>{await nextTick();if(disposed||!dialog.value)return;if(shown&&!dialog.value.open)dialog.value.showModal();else if(!shown&&dialog.value.open)dialog.value.close()},{flush:'post'})
watch(()=>props.status.revision,value=>{revision.value=Math.max(revision.value,value)})
watch(()=>props.status.families.map(f=>f.id).join(','),()=>{if(!props.status.families.some(f=>f.id===selected.value))selected.value=props.status.families[0]?.id??''})
watch(selected,()=>{confirming.value=''})
watch([()=>family.value?.id,()=>family.value?.name],([id,label],previous)=>{if(id!==previous?.[0]||name.value===previous?.[1])name.value=label??''},{immediate:true})
async function loadMembers(at=0){
 if(disposed||!open.value||!selected.value)return
 const current=++ticket;loading.value=true
 try{
  const manual=view.value==='decisions'
  if(!manual&&!props.status.buildId){rows.value=[];return}
  const page=await request<{rows:(FamilyMember|FamilyDecision)[];offset:number;more:boolean}>(manual?'libraryFamilyDecisions':'libraryFamilyMembers',{familyId:selected.value,buildId:props.status.buildId,offset:at,limit:25})
  if(disposed||current!==ticket)return
  rows.value=page.rows;offset.value=page.offset;more.value=page.more
 }finally{if(current===ticket)loading.value=false}
}
async function loadSuggestions(at=0){
 if(disposed||!open.value)return
 const current=++suggestionTicket
 const page=await request<{rows:FamilySuggestion[];offset:number;more:boolean;buildId:string}>('libraryFamilySuggestions',{offset:at,limit:10})
 if(disposed||current!==suggestionTicket)return
 suggestions.value=page.rows;suggestionOffset.value=page.offset;suggestionMore.value=page.more;suggestionBuild.value=page.buildId
 for(const row of page.rows){names.value[row.id]??=row.name;targets.value[row.id]??=''}
}
watch(()=>props.status.buildId,()=>{overrides.value={}})
watch([open,mode,selected,view,()=>props.status.buildId,()=>props.status.revision],()=>{
 if(!open.value)return
 void attempt(()=>mode.value==='suggestions'?loadSuggestions():loadMembers())
})
async function mutate(method:string,payload:Record<string,unknown>){
 if(busy.value)return
 busy.value=true
 try{const result=await request<{revision:number;family?:PatternFamily}>(method,{...payload,revision:revision.value});if(disposed)return;revision.value=Math.max(revision.value,result.revision);return result}
 finally{busy.value=false;if(!disposed)emit('refresh')}
}
function choice(row:FamilyMember|FamilyDecision){return overrides.value[`${selected.value}:${row.patternId}`]??row.decision}
function stateLabel(row:FamilyMember|FamilyDecision){const own=overrides.value[`${selected.value}:${row.patternId}`];if(own)return own==='include'?'Included':own==='exclude'?'Excluded':'Automatic';if(row.decision!=='auto')return row.decision==='include'?'Included':'Excluded';return 'state' in row&&row.state==='ambiguous'?'Ambiguous':'Matched'}
async function decide(row:FamilyMember|FamilyDecision,action:string){
 const id=selected.value
 const result=await mutate('libraryFamilyUpdate',{familyId:id,patternId:row.patternId,action})
 if(result){overrides.value[`${id}:${row.patternId}`]=action;if(view.value==='decisions')await loadMembers(offset.value)}
}
async function match(){await request('libraryFamilyRefresh');if(!disposed)emit('refresh')}
async function removeFamilies(ids:string[],step:string){
 if(!ids.length)return
 if(confirming.value!==step){confirming.value=step;return}
 confirming.value='';await mutate('libraryFamilyRemove',{familyIds:ids})
}
async function rename(){if(family.value&&name.value.trim())await mutate('libraryFamilyUpdate',{familyId:family.value.id,action:'rename',name:name.value.trim()})}
async function includeCandidate(){
 if(!props.candidate||!family.value)return
 const id=family.value.id,pattern=props.candidate.patternId
 if(await mutate('libraryFamilyUpdate',{familyId:id,patternId:pattern,action:'include'}))overrides.value[`${id}:${pattern}`]='include'
}
function compatible(row:FamilySuggestion){return sorted.value.filter(f=>(!f.numerator||(f.numerator===row.numerator&&f.denominator===row.denominator))&&(f.feel==='any'||row.feel==='any'||f.feel===row.feel))}
async function decideSuggestion(row:FamilySuggestion,action:string){
 if(action==='merge'&&!targets.value[row.id])return
 const result=await mutate('libraryFamilyDecision',{id:row.id,buildId:suggestionBuild.value,action,name:names.value[row.id],familyId:targets.value[row.id]})
 if(result){suggestions.value=suggestions.value.filter(suggestion=>suggestion.id!==row.id);if(action==='accept'&&result.family)selected.value=result.family.id;await loadSuggestions(suggestionOffset.value)}
}
onBeforeUnmount(()=>{disposed=true;++ticket;++suggestionTicket})
</script>
<template><details class="family-panel" :open="open" @toggle="open=($event.target as HTMLDetailsElement).open">
 <summary>Pattern families · {{status.families.length}} named<span v-if="status.suggestions"> · {{status.suggestions.toLocaleString()}} suggestions</span><span v-if="staleText"> · {{staleText}}</span><button v-if="canMatch" class="ssb-button family-match" title="Match the families against the current catalog" @click.prevent.stop="attempt(match)">MATCH NOW</button></summary>
 <dialog ref="dialog" class="family-dialog" aria-label="Pattern families" @cancel.prevent="open=false" @close="open=false"><template v-if="open">
  <header><strong>Pattern families</strong><button class="ssb-button" aria-label="Close pattern families" @click="open=false">CLOSE</button></header>
  <nav><button class="ssb-button" :aria-pressed="mode==='families'" @click="mode='families'">FAMILIES</button><button class="ssb-button" :aria-pressed="mode==='suggestions'" @click="mode='suggestions'">NAME SUGGESTIONS</button><span>Resolve a family to use its latest confirmed patterns.</span></nav>
  <template v-if="mode==='families'">
   <p v-if="familyWork&&progress?.state==='failed'&&progress.error" class="family-error">Matching failed: {{progress.error}}</p>
   <p v-if="!status.families.length">Save a searched groove to register its family, or accept a name suggestion.</p>
   <template v-else>
    <div class="family-controls"><select v-model="selected" aria-label="Pattern family" :disabled="busy"><option v-for="entry in sorted" :key="entry.id" :value="entry.id">{{entry.name}} · {{entry.numerator?`${entry.numerator}/${entry.denominator}`:'Any meter'}} · {{entry.feel}}</option></select><input v-model="name" aria-label="Family display name" :disabled="busy" maxlength="128"/><button class="ssb-button" :disabled="busy||!name.trim()||name===family?.name" @click="attempt(rename)">RENAME</button><button class="ssb-button" :disabled="busy||resolving||!family?.members" @click="family&&emit('resolve',family)">{{resolving?'RESOLVING…':'RESOLVE FAMILY'}}</button><button class="ssb-button" :disabled="busy||!family" @click="family&&attempt(()=>removeFamilies([family!.id],family!.id))">{{confirming===family?.id?'CONFIRM REMOVE':'REMOVE'}}</button></div>
    <div v-if="searchFamilies.length" class="family-controls"><span>{{searchFamilies.length.toLocaleString()}} {{searchFamilies.length===1?'family was':'families were'}} registered by searches and never saved or edited.</span><button class="ssb-button" :disabled="busy" @click="attempt(()=>removeFamilies(searchFamilies.map(f=>f.id),'searches'))">{{confirming==='searches'?`CONFIRM REMOVE ${searchFamilies.length.toLocaleString()}`:'REMOVE SEARCH FAMILIES'}}</button></div>
    <p v-if="family">{{family.members.toLocaleString()}} confirmed · {{family.ambiguous.toLocaleString()}} ambiguous · Query: {{family.query}}<span v-if="family.queries?.length"> · Also matches: {{family.queries.join(', ')}}</span></p>
    <div v-if="candidate&&family" class="family-controls"><span class="family-path">Library result: {{candidate.name}}</span><button :disabled="busy||overrides[`${family.id}:${candidate.patternId}`]==='include'" @click="attempt(includeCandidate)">INCLUDE LIBRARY RESULT</button></div>
    <div class="family-controls"><select v-model="view" aria-label="Family pattern view" :disabled="busy"><option value="patterns">Matched and ambiguous patterns</option><option value="decisions">Manual inclusions and exclusions</option></select><span v-if="loading">Loading…</span><span v-else>{{offset+Number(rows.length>0)}}–{{offset+rows.length}}</span><button :disabled="busy||loading||offset===0" @click="attempt(()=>loadMembers(Math.max(0,offset-25)))">Previous</button><button :disabled="busy||loading||!more" @click="attempt(()=>loadMembers(offset+25))">Next</button></div>
    <div class="family-patterns"><div v-for="row in rows" :key="row.patternId" class="family-pattern"><span class="family-state">{{stateLabel(row)}}</span><span class="family-path" :title="row.path">{{row.path}}<small v-if="'alternatives' in row&&row.alternatives?.length"> · Also: {{row.alternatives.map(other=>status.families.find(f=>f.id===other.familyId)?.name??'Another family').join(', ')}}<span v-if="row.moreAlternatives">…</span></small></span><button :disabled="busy||choice(row)==='include'" @click="attempt(()=>decide(row,'include'))">Include</button><button :disabled="busy||choice(row)==='exclude'" @click="attempt(()=>decide(row,'exclude'))">Exclude</button><button :disabled="busy||choice(row)==='auto'" @click="attempt(()=>decide(row,'auto'))">Auto</button></div><p v-if="!loading&&!rows.length">No patterns in this view.</p></div>
   </template>
  </template>
  <template v-else>
   <p v-if="!suggestions.length">{{status.stale?'Preparing suggestions…':'No uncovered families have enough supporting patterns.'}}</p>
   <div class="family-suggestions"><article v-for="row in suggestions" :key="row.id"><div class="family-controls"><input v-model="names[row.id]" :aria-label="`Suggested family name: ${row.name}`" :disabled="busy" maxlength="128"/><span>{{row.numerator}}/{{row.denominator}} · {{row.feel}} · {{row.patterns.toLocaleString()}} new / {{(row.supportedPatterns??row.patterns).toLocaleString()}} supported · Cohesion {{row.cohesion.toFixed(3)}}</span><button :disabled="busy||status.stale||!names[row.id]?.trim()" @click="attempt(()=>decideSuggestion(row,'accept'))">ACCEPT</button><button :disabled="busy" @click="attempt(()=>decideSuggestion(row,'dismiss'))">DISMISS</button></div><div class="family-controls"><select v-model="targets[row.id]" :aria-label="`Merge target: ${row.name}`" :disabled="busy"><option value="">Choose a compatible family</option><option v-for="entry in compatible(row)" :key="entry.id" :value="entry.id">{{entry.name}}</option></select><button :disabled="busy||status.stale||!targets[row.id]" @click="attempt(()=>decideSuggestion(row,'merge'))">MERGE</button><span v-for="overlap in row.overlap??[]" :key="overlap.familyId">{{status.families.find(f=>f.id===overlap.familyId)?.name??'Existing family'}}: {{overlap.patterns}} shared</span></div><details><summary>Supporting paths</summary><p v-for="example in row.examples" :key="example.patternId" class="family-path">{{example.path}}</p></details></article></div>
   <div class="family-controls"><button :disabled="busy||suggestionOffset===0" @click="attempt(()=>loadSuggestions(Math.max(0,suggestionOffset-10)))">Previous suggestions</button><button :disabled="busy||!suggestionMore" @click="attempt(()=>loadSuggestions(suggestionOffset+10))">Next suggestions</button></div>
  </template>
 </template></dialog>
</details></template>
<style scoped>
.family-panel{flex:none;font:10px monospace;color:var(--text-dim);border:1px solid #ffffff20;border-radius:3px;padding:6px}.family-panel>summary{cursor:pointer}.family-match{margin-left:8px;padding:1px 6px;font:inherit}.family-error{color:var(--c-danger)}.family-panel nav,.family-controls{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin:6px 0}.family-panel nav>span{flex:1;min-width:190px}.family-panel input,.family-panel select{background:var(--lcd-bg);color:var(--text);border:1px solid #ffffff20;border-radius:3px;padding:4px 6px;font:10px monospace;max-width:100%}.family-controls>input{min-width:140px;max-width:220px}.family-panel p{margin:6px 0;line-height:1.5}.family-patterns{max-height:185px;overflow:auto}.family-pattern{display:flex;gap:6px;align-items:center;border-top:1px solid #ffffff12;padding:4px 0}.family-state{min-width:65px}.family-path{overflow-wrap:anywhere}.family-pattern>.family-path{flex:1;min-width:80px}.family-suggestions{max-height:270px;overflow:auto}.family-suggestions article{border-top:1px solid #ffffff20;padding:5px 0}.family-suggestions details summary{cursor:pointer}.family-suggestions details p{padding-left:8px}.family-panel button{flex:none}
.family-dialog{width:min(1050px,88vw);max-height:80vh;overflow:auto;padding:14px;background:var(--bg);color:var(--text-dim);border:1px solid var(--c-primary);border-radius:6px;box-shadow:0 12px 60px #000b;font:12px monospace}.family-dialog::backdrop{background:#0009}.family-dialog>header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:9px;color:var(--c-primary)}.family-dialog .family-patterns{max-height:42vh}.family-dialog .family-suggestions{max-height:48vh}.family-dialog input,.family-dialog select{font-size:12px}.family-dialog .family-controls{gap:8px;margin:9px 0}
</style>
