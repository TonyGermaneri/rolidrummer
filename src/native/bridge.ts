interface Backend { addEventListener(name:string,fn:(value:any)=>void):unknown;emitEvent(name:string,value:unknown):void }
declare global {interface Window {__JUCE__?:{backend:Backend;initialisationData:Record<string,unknown[]>}}}
const backend=()=>window.__JUCE__?.backend
export const native=()=>Boolean(backend())
const pending=new Map<number,(value:any)=>void>();let next=0,listening=false
export function onNative(name:string,fn:(value:any)=>void){backend()?.addEventListener(name,fn)}
export async function request<T=any>(method:string,payload:unknown=null):Promise<T>{
 const b=backend();if(!b)throw new Error('Use the AU, VST3 or standalone app to access local libraries and play audio.')
 if(!listening){listening=true;b.addEventListener('__juce__complete',({promiseId,result})=>{pending.get(promiseId)?.(result);pending.delete(promiseId)})}
 const resultId=next++
 const result=await new Promise<any>(resolve=>{pending.set(resultId,resolve);b.emitEvent('__juce__invoke',{name:'rdRequest',params:[method,payload],resultId})})
 if(!result.ok)throw new Error(result.error);return result.result
}
