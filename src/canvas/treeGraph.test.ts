import {afterEach,expect,it,vi} from 'vitest'
import {TreeGraph} from './treeGraph.js'
afterEach(()=>vi.unstubAllGlobals())
it('waits for a nonzero viewport before fitting and preserves its center through parent resizes',()=>{
 vi.stubGlobal('window',{devicePixelRatio:2})
 const bounds={width:0,height:0},graph=Object.assign(Object.create(TreeGraph.prototype),{
  box:{getBoundingClientRect:()=>bounds},canvas:{},width:1,height:1,hasViewport:false,cameraRestored:false,
  source:{},drawn:[{x:0,y:0}],fitView:vi.fn(),shiftViewport:vi.fn(),paintSoon:vi.fn()
 })
 graph.resize();expect(graph.fitView).not.toHaveBeenCalled();expect(graph.canvas.width).toBeUndefined()
 Object.assign(bounds,{width:280,height:500});graph.resize()
 expect(graph.fitView).toHaveBeenCalledOnce();expect(graph.canvas).toEqual({width:560,height:1000})
 Object.assign(bounds,{width:900,height:260});graph.resize()
 expect(graph.fitView).toHaveBeenCalledOnce();expect(graph.shiftViewport).toHaveBeenCalledWith(310,-120)
 expect(graph.adjustingView).toBe(false)
})
it('recovers a remembered camera from a collapsed/offscreen layout without moving a visible saved view',()=>{
 vi.stubGlobal('window',{devicePixelRatio:1})
 const graph=Object.assign(Object.create(TreeGraph.prototype),{
  box:{getBoundingClientRect:()=>({width:300,height:400})},canvas:{},width:1,height:1,hasViewport:false,cameraRestored:true,
  source:{},drawn:[{}],screenOf:()=>[-1000,-1000],fitView:vi.fn(),paintSoon:vi.fn()
 })
 graph.resize();expect(graph.fitView).toHaveBeenCalledOnce()
 graph.hasViewport=false;graph.screenOf=()=>[100,200];graph.fitView.mockClear();graph.resize()
 expect(graph.fitView).not.toHaveBeenCalled()
})
