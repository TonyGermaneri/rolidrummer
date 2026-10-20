<script setup lang="ts">
import {computed,onMounted,onBeforeUnmount,ref,watch} from 'vue'
import {state,sendMidi,attempt,observeKeyTouch} from '../store'
import {colors} from '../model'
import {htmlDrop} from '../drag'
import {request} from '../native/bridge'
import {fitSeaboard} from '../lib/seaboardLayout'
import {useRollClock,keyHitLevels} from '../performance'
import {keyOrigins} from '../lib/keyMapping'
import {seaboardKeys,keySlide,keyBed,ribbonLanes,surfaceAtPoint,touchPoint,type RibbonAction,type SeaboardKey} from '../lib/seaboardKeys'
const emit=defineEmits<{patch:[direction:number]}>()
const props=defineProps<{base:number}>()
const stage=ref<HTMLElement|null>(null),surface=ref({width:0,height:0})
let observer:ResizeObserver|undefined
function fit(){if(stage.value)surface.value=fitSeaboard(stage.value.clientWidth,stage.value.clientHeight)}
onMounted(()=>{fit();if(typeof ResizeObserver!=='undefined'){observer=new ResizeObserver(fit);observer.observe(stage.value!)}else window.addEventListener('resize',fit)})
onBeforeUnmount(()=>{observer?.disconnect();window.removeEventListener('resize',fit)})
const blackPitch=new Set([1,3,6,8,10])
const {left,top,width,height}=keyBed,whiteWidth=width/29
const keyboard=computed(()=>seaboardKeys(props.base))
const lanes=computed(()=>ribbonLanes(keyboard.value))
function outline(key:typeof keyboard.value[number]) {
 const x=key.x+1,w=key.w-2,y=top,h=key.h;
 if(key.black)return `M ${x+3} ${y+3} Q ${x+w/2} ${y} ${x+w-2} ${y+3} L ${x+w} ${y+h-7} Q ${x+w} ${y+h} ${x+w/2} ${y+h} Q ${x} ${y+h} ${x} ${y+h-7} Z`;
 const pitch=(key.note-props.base)%12;
 const insetL=blackPitch.has((pitch+11)%12)?whiteWidth*.29:1;
 const insetR=blackPitch.has((pitch+1)%12)?whiteWidth*.29:1;
 return `M ${x+insetL} ${y+3} Q ${x+w/2} ${y-1} ${x+w-insetR} ${y+3} L ${x+w-insetR} ${y+h*.51} Q ${x+w-insetR} ${y+h*.6} ${x+w-1} ${y+h*.64} L ${x+w} ${y+h-5} Q ${x+w/2} ${y+h+1} ${x} ${y+h-5} L ${x+1} ${y+h*.64} Q ${x+insetL} ${y+h*.6} ${x+insetL} ${y+h*.51} Z`;
}
const hitTime=useRollClock()
const hits=computed(()=>keyHitLevels(hitTime.value))
const keys=computed(()=>new Map(state.patch.keys.map(k=>[k.note,k])))
const origins=computed(()=>keyOrigins(state.patch.keys))
function related(f:{note:number;origin?:number}){return [f.note,f.origin??origins.value[f.note]]}
const held=computed(()=>{const result=new Map<number,number>();for(const f of state.telemetry.fingers??[])if(f.playing!==false)for(const note of related(f))result.set(note,f.zone);return result})
const latchActions=computed(()=>{const result=new Map<number,'start'|'stop'>();for(const latch of state.telemetry.pendingLatches??[])for(const note of related(latch))result.set(note,latch.action);return result})
const queued=computed(()=>new Set([...latchActions.value.keys(),...(state.telemetry.fingers??[]).filter(f=>f.queued).flatMap(related)]))
const toggled=computed(()=>new Set((state.telemetry.fingers??[]).filter(f=>f.toggled).flatMap(related)))
const parallelOrigins=computed(()=>new Set((state.telemetry.fingers??[]).filter(f=>f.playing!==false&&f.note!==(f.origin??origins.value[f.note])).map(f=>f.origin??origins.value[f.note])))
const modes=[{id:'normal',label:'Normal',help:'Release a key to stop its part. The sustain pedal can hold released keys.'},{id:'hold',label:'Hold',help:'Keep released keys playing. Pedal up clears released keys.'},{id:'toggle',label:'Toggle',help:'Press a key to latch it; press the same zone to stop. A different zone replaces it. Pedal up clears released keys.'}] as const
const shortNames:Record<string,string>={'Closed hat':'CH','Open hat':'OH','Pedal hat':'PH','High tom':'HT','Mid tom':'MT','Low tom':'LT','High conga':'HC','Low conga':'LC','Cowbell':'Bell','Shaker':'Shkr'}
function keyLabel(name=''){return shortNames[name]??(name.length>5?name.slice(0,4)+'…':name)}
type TouchArea='key'|RibbonAction
type Contact={channel:number;svg:SVGSVGElement;key:SeaboardKey;area:TouchArea;playing:boolean}
const contacts=new Map<number,Contact>()
const pointerTouches=ref(new Map<number,{id:string;channel:number;note:number;x:number;y:number;pressure:number;area:TouchArea}>())
const keyboardContacts=ref(new Map<number,{channel:number;area:'key'|'top'}>())
function touchColor(id:string){let hash=0;for(const char of id)hash=(hash*31+char.charCodeAt(0))%360;return `hsl(${(hash*137.508)%360} 75% 75%)`}
const physicalLocations=computed(()=>{
 const virtual=[...keyboardContacts.value].flatMap(([note,contact])=>{const key=keyboard.value.find(key=>key.note===note);return key?[{id:`keyboard:${note}`,channel:contact.channel,note,x:key.x+key.w/2,y:contact.area==='top'?(keyBed.surfaceTop+top)/2:top+key.h/2,pressure:.5,area:contact.area as TouchArea}]:[]})
 const local=[...pointerTouches.value.values(),...virtual].map(touch=>({...touch,touch:(state.telemetry.touches??[]).find(t=>!t.surface&&t.channel===touch.channel)?.id,edge:undefined as string|undefined}))
 return [...(state.telemetry.touches??[]).filter(touch=>touch.surface).map(touch=>({...touch,...touchPoint(keyboard.value,touch),id:`touch:${touch.id}`,touch:touch.id,area:'key' as TouchArea})),...local].map(touch=>({...touch,radius:2.7+Math.max(0,Math.min(1,touch.pressure))*1.7,color:touchColor(touch.touch||touch.id)}))
})
const echoLocations=computed(()=>{
 const playing=[...(state.telemetry.fingers??[])].filter(f=>f.playing!==false).flatMap(f=>{const key=keyboard.value.find(k=>k.note===f.note);return key?[{...f,id:`owner:${f.owner??f.channel}:${f.zone}`,origin:f.origin??origins.value[f.note],action:undefined as 'start'|'stop'|undefined,x:key.x+key.w*(.5+(f.glide-.5)*.7),y:top+3+(1-f.slide)*(key.h-6),radius:5.8}]:[]})
 const pending=(state.telemetry.pendingLatches??[]).flatMap(latch=>{const key=keyboard.value.find(k=>k.note===latch.note);return key?[{...latch,id:`queue:${latch.origin}:${latch.action}:${latch.touch??''}`,owner:undefined,channel:-1,zone:-1,sustained:false,toggled:false,x:key.x+key.w/2,y:top+key.h-10,radius:5.8}]:[]})
 return [...playing,...pending].map(echo=>{
  const source=physicalLocations.value.find(touch=>echo.touch&&echo.touch!=='0'&&touch.touch===echo.touch)??(echo.action==='stop'?physicalLocations.value.find(touch=>touch.area==='stop'&&origins.value[touch.note]===echo.origin):undefined)
  return {...echo,source,sourceId:source?.touch?`touch:${source.touch}`:source?.id??(echo.touch?`touch:${echo.touch}`:undefined),color:source?.color??touchColor(echo.touch||echo.id),fromX:(source?.x??echo.x)-echo.x,fromY:(source?.y??echo.y)-echo.y}
 })
})
const macro=ref([...state.telemetry.macros])
const touching=new Set<number>()
watch(()=>state.telemetry.macros,values=>{values.forEach((value,i)=>{if(!touching.has(i))macro.value[i]=value})})
function zoneBottom(zones:{upper:number}[],i:number){return i?zones[i-1].upper:0}
function usedChannels(){const used=new Set([...contacts.values()].filter(c=>c.channel>=0).map(c=>c.channel).concat([...keyboardContacts.value.values()].map(c=>c.channel)));state.telemetry.contacts.forEach((value,ch)=>{if(value)used.add(ch)});for(const touch of state.telemetry.touches??[])used.add(touch.channel);return used}
function selectKey(note:number){const origin=origins.value[note];if(keys.value.has(note))state.selected=note;else if(state.trackKeywave&&origin>=0)state.selected=origin}
function point(svg:SVGSVGElement,e:PointerEvent){const p=svg.createSVGPoint();p.x=e.clientX;p.y=e.clientY;return p.matrixTransform(svg.getScreenCTM()!.inverse())}
function recordPointer(e:PointerEvent,c:Contact){const p=point(c.svg,e);pointerTouches.value.set(e.pointerId,{id:`pointer:${e.pointerId}`,channel:c.channel,note:c.key.note,x:p.x,y:p.y,pressure:e.pressure||.2,area:c.area})}
function stopKey(note:number){selectKey(note);observeKeyTouch([0x90,note,1]);void attempt(()=>request('ribbon',{action:'stop',note}))}
function enter(c:Contact,key:SeaboardKey,area:TouchArea,e?:PointerEvent){
 if(area==='stop')stopKey(key.note)
 if(c.playing){sendMidi([0x80|c.channel,c.key.note,64]);c.playing=false}
 c.key=key;c.area=area
 if(area==='stop')return
 if(c.channel<0)return
 selectKey(key.note);sendMidi([0xe0|c.channel,0,64]);sendMidi([0xd0|c.channel,100,0])
 sendMidi([0xb0|c.channel,74,area==='top'?127:Math.round(keySlide(key,e?point(c.svg,e).y:top+key.h/2)*127)])
 if(area==='top'){observeKeyTouch([0x90,key.note,100]);void attempt(()=>request('ribbon',{action:'top',note:key.note,channel:c.channel,velocity:100}))}
 else sendMidi([0x90|c.channel,key.note,100])
 c.playing=true
}
function down(e:PointerEvent){
 if(e.button!==0)return
 const target=e.currentTarget as SVGElement,svg=target.ownerSVGElement!,actual=point(svg,e),touch=surfaceAtPoint(keyboard.value,actual.x,actual.y)
 if(!touch)return
 const used=usedChannels(),channel=Array.from({length:15},(_,i)=>i+1).find(c=>!used.has(c))??-1
 target.setPointerCapture(e.pointerId)
 const c:Contact={channel,svg,key:touch.key,area:touch.area,playing:false};contacts.set(e.pointerId,c);recordPointer(e,c);enter(c,touch.key,touch.area,e)
}
function move(e:PointerEvent){
 const c=contacts.get(e.pointerId);if(!c)return
 recordPointer(e,c)
 const actual=point(c.svg,e),touch=surfaceAtPoint(keyboard.value,actual.x,actual.y);if(!touch)return
 if(touch.key.note!==c.key.note){
  if(touch.area!=='key'&&touch.area===c.area){
   const from=keyboard.value.findIndex(key=>key.note===c.key.note),to=keyboard.value.findIndex(key=>key.note===touch.key.note),step=to>from?1:-1
   for(let i=from+step;i!==to+step;i+=step)enter(c,keyboard.value[i],touch.area,e)
  }else enter(c,touch.key,touch.area,e)
 }else if(touch.area==='stop'){
  if(c.area!=='stop')enter(c,touch.key,touch.area,e)
 }else if(c.area==='stop')enter(c,touch.key,touch.area,e)
 else {
  if(touch.area==='top'&&c.area!=='top'&&c.channel>=0)void attempt(()=>request('ribbon',{action:'moveTop',note:touch.key.note,channel:c.channel}))
  c.area=touch.area;c.key=touch.key
  if(c.channel>=0){sendMidi([0xb0|c.channel,74,touch.area==='top'?127:Math.round(keySlide(touch.key,actual.y)*127)])
  if(e.pressure>0)sendMidi([0xd0|c.channel,Math.round(e.pressure*127),0])}
 }
 recordPointer(e,c)
}
function up(e:PointerEvent){const c=contacts.get(e.pointerId);if(c){if(c.playing)sendMidi([0x80|c.channel,c.key.note,64]);contacts.delete(e.pointerId)}pointerTouches.value.delete(e.pointerId)}
function ribbonDown(e:KeyboardEvent,note:number,action:RibbonAction){
 if(e.repeat)return
 if(action==='stop'){stopKey(note);return}
 if(keyboardContacts.value.has(note))return
 const channel=Array.from({length:15},(_,i)=>i+1).find(c=>!usedChannels().has(c));if(channel===undefined)return
 keyboardContacts.value.set(note,{channel,area:'top'});selectKey(note);observeKeyTouch([0x90,note,100]);sendMidi([0xe0|channel,0,64]);sendMidi([0xd0|channel,100,0]);void attempt(()=>request('ribbon',{action:'top',note,channel,velocity:100}))
}
function keyboardDown(e:KeyboardEvent,note:number){
 if(e.key!==' '&&e.key!=='Enter')return;e.preventDefault();selectKey(note)
 if(e.repeat||keyboardContacts.value.has(note)||origins.value[note]<0)return
 const used=usedChannels(),channel=Array.from({length:15},(_,i)=>i+1).find(c=>!used.has(c))
 if(channel===undefined)return;keyboardContacts.value.set(note,{channel,area:'key'});sendMidi([0xe0|channel,0,64]);sendMidi([0xd0|channel,100,0]);sendMidi([0xb0|channel,74,64]);sendMidi([0x90|channel,note,100])
}
function keyboardUp(note:number){const contact=keyboardContacts.value.get(note);if(contact){sendMidi([0x80|contact.channel,note,64]);keyboardContacts.value.delete(note)}}
function panic(){for(const contact of contacts.values())if(contact.playing)sendMidi([0x80|contact.channel,contact.key.note,64]);for(const [note,contact] of keyboardContacts.value)sendMidi([0x80|contact.channel,note,64]);contacts.clear();pointerTouches.value.clear();keyboardContacts.value.clear();touching.clear()}
watch(()=>props.base,panic)
function cc(i:number,v:number){macro.value[i]=v;state.telemetry.macros[i]=v;void attempt(()=>request('parameter',{id:`macro${i}`,value:v}))}
function padKey(e:KeyboardEvent){if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(e.key))return;e.preventDefault();const step=e.shiftKey?.1:.01;if(e.key==='ArrowLeft')cc(3,Math.max(0,macro.value[3]-step));else if(e.key==='ArrowRight')cc(3,Math.min(1,macro.value[3]+step));else if(e.key==='ArrowUp')cc(4,Math.min(1,macro.value[4]+step));else if(e.key==='ArrowDown')cc(4,Math.max(0,macro.value[4]-step))}
function xy(e:PointerEvent){touching.add(3);touching.add(4);const el=e.currentTarget as HTMLElement;el.setPointerCapture(e.pointerId);const rect=el.getBoundingClientRect();const move=(ev:PointerEvent)=>{cc(3,Math.max(0,Math.min(1,(ev.clientX-rect.left)/rect.width)));cc(4,Math.max(0,Math.min(1,1-(ev.clientY-rect.top)/rect.height)))};move(e);const up=()=>{touching.delete(3);touching.delete(4);el.removeEventListener('pointermove',move);el.removeEventListener('pointerup',up);el.removeEventListener('pointercancel',up)};el.addEventListener('pointermove',move);el.addEventListener('pointerup',up);el.addEventListener('pointercancel',up)}
window.addEventListener('blur',panic);onBeforeUnmount(()=>{window.removeEventListener('blur',panic);panic()})
</script>
<template><div class="keyboard-performance"><div class="keyboard-modes" role="group" aria-label="Keyboard mode"><span>KEY MODE</span><button v-for="mode in modes" :key="mode.id" class="ssb-button" :class="{active:state.patch.keyboardMode===mode.id}" :aria-pressed="state.patch.keyboardMode===mode.id" :title="mode.help" @click="state.patch.keyboardMode=mode.id">{{mode.label}}</button><button class="ssb-button latch-quantize" :class="{active:state.patch.latchQuantize}" :aria-pressed="Boolean(state.patch.latchQuantize)" aria-label="Quantize latch and unlatch" title="Apply Hold and Toggle changes at the next selected bar boundary" @click="state.patch.latchQuantize=!state.patch.latchQuantize">LATCH QUANTIZE</button><select v-model.number="state.patch.latchBars" aria-label="Latch quantization bars"><option :value="0">Instant</option><option v-for="bars in Array.from({length:16},(_,i)=>i+1)" :key="bars" :value="bars">{{bars}} {{bars===1?'bar':'bars'}}</option></select><small v-if="state.patch.keyboardMode!=='normal'">Pedal up clears released keys</small></div><div ref="stage" class="seaboard-stage"><div class="seaboard" :style="{width:`${surface.width}px`,height:`${surface.height}px`}"><svg class="seaboard-svg" viewBox="49 115 705 210" preserveAspectRatio="xMidYMid meet" role="group" aria-label="Seaboard RISE 49 keywaves"><defs>
 <linearGradient id="rd-case" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#505253"/><stop offset=".055" stop-color="#343637"/><stop offset=".65" stop-color="#303233"/><stop offset="1" stop-color="#1c1e20"/></linearGradient>
 <linearGradient id="rd-bed" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#111416"/><stop offset=".13" stop-color="#272a2b"/><stop offset=".82" stop-color="#171a1c"/><stop offset="1" stop-color="#0c0f11"/></linearGradient>
 <linearGradient id="rd-natural"><stop stop-color="#101315"/><stop offset=".22" stop-color="#292d2f"/><stop offset=".48" stop-color="#363a3c"/><stop offset=".63" stop-color="#292d2f"/><stop offset="1" stop-color="#101315"/></linearGradient>
 <linearGradient id="rd-sharp"><stop stop-color="#080b0d"/><stop offset=".28" stop-color="#191d1f"/><stop offset=".52" stop-color="#323638"/><stop offset=".67" stop-color="#262a2c"/><stop offset="1" stop-color="#080b0d"/></linearGradient>
 <linearGradient id="rd-ridge" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#b2b8b9" stop-opacity=".05"/><stop offset=".15" stop-color="#9ca4a7" stop-opacity=".7"/><stop offset=".8" stop-color="#7c8589" stop-opacity=".5"/><stop offset="1" stop-color="#526069" stop-opacity="0"/></linearGradient>
 <linearGradient id="rd-ribbon" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#171b1d"/><stop offset=".45" stop-color="#292d2f"/><stop offset="1" stop-color="#15191b"/></linearGradient>

 </defs>
