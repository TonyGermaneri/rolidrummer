<script setup lang="ts">
import {computed,onMounted,onBeforeUnmount,ref,watch} from 'vue'
import {state,sendMidi,attempt} from '../store'
import {colors,targets,newRoute} from '../model'
import {htmlDrop} from '../drag'
import {request} from '../native/bridge'
import {fitSeaboard} from '../lib/seaboardLayout'
const props=defineProps<{base:number}>()
const stage=ref<HTMLElement|null>(null),surface=ref({width:0,height:0})
let observer:ResizeObserver|undefined
function fit(){if(stage.value)surface.value=fitSeaboard(stage.value.clientWidth,stage.value.clientHeight)}
onMounted(()=>{fit();if(typeof ResizeObserver!=='undefined'){observer=new ResizeObserver(fit);observer.observe(stage.value!)}else window.addEventListener('resize',fit)})
onBeforeUnmount(()=>{observer?.disconnect();window.removeEventListener('resize',fit)})
const blackPitch=new Set([1,3,6,8,10])
const left=150,top=173,width=572,height=106,whiteWidth=width/29
const keyboard=computed(()=>{let white=0;return Array.from({length:49},(_,i)=>{const note=props.base+i,black=blackPitch.has(i%12),x=left+(black?white-.31:white)*whiteWidth;const w=black?.62*whiteWidth:whiteWidth;const h=black?height*.57:height;if(!black)white++;return{note,black,x,w,h}})})
function outline(key:typeof keyboard.value[number]) {
 const x=key.x+1,w=key.w-2,y=top,h=key.h;
 if(key.black)return `M ${x+3} ${y-2} Q ${x+w/2} ${y-5} ${x+w-2} ${y-2} L ${x+w} ${y+h-7} Q ${x+w} ${y+h+3} ${x+w/2} ${y+h+5} Q ${x} ${y+h+3} ${x} ${y+h-7} Z`;
 const pitch=(key.note-props.base)%12;
 const insetL=blackPitch.has((pitch+11)%12)?whiteWidth*.29:1;
 const insetR=blackPitch.has((pitch+1)%12)?whiteWidth*.29:1;
 return `M ${x+insetL} ${y+3} Q ${x+w/2} ${y-1} ${x+w-insetR} ${y+3} L ${x+w-insetR} ${y+h*.51} Q ${x+w-insetR} ${y+h*.6} ${x+w-1} ${y+h*.64} L ${x+w} ${y+h-5} Q ${x+w/2} ${y+h+1} ${x} ${y+h-5} L ${x+1} ${y+h*.64} Q ${x+insetL} ${y+h*.6} ${x+insetL} ${y+h*.51} Z`;
}
const keys=computed(()=>new Map(state.patch.keys.map(k=>[k.note,k])))
const held=computed(()=>{const result=new Map<number,number>();for(const raw of state.telemetry.contacts)if(raw)result.set((raw&255)-1,raw>>8);return result})
const shortNames:Record<string,string>={'Closed hat':'CH','Open hat':'OH','Pedal hat':'PH','High tom':'HT','Mid tom':'MT','Low tom':'LT','High conga':'HC','Low conga':'LC','Cowbell':'Bell','Shaker':'Shkr'}
function keyLabel(name=''){return shortNames[name]??(name.length>5?name.slice(0,4)+'…':name)}
const fingerLocations=computed(()=>(state.telemetry.fingers??[]).flatMap(f=>{const key=keyboard.value.find(k=>k.note===f.note);return key?[{...f,x:key.x+key.w*(.5+(f.glide-.5)*.7),y:top+3+(1-f.slide)*(key.h-6),radius:2.5+Math.max(.1,f.pressure)*3}]:[]}))
const contacts=new Map<number,{note:number;channel:number;svg:SVGSVGElement;h:number}>()
const keyboardContacts=new Map<number,number>()
const macro=ref([...state.telemetry.macros]),globalOpen=ref(false)
const touching=new Set<number>()
watch(()=>state.telemetry.macros,values=>{values.forEach((value,i)=>{if(!touching.has(i))macro.value[i]=value})})
function zoneBottom(zones:{upper:number}[],i:number){return i?zones[i-1].upper:0}
function usedChannels(){const used=new Set([...contacts.values()].map(c=>c.channel).concat([...keyboardContacts.values()]));state.telemetry.contacts.forEach((value,ch)=>{if(value)used.add(ch)});return used}
function down(e:PointerEvent,key:typeof keyboard.value[number]){if(e.button!==0)return;state.selected=key.note;if(!keys.value.has(key.note))return;const used=usedChannels(),channel=Array.from({length:15},(_,i)=>i+1).find(c=>!used.has(c));if(channel===undefined)return;const target=e.currentTarget as SVGElement;target.setPointerCapture(e.pointerId);contacts.set(e.pointerId,{note:key.note,channel,svg:target.ownerSVGElement!,h:key.h});sendMidi([0xe0|channel,0,64]);sendMidi([0xd0|channel,100,0]);move(e);sendMidi([0x90|channel,key.note,100])}
function move(e:PointerEvent){const c=contacts.get(e.pointerId);if(!c)return;const p=c.svg.createSVGPoint();p.x=e.clientX;p.y=e.clientY;const actual=p.matrixTransform(c.svg.getScreenCTM()!.inverse());const value=Math.round(Math.max(0,Math.min(1,1-(actual.y-top)/c.h))*127);sendMidi([0xb0|c.channel,74,value]);if(e.pressure>0)sendMidi([0xd0|c.channel,Math.round(e.pressure*127),0])}
function up(e:PointerEvent){const c=contacts.get(e.pointerId);if(c){sendMidi([0x80|c.channel,c.note,64]);contacts.delete(e.pointerId)}}
function keyboardDown(e:KeyboardEvent,note:number){
 if(e.key!==' '&&e.key!=='Enter')return;e.preventDefault();state.selected=note
 if(e.repeat||keyboardContacts.has(note)||!keys.value.has(note))return
 const used=usedChannels(),channel=Array.from({length:15},(_,i)=>i+1).find(c=>!used.has(c))
 if(channel===undefined)return;keyboardContacts.set(note,channel);sendMidi([0xe0|channel,0,64]);sendMidi([0xd0|channel,100,0]);sendMidi([0xb0|channel,74,64]);sendMidi([0x90|channel,note,100])
}
function keyboardUp(note:number){const channel=keyboardContacts.get(note);if(channel!==undefined){sendMidi([0x80|channel,note,64]);keyboardContacts.delete(note)}}
function panic(){for(const contact of contacts.values())sendMidi([0x80|contact.channel,contact.note,64]);for(const [note,channel] of keyboardContacts)sendMidi([0x80|channel,note,64]);contacts.clear();keyboardContacts.clear();touching.clear()}
watch(()=>props.base,panic)
function route(i:number){return state.patch.globalRoutes.find(r=>r.source===i)?.target||''}
function map(i:number,e:Event){const target=(e.target as HTMLSelectElement).value;const routes=state.patch.globalRoutes.filter(r=>r.source!==i);if(target)routes.push(newRoute(i,target));if(state.patch.keys.some(k=>k.routes.length+routes.length>16)){state.error='A key already uses all 16 mappings. Remove a key mapping before adding this global controller.';(e.target as HTMLSelectElement).value=route(i);return}state.patch.globalRoutes=routes}
function cc(i:number,v:number){macro.value[i]=v;state.telemetry.macros[i]=v;void attempt(()=>request('parameter',{id:`macro${i}`,value:v}))}
function padKey(e:KeyboardEvent){if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(e.key))return;e.preventDefault();const step=e.shiftKey?.1:.01;if(e.key==='ArrowLeft')cc(3,Math.max(0,macro.value[3]-step));else if(e.key==='ArrowRight')cc(3,Math.min(1,macro.value[3]+step));else if(e.key==='ArrowUp')cc(4,Math.min(1,macro.value[4]+step));else if(e.key==='ArrowDown')cc(4,Math.max(0,macro.value[4]-step))}
function xy(e:PointerEvent){touching.add(3);touching.add(4);const el=e.currentTarget as HTMLElement;el.setPointerCapture(e.pointerId);const rect=el.getBoundingClientRect();const move=(ev:PointerEvent)=>{cc(3,Math.max(0,Math.min(1,(ev.clientX-rect.left)/rect.width)));cc(4,Math.max(0,Math.min(1,1-(ev.clientY-rect.top)/rect.height)))};move(e);const up=()=>{touching.delete(3);touching.delete(4);el.removeEventListener('pointermove',move);el.removeEventListener('pointerup',up);el.removeEventListener('pointercancel',up)};el.addEventListener('pointermove',move);el.addEventListener('pointerup',up);el.addEventListener('pointercancel',up)}
window.addEventListener('blur',panic);onBeforeUnmount(()=>{window.removeEventListener('blur',panic);panic()})
</script>
<template><div ref="stage" class="seaboard-stage"><div class="seaboard" :style="{width:`${surface.width}px`,height:`${surface.height}px`}"><svg class="seaboard-svg" viewBox="49 115 705 210" role="group" aria-label="Seaboard RISE 49 keywaves"><defs>
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
 <path d="M 67 154 C 78 150 74 161 69 158 M 70 157 L 76 163" fill="none" stroke="#969c9e" stroke-width="1.2"/>
 <rect x="89" y="157" width="33" height="11" rx="4" fill="#1d2123" stroke="#14171a"/>
 <path d="M 98 160 L 96 162 L 98 164 M 114 160 L 116 162 L 114 164" stroke="#777f83" stroke-width=".6" fill="none"/>
 <g v-for="x in [70,90,110]" :key="x"><rect :x="x" y="175" width="12" height="52" rx="3" fill="#171b1d" stroke="#101315"/><path :d="`M ${x+5} 181 V 223`" stroke="#656e72" stroke-width=".5"/><circle :cx="x+6" cy="179" r=".8" fill="#a8afb3"/></g>
 <rect x="70" y="234" width="52" height="41" rx="3" fill="#1b1f22" stroke="#14181a"/><path d="M 96 240 v 2 M 96 267 v 2 M 77 254 h 2 M 113 254 h 2" stroke="#7d878d" stroke-width=".8"/>
 <circle cx="76" cy="288" r="6" fill="#171b1d" stroke="#101315"/><circle cx="76" cy="288" r="1.2" fill="#7c9194"/>
 <rect x="89" y="282" width="33" height="12" rx="4" fill="#191d1f" stroke="#111517"/>
 <path d="M 98 286 L 96 288 L 98 290 M 114 286 L 116 288 L 114 290" stroke="#747e84" stroke-width=".6" fill="none"/>
