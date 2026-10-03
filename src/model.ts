import {defaultSettings,type SoundSettings} from './types'
import factoryData from './data/factoryKit.json'
export interface Zone{upper:number;clipId:string}
export interface Route{source:number;target:string;min:number;max:number;curve:number;smoothing:number}
export interface Sound{path:string;asset?:string;missing?:string;s:SoundSettings & {reverse:boolean}}
export interface Key{note:number;name:string;parts:number[];sound:Sound;zones:Zone[];zoneSource:number;routes:Route[];mpeDefaultsVersion?:number}
export interface Clip{id:string;name:string;path?:string;channel?:number;track?:number;length:number;notes:{beat:number;duration:number;note:number;velocity:number}[]}
export interface Patch{version:number;name:string;keys:Key[];clips:Clip[];templateZones:Zone[];launchGrid:number;switchGrid:number;hardRelease:boolean;volume:number;bpm:number;globalCC:number[];globalRoutes:Route[]}
export interface Asset{id:string;path:string;name:string;folder:boolean;size:number;modified?:string;preset?:{sound:Sound;routes:Route[];parts:number[]}}
export const colors=Array.from({length:8},(_,i)=>`var(--zone-${i})`)
export const clone=<T>(v:T):T=>JSON.parse(JSON.stringify(v))
export function defaultMpeRoutes():Route[]{return [
 {...newRoute(0,'volume'),min:.45,max:1},
 {...newRoute(1,'pitch'),min:-2,max:2},
 {...newRoute(2,'cutoff'),min:900,max:18000,curve:.55},
 {...newRoute(3,'reverbMix'),min:.02,max:.3},
 {...newRoute(4,'release'),min:.06,max:1.2},
]}
export const drumParts=[{name:'Closed hat',notes:[42]},{name:'Open hat',notes:[46]},{name:'Pedal hat',notes:[44]},{name:'Rim',notes:[37]},{name:'Cowbell',notes:[56]},{name:'Shaker',notes:[70]},{name:'High conga',notes:[62,63]},{name:'Low conga',notes:[64]},{name:'Kick',notes:[35,36]},{name:'Hi-hat',notes:[42,44,46]},{name:'Snare',notes:[38,40]},{name:'High tom',notes:[48,50]},{name:'Mid tom',notes:[45,47]},{name:'Low tom',notes:[41,43]},{name:'Clap',notes:[39]},{name:'Ride',notes:[51,53,59]},{name:'Crash',notes:[49,52,55,57]}]
export function partName(notes:number[]){return drumParts.find(p=>notes.length>0&&notes.every(n=>p.notes.includes(n)))?.name??'Custom'}
export function legacyGrooveNotes(p:number,legacy=false):Clip['notes']{const notes:Clip['notes']=[];for(let i=0;i<16;i++){
 if(legacy){if(i%4===0)notes.push({beat:i/4,duration:.1,note:36,velocity:.9});if(i%4===2)notes.push({beat:i/4,duration:.1,note:38,velocity:.8});if(i%[4,2,1][p]===0)notes.push({beat:i/4,duration:.07,note:42,velocity:i%2===0?.7:.4})}
 else if(i%[4,2,1][p]===0)for(const note of [36,42,38,50,47,43])notes.push({beat:i/4,duration:note===42?.07:.1,note,velocity:i%4===0?.85:i%2===0?.65:.45})
}return notes}
export function grooveNotes(p:number):Clip['notes']{return clone(factoryData.clips[p].notes)}
export function normalize(p:Patch):Patch{
 for(const k of p.keys){if(k.zoneSource===4)k.zoneSource=2;k.sound.s={...defaultSettings(k.name),...k.sound.s,reverse:k.sound.s.reverse??false};k.routes??=[];if(k.mpeDefaultsVersion!==1){if(!k.routes.length)k.routes=defaultMpeRoutes();k.mpeDefaultsVersion=1}}
 for(let i=0;i<3;i++){const clip=p.clips.find(c=>c.id===`factory-${i}`);if(clip&&clip.length===4&&[false,true].some(version=>{const old=legacyGrooveNotes(i,version);return clip.notes.length===old.length&&clip.notes.every((note,j)=>(['beat','duration','note','velocity'] as const).every(field=>Math.abs(note[field]-old[j][field])<1e-7))})){clip.notes=grooveNotes(i);clip.length=8;clip.name=factoryData.clips[i].name}}

 p.globalRoutes??=[];return p
}
export function factory():Patch{
 const zones=[{upper:.22,clipId:'factory-0'},{upper:.76,clipId:'factory-1'},{upper:1,clipId:'factory-2'}]
 const keys:Key[]=factoryData.voices.map(v=>({note:v.note,name:v.name,parts:clone(v.parts),sound:{path:v.source,s:{...defaultSettings(v.name),...v.settings} as Sound['s']},zones:clone(zones),zoneSource:2,routes:clone(v.routes),mpeDefaultsVersion:1}))
 return normalize({version:1,name:factoryData.name,keys,clips:clone(factoryData.clips),templateZones:zones,launchGrid:.25,switchGrid:.25,hardRelease:false,volume:.8,bpm:120,globalCC:[107,109,111,113,114],globalRoutes:[]})
}
export function factoryAssets():Asset[]{return factory().keys.map(key=>({id:`factory-kit:${key.note}`,path:key.sound.path,name:key.name,folder:false,size:0,preset:{sound:clone(key.sound),routes:clone(key.routes),parts:clone(key.parts)}}))}
export const sources=['Strike','Glide / pitch','Slide / CC74','Press / pressure','Lift / release']
export const targets=[['volume',0,1.5],['pan',-1,1],['pitch',-24,24],['fine',-100,100],['speed',.25,4],['cutoff',20,20000],['resonance',.1,20],['reverse',0,1],['repeat',0,16],['choke',0,8],['clipIn',0,1],['clipOut',0,1],['rootNote',0,127],['velAmount',0,1],['bendRange',0,48],['attack',.001,5],['decay',.001,5],['sustain',0,1],['release',.001,10],['fEnvAmount',-5,5],['fAttack',.001,5],['fDecay',.001,5],['fSustain',0,1],['fRelease',.001,10],['grainSize',.02,10000],['grainPos',0,1],['grainWidth',0,1],['grainRate',.1,200],['grainShape',0,1],['grainReverse',0,1],['grainSpread',0,1],['grainScatter',0,1],['grainDrift',0,1],['grainJitter',0,12],['grainStreams',1,8],['delayTime',.01,2],['delayFeedback',0,.9],['delayMix',0,1],['reverbSize',.1,6],['reverbDecay',.5,10],['reverbMix',0,1],['eqLow',-12,12],['eqMid',-12,12],['eqHigh',-12,12]] as const
export function newRoute(source=3,target='volume'):Route{const t=targets.find(t=>t[0]===target)||targets[0];return{source,target,min:t[1],max:t[2],curve:1,smoothing:8}}
