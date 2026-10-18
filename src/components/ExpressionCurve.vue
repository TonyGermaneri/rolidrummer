<script setup lang="ts">
import {onMounted,ref,watch} from 'vue'
import {currentTheme} from '../theme/current'
const model=defineModel<number>({required:true})
const props=withDefaults(defineProps<{label?:string}>(),{label:'Expression curve'})
const canvas=ref<HTMLCanvasElement|null>(null)
let dragging=false
function set(value:number){model.value=Math.max(.1,Math.min(10,value))}
function paint(){
 const el=canvas.value,ctx=el?.getContext?.('2d');if(!el||!ctx)return
 const ratio=window.devicePixelRatio||1;el.width=80*ratio;el.height=46*ratio;ctx.scale(ratio,ratio)
 const css=getComputedStyle(el),color=css.getPropertyValue('--c-secondary').trim()||'#8fb3d6'
 ctx.clearRect(0,0,80,46);ctx.strokeStyle=css.getPropertyValue('--text-mute').trim()||'#677078';ctx.globalAlpha=.3;ctx.lineWidth=.5
 for(const t of [0,.5,1]){ctx.beginPath();ctx.moveTo(5+t*70,4);ctx.lineTo(5+t*70,42);ctx.moveTo(5,42-t*38);ctx.lineTo(75,42-t*38);ctx.stroke()}
 ctx.beginPath();ctx.moveTo(5,42);ctx.lineTo(75,4);ctx.stroke();ctx.globalAlpha=1;ctx.strokeStyle=color;ctx.lineWidth=1.5;ctx.beginPath()
 for(let i=0;i<=70;i++){const x=5+i,y=42-Math.pow(i/70,model.value)*38;if(i)ctx.lineTo(x,y);else ctx.moveTo(x,y)}ctx.stroke()
 ctx.fillStyle=color;ctx.beginPath();ctx.arc(40,42-Math.pow(.5,model.value)*38,2.5,0,Math.PI*2);ctx.fill()
}
function move(e:PointerEvent){if(!dragging)return;const box=(e.currentTarget as HTMLElement).getBoundingClientRect();const x=Math.max(.03,Math.min(.97,(e.clientX-box.left-5)/70)),y=Math.max(.001,Math.min(.999,1-(e.clientY-box.top-4)/38));set(Math.log(y)/Math.log(x))}
function down(e:PointerEvent){if(e.button!==0)return;e.preventDefault();(e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);dragging=true;move(e)}
function key(e:KeyboardEvent){if(e.key==='Home')set(.1);else if(e.key==='End')set(10);else if(['ArrowUp','ArrowRight','ArrowDown','ArrowLeft'].includes(e.key))set(model.value*Math.pow(e.shiftKey?1.01:1.1,['ArrowUp','ArrowRight'].includes(e.key)?1:-1));else return;e.preventDefault();e.stopPropagation()}
onMounted(paint);watch([model,currentTheme],paint,{flush:'post'})
</script>
<template><div class="expression-curve"><canvas ref="canvas" role="slider" tabindex="0" :aria-label="props.label" aria-valuemin="0.1" aria-valuemax="10" :aria-valuenow="model" :aria-valuetext="`Exponent ${model.toFixed(2)}`" title="Drag the curve to shape the response. Double-click for linear." @pointerdown.stop="down" @pointermove="move" @pointerup="dragging=false" @pointercancel="dragging=false" @lostpointercapture="dragging=false" @dblclick.stop="set(1)" @keydown="key"/><span>{{model.toFixed(2)}} <small>CURVE</small></span></div></template>
<style scoped>
.expression-curve{display:flex;flex-direction:column;align-items:center;width:80px;flex:none}.expression-curve canvas{width:80px;height:46px;background:var(--lcd-bg);border-radius:3px;box-shadow:inset 0 1px 4px #0009;cursor:crosshair;touch-action:none}.expression-curve canvas:focus-visible{outline:1px solid var(--c-secondary)}.expression-curve span{font:11px monospace;color:var(--c-secondary);line-height:16px}.expression-curve small{font:7px sans-serif;letter-spacing:1px;color:var(--text-dim)}
</style>