<g aria-hidden="true">
 <rect x="51" y="141" width="704" height="177" rx="5" fill="#000" opacity=".4"/><rect x="50" y="138" width="704" height="177" rx="5" fill="url(#rd-case)"/>
 <path d="M 55 139 H 749" stroke="#909596" stroke-opacity=".2"/>
 <rect x="135" y="155" width="595" height="140" rx="4" fill="#141719" stroke="#090c0e" stroke-width=".65"/>
 <rect x="141" y="160" width="583" height="130" rx="8" fill="url(#rd-bed)"/>
 <rect x="149" y="163" width="573" height="12" rx="5" fill="url(#rd-ribbon)"/>
 <rect x="149" y="272" width="573" height="16" rx="5" fill="url(#rd-ribbon)"/>
 <path d="M 67 154 Q 70 153 72 153 C 77 153 77 159 70 159 L 76 165" fill="none" stroke="#969c9e" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"/>
 <rect x="89" y="157" width="33" height="11" rx="4" fill="#1d2123" stroke="#14171a"/>
 <path d="M 98 160 L 96 162 L 98 164 M 114 160 L 116 162 L 114 164" stroke="#777f83" stroke-width=".6" fill="none"/>
 <g v-for="x in [70,90,110]" :key="x"><rect :x="x" y="175" width="12" height="52" rx="3" fill="#171b1d" stroke="#101315"/><path :d="`M ${x+5} 181 V 223`" stroke="#656e72" stroke-width=".5"/><circle :cx="x+6" cy="179" r=".8" fill="#a8afb3"/></g>
 <rect x="70" y="234" width="52" height="41" rx="3" fill="#1b1f22" stroke="#14181a"/><path d="M 96 240 v 2 M 96 267 v 2 M 77 254 h 2 M 113 254 h 2" stroke="#7d878d" stroke-width=".8"/>
 <circle cx="76" cy="288" r="6" fill="#171b1d" stroke="#101315"/><circle cx="76" cy="288" r="1.2" fill="#7c9194"/>
 <rect x="89" y="282" width="33" height="12" rx="4" fill="#191d1f" stroke="#111517"/>
 <path d="M 98 286 L 96 288 L 98 290 M 114 286 L 116 288 L 114 290" stroke="#747e84" stroke-width=".6" fill="none"/>