</g>
<defs><clipPath v-for="key in keyboard" :id="`keyclip-${key.note}`" :key="key.note"><path :d="outline(key)"/></clipPath></defs>
<g v-for="black in [false,true]" :key="String(black)"><g v-for="key in keyboard.filter(k=>k.black===black)" :key="key.note" class="seaboard-key" :class="{selected:state.selected===key.note,held:held.has(key.note),black}" :data-drop="`key:${key.note}`" :aria-label="keys.get(key.note)?.name||`Note ${key.note}`" tabindex="0" role="button" @focus="state.selected=key.note" @keydown="keyboardDown($event,key.note)" @keyup="($event.key===' '||$event.key==='Enter')&&keyboardUp(key.note)" @blur="keyboardUp(key.note)" @pointerdown="down($event,key)" @pointermove="move" @pointerup="up" @pointercancel="up" @dragover.prevent @drop="htmlDrop($event,`key:${key.note}`)">
<path class="key-hit" :d="outline(key)" :fill="key.black?'url(#rd-sharp)':'url(#rd-natural)'"/>
<path :d="`M ${key.x+key.w*.47} ${top+5} V ${top+key.h*(key.black?.82:.48)}`" stroke="url(#rd-ridge)" :stroke-width="key.black?.8:1" fill="none" pointer-events="none"/>
<path :d="`M ${key.x+key.w*.54} ${top+5} V ${top+key.h*(key.black?.82:.48)}`" stroke="#000" stroke-opacity=".3" stroke-width=".7" fill="none" pointer-events="none"/>
<g :clip-path="`url(#keyclip-${key.note})`" pointer-events="none"><rect v-for="(zone,i) in keys.get(key.note)?.zones||state.patch.templateZones" :key="i" :x="key.x+1" :y="top+(1-zone.upper)*key.h" :width="key.w-2" :height="(zone.upper-zoneBottom(keys.get(key.note)?.zones||state.patch.templateZones,i))*key.h" :fill="colors[(keys.get(key.note)?.zones||state.patch.templateZones).length-1-i]" :opacity="held.get(key.note)===i?.65:keys.has(key.note)?.25:.045"/></g>
<path v-if="state.selected===key.note" class="key-selected" :d="outline(key)" pointer-events="none"/><path class="key-outline" :d="outline(key)" pointer-events="none"/>
</g></g>
<g class="finger-indicators" pointer-events="none" aria-label="Live finger positions"><g v-for="finger in fingerLocations" :key="finger.channel" :data-finger="finger.channel" :transform="`translate(${finger.x} ${finger.y})`"><circle :r="finger.radius+2" class="finger-glow"/><circle :r="finger.radius" class="finger-dot"/><circle r=".85" fill="white"/></g></g>
<text v-for="key in keyboard.filter(k=>keys.has(k.note))" :key="key.note" class="seaboard-key-label" :x="key.x+key.w/2" :y="key.black?121:134" text-anchor="middle"><title>{{keys.get(key.note)?.name}}</title>{{keyLabel(keys.get(key.note)?.name)}}</text>
</svg>
<div class="seaboard-faders"><input v-for="i in 3" :key="i" :value="macro[i-1]" type="range" min="0" max="1" step=".001" :aria-label="`Global controller ${i}`" @pointerdown="touching.add(i-1)" @pointerup="touching.delete(i-1)" @pointercancel="touching.delete(i-1)" @keydown="touching.add(i-1)" @keyup="touching.delete(i-1)" @blur="touching.delete(i-1)" @input="cc(i-1,Number(($event.target as HTMLInputElement).value))"/></div>
<div class="seaboard-xy" role="group" tabindex="0" aria-label="Global XY pad. Use arrow keys to adjust." @keydown="padKey" @pointerdown="xy"><i :style="{left:`${macro[3]*100}%`,bottom:`${macro[4]*100}%`}"/></div>
<button class="seaboard-bindings" aria-label="Global MIDI bindings" @click="globalOpen=!globalOpen">⚙</button>
<div v-if="globalOpen" class="seaboard-mappings"><div class="section-label">GLOBAL CONTROLLERS<button @click="globalOpen=false">×</button></div><div v-for="(_,i) in macro" :key="i" class="global-binding"><span>{{['Fader 1','Fader 2','Fader 3','Pad X','Pad Y'][i]}}</span><button @click="state.learn=state.learn===i?-1:i">{{state.learn===i?'Move control…':`CC ${state.patch.globalCC[i]}`}}</button><select :value="route(i)" @change="map(i,$event)"><option value="">Unbound</option><option v-for="t in targets" :key="t[0]" :value="t[0]">{{t[0]}}</option></select></div></div>
</div></div></template>
<style scoped>
.seaboard-stage{flex:1 1 0;align-self:stretch;min-width:0;min-height:0;position:relative;display:flex;align-items:center;justify-content:center}.seaboard{flex:none;position:relative;min-width:0;border:0;background:transparent;display:block}.seaboard-svg{width:100%;height:100%;display:block;overflow:hidden}.seaboard-key{cursor:pointer;touch-action:none;outline:none}.key-hit{stroke:#070a0c;stroke-opacity:.45;stroke-width:.4}.key-outline{fill:none;stroke:transparent;stroke-width:.9}.seaboard-key:hover .key-outline{stroke:#a5abb3;stroke-opacity:.5}.key-selected{fill:var(--c-primary,#d4a84b);opacity:.3;stroke:none}.seaboard-key.held .key-selected{opacity:.45}.finger-glow{fill:var(--c-secondary,#8fb3d6);opacity:.2}.finger-dot{fill:var(--c-secondary,#8fb3d6);stroke:white;stroke-opacity:.75;stroke-width:.6;filter:drop-shadow(0 0 2px var(--c-secondary,#8fb3d6))}.seaboard-key-label{fill:var(--text-dim,#b7b3aa);font:6px system-ui;pointer-events:none}.seaboard-faders{position:absolute;left:3%;top:29%;width:7.3%;height:25%;display:flex;justify-content:space-around;gap:5%}.seaboard-faders input{writing-mode:vertical-lr;direction:rtl;width:20%;height:100%;appearance:none;-webkit-appearance:none;background:transparent;cursor:ns-resize}.seaboard-faders input::-webkit-slider-runnable-track{width:1px;background:#737b7e}.seaboard-faders input::-webkit-slider-thumb{appearance:none;-webkit-appearance:none;width:5px;height:2px;background:#c2c9ca;border:0;border-radius:1px;box-shadow:0 0 3px #c2c9ca66}.seaboard-faders input::-moz-range-track{width:1px;background:#737b7e}.seaboard-faders input::-moz-range-thumb{width:5px;height:2px;background:#c2c9ca;border:0;border-radius:1px}.seaboard-xy{position:absolute;left:3%;top:56%;width:7.3%;height:20%;border-radius:3px;cursor:crosshair;touch-action:none}.seaboard-xy i{position:absolute;width:4px;height:4px;border-radius:50%;transform:translate(-50%,50%);background:var(--c-primary);box-shadow:0 0 5px var(--c-primary)}.seaboard-bindings{position:absolute;left:2.8%;top:78%;font-size:9px;padding:0 4px;border:0;background:transparent;color:#949a9f}.seaboard-mappings{position:absolute;left:0;bottom:80%;width:370px;background:var(--panel-mid);border:1px solid var(--line);border-radius:5px;padding:14px;z-index:10;box-shadow:0 5px 30px #0009}.global-binding{align-items:center}.global-binding>span{font-size:10px;width:48px}
</style>
