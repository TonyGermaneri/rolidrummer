<script setup lang="ts">
import { ref, computed, watch, onMounted, onBeforeUnmount, nextTick } from 'vue'
import DataGrid from './DataGrid.vue'
import { TreeGraph } from '../canvas/treeGraph.js'
import { buildTree } from '../core/pathTree.js'
import { type Asset } from '../model'
import { SharedView } from '../lib/sharedView'
import { state, shared, writeShared, attempt, refreshShared } from '../store'
import { request, native } from '../native/bridge'
import { currentTheme } from '../theme/current'
import { begin, htmlDrag } from '../drag'
import { factoryAssets } from '../model'
import { preview, audition, stopPreview } from '../preview'

interface Navigation { path: string; query: string; view: string }
interface Arrangement { camera: unknown; places: Record<string, number[]>; open: string[] }
const props = withDefaults(defineProps<{ patterns?: boolean; patches?: (Asset & {keyCount:number;patternCount:number})[]; currentId?:string; disabled?:boolean }>(),{patterns:false})
const emit=defineEmits<{loadPatch:[id:string]}>()
const isPatches=computed(()=>props.patches!==undefined)
const kind = computed(() => isPatches.value?'patches':props.patterns ? 'patterns' : 'sounds')
const path = ref(''), pathEntry = ref(''), query = ref(''), view = ref('grid'), rows = ref<Asset[]>([])
const chosen = ref<Asset | null>(null), busy = ref(false), box = ref<HTMLElement>()
const visible = computed(() => rows.value.filter(r => r.name.toLowerCase().includes(query.value.toLowerCase())).map(row=>({...row,preview:row.folder?'':preview.id===row.id?'■':'▶',load:row.id===props.currentId?'●':'LOAD'})))
const byId=computed(()=>new Map(rows.value.map(row=>[row.id,row])))
const byName = computed(() => new Map(rows.value.map(row => [row.name, row])))
const columns = computed(()=>isPatches.value?[{name:'load',title:'Load',width:65},{name:'name',title:'Patch',width:300},{name:'keyCount',title:'Keys',width:65},{name:'patternCount',title:'Patterns',width:85}]:[{ name:'preview',title:'Listen',width:58 },{ name: 'name', title: 'Name', width: 350 }, { name: 'folder', title: 'Folder', width: 70 }, { name: 'size', title: 'Bytes', width: 110 }, { name: 'modified', title: 'Modified', width: 210 }])
const labels = ref<{ asset: Asset; x: number; y: number; dim: boolean }[]>([])
const viewKey = computed(() => `browser/${kind.value}/navigation`)
const graphKey = computed(() => `graph/${kind.value}/${path.value}/layout`)
let graph: any = null, frame = 0, remote = false, localLayout = false, loadId = 0, bootId = 0, disposed = false
let navigation: SharedView<Navigation> | undefined, arrangement: SharedView<Arrangement> | undefined
let gridView: SharedView<any> | undefined
const gridState = ref<any>(null)
const gridKey = computed(() => `grid/${kind.value}/${path.value}/view`)
function setupGrid() {
 gridView?.dispose(); gridState.value = null
 const key = gridKey.value
 gridView = new SharedView<any>((value, revision) => writeShared(key, value, revision), value => { gridState.value = value })
 gridView.receive(shared[key])
}
function saveGrid(value: any) { void attempt(async () => { await gridView?.finish(value) }) }
let viewTimer: ReturnType<typeof setTimeout> | undefined