</g>
<g v-for="(direction,i) in [-1,1]" :key="direction" class="preset-button" role="button" tabindex="0" :aria-label="direction<0?'Previous patch':'Next patch'" @click="emit('patch',direction)" @keydown.enter.prevent="emit('patch',direction)" @keydown.space.prevent="emit('patch',direction)"><title>{{direction<0?'Previous patch':'Next patch'}}</title><rect :x="89+i*16.5" y="157" width="16.5" height="11" rx="3" fill="transparent"/></g>
<rect class="keybed-hit" :x="left" :y="keyBed.surfaceTop" :width="width" :height="keyBed.surfaceBottom-keyBed.surfaceTop" fill="transparent" @pointerdown="down" @pointermove="move" @pointerup="up" @pointercancel="up"/><defs><clipPath v-for="key in keyboard" :id="`keyclip-${key.note}`" :key="key.note"><path :d="outline(key)"/></clipPath></defs>
<g v-for="black in [false,true]" :key="String(black)"><g v-for="key in keyboard.filter(k=>k.black===black)" :key="key.note" class="seaboard-key" :class="{selected:state.selected===key.note,held:held.has(key.note),toggled:toggled.has(key.note),sounding:hits.has(key.note),queued:queued.has(key.note),'parallel-origin':parallelOrigins.has(key.note),black}" :data-origin="origins[key.note]" :data-parallel="!keys.has(key.note)" :data-drop="`key:${key.note}`" :aria-label="keys.get(key.note)?.name||`Note ${key.note} · Parallel to ${keys.get(origins[key.note])?.name||'unassigned'}`" :aria-pressed="state.patch.keyboardMode==='toggle'?toggled.has(key.note):undefined" tabindex="0" role="button" @focus="selectKey(key.note)" @keydown="keyboardDown($event,key.note)" @keyup="($event.key===' '||$event.key==='Enter')&&keyboardUp(key.note)" @blur="keyboardUp(key.note)" @pointerdown="down($event)" @pointermove="move" @pointerup="up" @pointercancel="up" @dragover.prevent @drop="htmlDrop($event,`key:${key.note}`)">
<path class="key-hit" :d="outline(key)" :fill="key.black?'url(#rd-sharp)':'url(#rd-natural)'"/>
<path :d="`M ${key.x+key.w*.47} ${top+5} V ${top+key.h*(key.black?.82:.48)}`" stroke="url(#rd-ridge)" :stroke-width="key.black?.8:1" fill="none" pointer-events="none"/>
<path :d="`M ${key.x+key.w*.54} ${top+5} V ${top+key.h*(key.black?.82:.48)}`" stroke="#000" stroke-opacity=".3" stroke-width=".7" fill="none" pointer-events="none"/>
<g :clip-path="`url(#keyclip-${key.note})`" pointer-events="none"><rect v-for="(zone,i) in keys.get(key.note)?.zones||state.patch.templateZones" :key="i" :x="key.x+1" :y="top+(1-zone.upper)*key.h" :width="key.w-2" :height="(zone.upper-zoneBottom(keys.get(key.note)?.zones||state.patch.templateZones,i))*key.h" :fill="colors[(keys.get(key.note)?.zones||state.patch.templateZones).length-1-i]" :opacity="held.get(key.note)===i?.65:keys.has(key.note)?.25:.045"/></g>
<path v-if="toggled.has(key.note)" class="key-toggled" :d="outline(key)" pointer-events="none"/>
<path v-if="hits.has(key.note)" class="key-sounding" :data-playing-key="key.note" :style="{opacity:hits.get(key.note)}" :d="outline(key)" pointer-events="none"/><path v-if="state.selected===key.note" class="key-selected" :d="outline(key)" pointer-events="none"/><path class="key-outline" :d="outline(key)" pointer-events="none"/>
<g v-if="latchActions.has(key.note)" class="key-latch-pending" :class="`latch-${latchActions.get(key.note)}`" :data-queued-key="key.note" :data-queued-action="latchActions.get(key.note)" role="img" :aria-label="latchActions.get(key.note)==='start'?'Queued start':'Queued stop'" :transform="`translate(${key.x+key.w/2} ${top+key.h-10})`" pointer-events="none"><title>{{latchActions.get(key.note)==='start'?'Starts':'Stops'}} at the next quantized boundary</title><circle r="4.2"/><path v-if="latchActions.get(key.note)==='start'" d="M -1.1 -2 L 2 0 L -1.1 2 Z"/><rect v-else x="-1.5" y="-1.5" width="3" height="3"/></g>
</g></g>
<g v-for="action in (['top','stop'] as const)" :key="action" class="performance-ribbon" :class="`ribbon-${action}`" :aria-label="action==='top'?'Top-zone ribbon':'Stop ribbon'"><g v-for="lane in lanes" :key="lane.note" class="ribbon-lane" :class="{black:lane.black,held:held.has(lane.note),queued:queued.has(lane.note)}" role="button" tabindex="0" :data-ribbon-note="lane.note" :data-ribbon-action="action" :aria-label="`${action==='top'?'Top zone':'Stop'} · ${keys.get(lane.note)?.name||`Note ${lane.note} · ${keys.get(origins[lane.note])?.name||'unassigned'}`}`" @pointerdown="down" @pointermove="move" @pointerup="up" @pointercancel="up" @keydown.enter.prevent="ribbonDown($event,lane.note,action)" @keydown.space.prevent="ribbonDown($event,lane.note,action)" @keyup="($event.key===' '||$event.key==='Enter')&&keyboardUp(lane.note)" @blur="keyboardUp(lane.note)"><title>{{action==='top'?'Top zone':'Stop'}} · {{keys.get(lane.note)?.name||`Note ${lane.note}`}}</title><rect :x="lane.x" :y="action==='top'?keyBed.surfaceTop:top+height+.1" :width="lane.w" :height="action==='top'?top-keyBed.surfaceTop:keyBed.surfaceBottom-top-height-.1"/><path v-if="action==='top'" :d="`M ${lane.x+lane.w/2-1.5} ${top-4} L ${lane.x+lane.w/2} ${top-7} L ${lane.x+lane.w/2+1.5} ${top-4}`"/><rect v-else class="stop-mark" :x="lane.x+lane.w/2-1.3" :y="top+height+6" width="2.6" height="2.6"/></g></g>
<g class="finger-indicators" pointer-events="none" aria-label="Physical fingers and command echoes">
 <g class="finger-emissions" aria-hidden="true"><path v-for="echo in echoLocations.filter(echo=>echo.source)" :key="echo.id" :data-emission="echo.id" :data-source-touch="echo.sourceId" :stroke="echo.color" :d="`M ${echo.source!.x} ${echo.source!.y} L ${echo.x} ${echo.y}`"/></g>
 <g v-for="echo in echoLocations" :key="echo.id" class="command-echo" :class="{sustained:echo.sustained,toggled:echo.toggled,pending:echo.action,'stop-echo':echo.action==='stop'}" :data-owner="echo.owner" :data-echo="echo.id" :data-source-touch="echo.sourceId" :data-sustained="!!echo.sustained" :data-toggled="!!echo.toggled" :aria-label="`${echo.action?`Queued ${echo.action}`:echo.toggled?'Toggled':echo.sustained?'Sustained':'Playing'} note ${echo.note}`" :style="{'--touch-color':echo.color,'--emit-x':`${echo.fromX}px`,'--emit-y':`${echo.fromY}px`}" :transform="`translate(${echo.x} ${echo.y})`"><title>{{echo.action?`Queued ${echo.action}`:echo.toggled?'Toggled on':echo.sustained?'Held after release':'Playing keywave'}} · command echo</title><circle :r="echo.radius" class="echo-ring"/><circle :r="echo.radius" class="echo-pulse"/></g>
 <g v-for="finger in physicalLocations" :key="finger.id" class="physical-finger" :data-finger="finger.channel" :data-touch="finger.id" :data-source-touch="finger.touch?`touch:${finger.touch}`:finger.id" :data-edge="finger.edge" :style="{'--touch-color':finger.color}" :aria-label="`Finger touching note ${finger.note}`" role="img" :transform="`translate(${finger.x} ${finger.y})`"><title>Physical finger · note {{finger.note}}{{finger.edge?' · outside visible range':''}}</title><circle :r="finger.radius+2.8" class="finger-glow"/><circle :r="finger.radius" class="finger-dot"/><circle r="1.1" fill="white"/></g>
