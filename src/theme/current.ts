import { computed, ref } from 'vue'
import { themeById, themeVars, type Theme } from './themes'
export const themeId=ref('noir49')
export const themePreview=ref<string|null>(null)
export const currentTheme=computed(()=>themeById(themePreview.value??themeId.value))
function blend(a:string,b:string,t:number){return '#'+[1,3,5].map(i=>Math.round(parseInt(a.slice(i,i+2),16)*(1-t)+parseInt(b.slice(i,i+2),16)*t).toString(16).padStart(2,'0')).join('')}
export function visualVars(t:Theme){
 const vars=themeVars(t)
 Object.assign(vars,{'--primary':t.primary,'--secondary':t.secondary,'--accent':t.accent,'--ink':t.text[0],'--line':blend(t.text[3],t.panel[1],.6),'--on-primary':t.lcd})
 // A related amber-to-steel (or selected-theme) palette, not traffic-light zones.
 for(let i=0;i<8;i++)vars[`--zone-${i}`]=blend(t.primary,t.secondary,.15+i*.11)
 return vars
}
