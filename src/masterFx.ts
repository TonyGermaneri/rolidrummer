export const busTargets=['reverbMix','delayMix','chorusMix','cutoff','resonance'] as const
export type BusTarget=typeof busTargets[number]
export const busLabels={reverbMix:'REVERB',delayMix:'DELAY',chorusMix:'CHORUS',cutoff:'CUTOFF',resonance:'RES'}
export const surfaceLabels=['FADER 1','FADER 2','FADER 3','PAD X','PAD Y']
export interface MasterFx {
 reverbMix:number;reverbSize:number;reverbDecay:number;
 delayMix:number;delayTime:number;delayFeedback:number;delaySync:boolean;delayBeats:number;
 chorusMix:number;chorusRate:number;chorusDepth:number;cutoff:number;resonance:number;
 bindings:Record<BusTarget,number>
}
export const fxRanges={reverbMix:[0,1,0],reverbSize:[.1,8,3],reverbDecay:[.5,10,3],delayMix:[0,1,0],delayTime:[.01,2,.375],delayFeedback:[0,.9,.4],delayBeats:[.125,8,.75],chorusMix:[0,1,0],chorusRate:[.05,8,.6],chorusDepth:[0,1,.5],cutoff:[20,20000,20000],resonance:[.1,20,.7]} as const
export function normalizeFx(input?:Partial<MasterFx>):MasterFx {
 const fx={...input,delaySync:!!input?.delaySync,bindings:{}} as MasterFx
 for(const key of Object.keys(fxRanges) as (keyof typeof fxRanges)[]){const [min,max,value]=fxRanges[key],n=input?.[key];fx[key]=typeof n==='number'&&Number.isFinite(n)?Math.max(min,Math.min(max,n)):value}
 for(const key of busTargets){const source=input?.bindings?.[key];fx.bindings[key]=typeof source==='number'&&Number.isInteger(source)&&source>=0&&source<5?source:-1}
 return fx
}
export function busValue(target:BusTarget,position:number){const x=Math.max(0,Math.min(1,position));return target==='cutoff'?20*1000**x:target==='resonance'?.1+19.9*x:x}