</g>
<text v-for="key in keyboard.filter(k=>keys.has(k.note))" :key="key.note" class="seaboard-key-label" :x="key.x+key.w/2" :y="key.black?121:134" text-anchor="middle"><title>{{keys.get(key.note)?.name}}</title>{{keyLabel(keys.get(key.note)?.name)}}</text>
</svg>
<div class="seaboard-faders"><input v-for="i in 3" :key="i" :value="macro[i-1]" type="range" min="0" max="1" step=".001" :aria-label="`Global controller ${i}`" @pointerdown="touching.add(i-1)" @pointerup="touching.delete(i-1)" @pointercancel="touching.delete(i-1)" @keydown="touching.add(i-1)" @keyup="touching.delete(i-1)" @blur="touching.delete(i-1)" @input="cc(i-1,Number(($event.target as HTMLInputElement).value))"/></div>
<div class="seaboard-xy" role="group" tabindex="0" aria-label="Global XY pad. Use arrow keys to adjust." @keydown="padKey" @pointerdown="xy"><i :style="{left:`${macro[3]*100}%`,bottom:`${macro[4]*100}%`}"/></div>
<button class="seaboard-bindings" aria-label="Global MIDI bindings" @click="state.settingsSection='midi';state.tab='Settings'">⚙</button>
</div></div></div></template>
<style scoped>
.physical-finger .finger-glow{fill:var(--touch-color);opacity:.3;filter:blur(1.3px)}.physical-finger .finger-dot{fill:#edffff;fill-opacity:.92;stroke:var(--touch-color);stroke-width:1;filter:drop-shadow(0 0 3px var(--touch-color))}.finger-emissions path{fill:none;stroke-width:.65;stroke-opacity:.4;stroke-dasharray:1 1}.command-echo .echo-ring{fill:var(--touch-color);fill-opacity:.07;stroke:var(--touch-color);stroke-opacity:.85;stroke-width:.8}.command-echo.sustained .echo-ring{stroke-dasharray:1.5 1;fill-opacity:.1}.command-echo.toggled .echo-ring{stroke:var(--key-toggle);fill:var(--key-toggle);fill-opacity:.15}.command-echo.pending .echo-ring{stroke:#4cdb88;stroke-dasharray:1.4 1}.command-echo.stop-echo .echo-ring{stroke:#ff666d}.echo-pulse{fill:none;stroke:var(--touch-color);stroke-width:.8;opacity:0;animation:emit-command .32s ease-out both;transform-box:view-box;transform-origin:0 0}@keyframes emit-command{0%{transform:translate(var(--emit-x),var(--emit-y)) scale(.45);opacity:.95}65%{opacity:.7}100%{transform:translate(0,0) scale(1.4);opacity:0}}@media(prefers-reduced-motion:reduce){.echo-pulse{animation:none}}
.keyboard-performance{flex:1 1 0;min-width:0;min-height:0;display:flex;flex-direction:column;--key-toggle:#be86ef}.keyboard-modes{display:flex;align-items:center;gap:5px;flex-wrap:wrap;flex:none;padding:0 5px 3px}.keyboard-modes>span{font:7px 'Orbitron',sans-serif;letter-spacing:.1em;color:var(--text-dim);margin-right:3px}.keyboard-modes button{font-size:8px;padding:3px 8px}.keyboard-modes button.active{background:var(--c-primary);border-color:var(--c-primary);color:var(--bg)}.keyboard-modes small{font-size:9px;margin-left:5px}.seaboard-key.queued .key-outline{stroke:var(--c-primary);stroke-dasharray:2 2;stroke-width:1.4}.key-sounding{fill:var(--c-primary);stroke:#fff;stroke-opacity:.65;stroke-width:1.3;filter:drop-shadow(0 0 3px var(--c-primary))}.key-toggled{fill:var(--key-toggle);fill-opacity:.52;stroke:var(--key-toggle);stroke-width:1.2}.seaboard-key.toggled.held .key-selected{opacity:.12}.finger-indicators .toggled .finger-glow{fill:var(--key-toggle);opacity:.35}.finger-indicators .toggled .finger-dot{fill:var(--key-toggle);filter:drop-shadow(0 0 2px var(--key-toggle))}

.performance-ribbon{cursor:pointer;touch-action:none;outline:none;color:#4cdb88}.performance-ribbon.ribbon-stop{color:#ff666d}.performance-ribbon rect{fill:currentColor;fill-opacity:.05;stroke:currentColor;stroke-opacity:.25;stroke-width:.5}.ribbon-lane:hover>rect,.ribbon-lane:focus-visible>rect,.ribbon-lane.held>rect{fill-opacity:.25;stroke-opacity:.8}.ribbon-lane{outline:none}.ribbon-lane.black>rect{fill-opacity:.12}.ribbon-lane.queued>rect{stroke-dasharray:1 1;stroke-opacity:1}.ribbon-lane path{fill:none;stroke:currentColor;stroke-width:.6;pointer-events:none;opacity:.6}.ribbon-lane .stop-mark{fill-opacity:.6;stroke:none;pointer-events:none}.performance-ribbon text{fill:currentColor;fill-opacity:.65;font:5px system-ui;letter-spacing:.7px}.key-latch-pending{color:#4cdb88}.key-latch-pending.latch-stop{color:#ff666d}.key-latch-pending circle{fill:currentColor;stroke:#07100b;stroke-width:.8;filter:drop-shadow(0 0 2px currentColor)}.key-latch-pending path,.key-latch-pending rect{fill:#07100b}.preset-button{cursor:pointer}.preset-button:hover rect,.preset-button:focus-visible rect{fill:#ffffff18;stroke:var(--c-primary);stroke-width:.4}.preset-button:focus{outline:none}
.seaboard-stage{overflow:hidden;flex:1 1 0;align-self:stretch;min-width:0;min-height:0;position:relative;display:flex;align-items:center;justify-content:center}.seaboard{flex:none;position:relative;min-width:0;border:0;background:transparent;display:block}.seaboard-svg{width:100%;height:100%;display:block;overflow:hidden}.seaboard-key{cursor:pointer;touch-action:none;outline:none}.key-hit{stroke:#070a0c;stroke-opacity:.45;stroke-width:.4}.key-outline{fill:none;stroke:transparent;stroke-width:.9}.seaboard-key.parallel-origin .key-outline{stroke:var(--c-primary);stroke-width:1.7;filter:drop-shadow(0 0 3px var(--c-primary))}.keybed-hit{touch-action:none}.seaboard-key:hover .key-outline{stroke:#a5abb3;stroke-opacity:.5}.key-selected{fill:var(--c-primary,#d4a84b);opacity:.3;stroke:none}.seaboard-key.held .key-selected{opacity:.45}.finger-glow{fill:var(--c-secondary,#8fb3d6);opacity:.2}.finger-dot{fill:var(--c-secondary,#8fb3d6);stroke:white;stroke-opacity:.75;stroke-width:.6;filter:drop-shadow(0 0 2px var(--c-secondary,#8fb3d6))}.sustained .finger-glow{fill:var(--c-primary,#d4a84b);opacity:.3}.sustained .finger-dot{fill:var(--c-primary,#d4a84b);stroke-dasharray:1.5 1;filter:drop-shadow(0 0 2px var(--c-primary,#d4a84b))}.seaboard-key-label{fill:var(--text-dim,#b7b3aa);font:6px system-ui;pointer-events:none}.seaboard-faders{position:absolute;left:3%;top:29%;width:7.3%;height:25%;display:flex;justify-content:space-around;gap:5%}.seaboard-faders input{writing-mode:vertical-lr;direction:rtl;width:20%;height:100%;appearance:none;-webkit-appearance:none;background:transparent;cursor:ns-resize}.seaboard-faders input::-webkit-slider-runnable-track{width:1px;background:#737b7e}.seaboard-faders input::-webkit-slider-thumb{appearance:none;-webkit-appearance:none;width:5px;height:2px;background:#c2c9ca;border:0;border-radius:1px;box-shadow:0 0 3px #c2c9ca66}.seaboard-faders input::-moz-range-track{width:1px;background:#737b7e}.seaboard-faders input::-moz-range-thumb{width:5px;height:2px;background:#c2c9ca;border:0;border-radius:1px}.seaboard-xy{position:absolute;left:3%;top:56%;width:7.3%;height:20%;border-radius:3px;cursor:crosshair;touch-action:none}.seaboard-xy i{position:absolute;width:4px;height:4px;border-radius:50%;transform:translate(-50%,50%);background:var(--c-primary);box-shadow:0 0 5px var(--c-primary)}.seaboard-bindings{position:absolute;left:2.8%;top:78%;font-size:9px;padding:0 4px;border:0;background:transparent;color:#949a9f}.seaboard-mappings{position:absolute;left:0;bottom:0;width:370px;max-width:100%;max-height:100%;overflow:auto;background:var(--panel-mid);border:1px solid var(--line);border-radius:5px;padding:14px;z-index:10;box-shadow:0 5px 30px #0009}.sustain-binding{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.pedal-help{font-size:10px;color:var(--text-dim);margin:8px 0}.global-binding{align-items:center;flex-wrap:wrap}.global-binding>span{font-size:10px;width:48px}
</style>