function nodePath(at: number): string {
  const parts: string[] = []
  while (at >= 0 && graph?.source.nodes[at]) { parts.unshift(graph.source.nodes[at].browserId?encodeURIComponent(graph.source.nodes[at].browserId):graph.source.nodes[at].label); at = graph.source.parents[at] }
  return parts.join('/')
}
async function load(next = path.value) {
  const ticket = ++loadId
  busy.value = true
  try {
    let result: Asset[]
    if(isPatches.value){next='';result=[...(props.patches??[])]}
    else if (next) result = await request<Asset[]>('listDirectory', { path: next, patterns: props.patterns })
    else result = props.patterns
      ? state.patch.clips.map(c => ({ id: c.id, path: c.path || c.id, name: c.name, folder: false, size: c.notes.length }))
      : [...factoryAssets(), ...['kick', 'hat', 'snare', 'tom'].map(name => ({ id: `factory:${name}`, path: `factory:${name}`, name: `Source ${name}`, folder: false, size: 0 }))]
    if (ticket !== loadId || disposed) return false
    const selectedId=chosen.value?.id,layout=isPatches.value?graphSnapshot():undefined
    rows.value = result.sort((a, b) => Number(b.folder) - Number(a.folder) || a.name.localeCompare(b.name))
    path.value = next
    pathEntry.value = next
    chosen.value = isPatches.value?rows.value.find(row=>row.id===(selectedId??props.currentId))??null:null
    await nextTick()
    if (ticket !== loadId || disposed) return false
    build(layout)
    return true
  } finally { if (ticket === loadId) busy.value = false }
}
async function navigate(next: string) {
  const active = navigation, ticket = loadId + 1
  clearTimeout(viewTimer)
  active?.begin()
  try { if (await load(next)) remember() }
  catch (error) {
    // A failed directory read must not leave remote navigation deferred forever.
    if (active === navigation && ticket === loadId) active?.cancel()
    throw error
  }
}
async function choose() { const ticket = bootId; const folder = await request<string | null>('chooseFolder'); if (folder && !disposed && ticket === bootId) await navigate(folder) }
function use(asset: Asset) { chosen.value = asset;if(isPatches.value){if(!props.disabled)emit('loadPatch',asset.id);return} if (asset.folder) void attempt(() => navigate(asset.path)) }
function remember() {
  if (remote || disposed) return
  navigation?.begin()
  clearTimeout(viewTimer)
  viewTimer = setTimeout(() => void attempt(async () => { await navigation?.finish({ path: path.value, query: query.value, view: view.value }) }), 150)
}
function touchGraph() { localLayout = true; arrangement?.begin() }
function graphSnapshot():Arrangement|undefined{if(!graph)return;const a=graph.arrangement();const value:Arrangement={camera:a.camera,open:a.open.map(nodePath),places:{}};for(const p of a.places)value.places[nodePath(p[0])]=p.slice(1);return value}
function graphAsset(seat:any):Asset|undefined{return isPatches.value?byId.value.get(graph?.source.nodes[seat.at]?.browserId):byName.value.get(seat.label)}
function saveGraph() {
  if (remote || !graph || !localLayout) return
  const a = graph.arrangement()
  const value: Arrangement = { camera: a.camera, open: a.open.map(nodePath), places: {} }
  for (const p of a.places) value.places[nodePath(p[0])] = p.slice(1)
  arrangement?.begin()
  void attempt(async () => { await arrangement?.finish(value) })
}
function restoreGraph(value: Arrangement) {
  if (!graph) return
  remote = true
  localLayout = false
  try {
    const places: number[][] = [], open: number[] = []
    for (let i = 0; i < graph.source.nodes.length; i++) {
      const id = nodePath(i), position = value.places[id]
      if (position) places.push([i, ...position])
      if (value.open.includes(id)) open.push(i)
    }
    graph.setTree(graph.source, { open, places, camera: value.camera })
    mark()
  } finally { remote = false }
}
function build(preserve?:Arrangement) {
  const retain=isPatches.value&&!!preserve&&!!arrangement,wasLocal=localLayout
  if(!retain)arrangement?.dispose()
  graph?.destroy()
  graph = null
  labels.value = []
  if (disposed || view.value !== 'graph' || !box.value) return
  const tree = buildTree(rows.value.map(a => ({ path: isPatches.value?`Patches/${encodeURIComponent(a.id)}`:`Library/${a.name}` })))
  if(isPatches.value)for(const node of tree.nodes){const asset=byId.value.get(decodeURIComponent(node.label));if(asset){node.browserId=asset.id;node.label=asset.name}}
  graph = new (TreeGraph as any)(box.value, { onPick: (seat: any) => { chosen.value = graphAsset(seat) || null }, onArrange: saveGraph })
  graph.look.ground = currentTheme.value.bg[1]; graph.look.from = currentTheme.value.primary; graph.look.to = currentTheme.value.secondary
  remote = true
  graph.setTree(tree, { open: [0] })
  remote = false
  if(retain&&preserve){restoreGraph(preserve);localLayout=wasLocal;return}
  const key = graphKey.value
  arrangement = new SharedView<Arrangement>((value, revision) => writeShared(key, value, revision), restoreGraph)
  const saved = shared[key]
  if (saved) arrangement.receive(saved)
  else { localLayout = true; arrangement.begin() }
  if(preserve)restoreGraph(preserve)
  mark()
}
function mark() {
  if (!graph) return
  const text = query.value.toLowerCase()
  const marked = text ? new Set<number>() : null
  if (marked) for (let i = 0; i < graph.source.nodes.length; i++) if (graph.source.nodes[i].label.toLowerCase().includes(text)) marked.add(i)
  graph.setMarked(marked)
}
function animate() {
  if (graph) labels.value = graph.drawn.flatMap((seat: any) => {
    const asset = graphAsset(seat), [x, y] = graph.screenOf(seat)
    return asset && x > -100 && y > -20 && x < box.value!.clientWidth && y < box.value!.clientHeight + 20 ? [{ asset, x, y, dim: seat.dim }] : []
  }).slice(0, 200)
  frame = requestAnimationFrame(animate)
}
watch(currentTheme,t=>{if(graph){graph.look.ground=t.bg[1];graph.look.from=t.primary;graph.look.to=t.secondary;graph.paintSoon?.()}})
watch(query, () => { if (!remote) remember(); mark() }, { flush: 'sync' })
watch(view, () => { if (!remote) remember(); void nextTick(()=>build()) }, { flush: 'sync' })
watch(kind, () => {if(preview.id)void attempt(stopPreview);void attempt(boot)})
watch(()=>props.patches,()=>{if(isPatches.value&&navigation)void attempt(()=>load(''))},{deep:true})
watch(()=>props.currentId,id=>{if(isPatches.value)chosen.value=rows.value.find(row=>row.id===id)??null})
watch(() => shared[viewKey.value]?.revision, () => navigation?.receive(shared[viewKey.value]))
watch(() => shared[graphKey.value]?.revision, () => arrangement?.receive(shared[graphKey.value]))
watch(gridKey, setupGrid)
watch(() => shared[gridKey.value]?.revision, () => gridView?.receive(shared[gridKey.value]))
async function boot() {
  const bootTicket = ++bootId
  ++loadId
  clearTimeout(viewTimer)
  navigation?.dispose()
  const key = viewKey.value
  navigation = new SharedView<Navigation>((value, revision) => writeShared(key, value, revision), value => {
    remote = true
    query.value = value.query || ''; view.value = value.view || 'grid'
    remote = false
    if (path.value !== value.path) void attempt(() => load(value.path))
  })
  await refreshShared()
  if (disposed || bootTicket !== bootId) return
  const saved = shared[key]
  if (saved) {
    remote = true
    path.value = saved.value.path || ''; query.value = saved.value.query || ''; view.value = saved.value.view || 'grid'
    remote = false
    navigation.receive(saved)
  } else {
    remote = true
    path.value = ''; query.value = ''; view.value = 'grid'
    remote = false
  }
  setupGrid()
  await attempt(() => load())
}
onMounted(() => { void attempt(boot); animate() })
onBeforeUnmount(() => {if(preview.id)void attempt(stopPreview); disposed = true; ++loadId; cancelAnimationFrame(frame); clearTimeout(viewTimer); navigation?.dispose(); arrangement?.dispose(); gridView?.dispose(); graph?.destroy() })
</script>
<template>
<section class="asset-browser" :class="{'patch-browser':isPatches}">
 <div class="browser-tools">
  <template v-if="!isPatches"><button @click="attempt(choose)">＋ Folder</button><button :disabled="!path" @click="attempt(()=>navigate(path.slice(0,path.lastIndexOf('/'))||'/'))">↑</button>
  <input v-model="pathEntry" aria-label="Library directory" placeholder="Factory library or directory path" @keydown.enter="attempt(()=>navigate(pathEntry))"/><button @click="attempt(()=>load())">↻</button>
  </template><span v-if="isPatches" class="patch-library-title">PATCH LIBRARY</span>
  <input v-model="query" :aria-label="isPatches?'Filter patches':'Filter assets'" :placeholder="isPatches?'Find a patch…':'Filter files…'"/>
  <div class="segmented"><button :class="{active:view==='grid'}" @click="view='grid'">Grid</button><button :class="{active:view==='graph'}" @click="view='graph'">Graph</button></div>
 </div>
 <div class="browser-body">
  <DataGrid v-if="view==='grid'" :rows="visible" :columns="columns" :chosen="chosen||undefined" :view-state="gridState" @interaction="gridView?.begin()" @view="saveGrid" @pick="chosen=$event" @use="use" @drag="(asset:any,event:any)=>!isPatches&&begin(asset,patterns,event)" @cell="(column:string,asset:Asset)=>isPatches?(column==='load'&&use(asset)):(column==='preview'&&attempt(()=>audition(asset,patterns)))" @key="(event:any,asset:Asset)=>{if(!isPatches&&(event.key||event.NativeEvent?.key)===' '&&asset){event.preventDefault();attempt(()=>audition(asset,patterns))}}"/>
  <div v-else ref="box" class="graph-box" @pointerdown.capture="touchGraph" @wheel.capture.passive="touchGraph">
   <div v-for="one in labels" :key="one.asset.id" class="graph-asset" :style="{left:`${one.x}px`,top:`${one.y}px`,opacity:one.dim?.25:1}"><button v-if="!isPatches&&!one.asset.folder" class="preview-button" :aria-label="`${preview.id===one.asset.id?'Stop':'Preview'} ${one.asset.name}`" @pointerdown.stop @click.stop="attempt(()=>audition(one.asset,patterns))">{{preview.id===one.asset.id?'■':'▶'}}</button><button class="graph-label" :class="{'patch-chosen':isPatches&&chosen?.id===one.asset.id}" :draggable="!isPatches&&!one.asset.folder" @dragstart="!isPatches&&htmlDrag($event,one.asset,patterns)" @click="chosen=one.asset" @dblclick="use(one.asset)" @keydown.enter.prevent="use(one.asset)">{{one.asset.folder?'▸ ':''}}{{one.asset.name}}</button></div>
  </div>
  <div v-if="busy" class="loading">{{isPatches?'Updating patches…':'Reading directory…'}}</div>
 </div>
 <footer v-if="isPatches" class="browser-footer"><span>{{visible.length}} patches · Double-click or press Enter to load</span><button class="ssb-button" :disabled="!chosen||disabled" @click="chosen&&use(chosen)">{{chosen?`LOAD ${chosen.name}`:'SELECT A PATCH'}}</button></footer>
 <footer v-else class="browser-footer"><span>{{visible.length}} items · {{patterns?'Drag a MIDI file to a zone':'Drag a sound to a key'}} · ▶ to listen · Double-click folders to open</span><button v-if="preview.id" @click="attempt(stopPreview)">{{preview.loading?'Loading…':'■ Stop preview'}}</button><button v-if="chosen&&!chosen.folder" draggable="true" @dragstart="htmlDrag($event,chosen!,patterns)">{{chosen.name}} ↗</button><span v-if="!native()">Native app required for folders</span></footer>
</section>
</template>
