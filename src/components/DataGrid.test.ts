import {afterEach, describe, expect, it, vi} from 'vitest'
import {mount} from '../testing/componentHost'
const fake=vi.hoisted(()=>({grid:null as any}))
vi.mock('canvas-datagrid',()=>({default:(options:any)=>{
 const listeners:Record<string,((e:any)=>void)[]>={}
 const grid:any={...options,style:{},viewData:options.data,activeCell:{rowIndex:0,columnIndex:0},
  addEventListener:(name:string,fn:any)=>(listeners[name]??=[]).push(fn),
  fire:(name:string,details:any)=>{const e={preventDefault:vi.fn(),...details};for(const fn of listeners[name]??[])fn(e);return e},
  focus:vi.fn(),clearSelections:vi.fn(),draw:vi.fn(),dispose:vi.fn(),
  setActiveCell:(columnIndex:number,rowIndex:number)=>{grid.activeCell={columnIndex,rowIndex}},
  selectRow:vi.fn(()=>grid.fire('selectionchanged',{}))}
 fake.grid=grid;return grid
}}))
import DataGrid from './DataGrid.vue'
let cleanup:(()=>void)|undefined
afterEach(()=>{cleanup?.();vi.clearAllTimers();vi.useRealTimers()})
const rows=[{id:'a',name:'Kick'},{id:'b',name:'Snare'}]
const cell=(rowIndex=0,name='name')=>({isNormal:true,dragContext:'cell',rowIndex,columnIndex:1,header:{name},data:rows[rowIndex]})
const mouse=(x=10,y=10,button=0)=>({clientX:x,clientY:y,button})
async function render(){vi.useFakeTimers();const pick=vi.fn(),drag=vi.fn(),action=vi.fn(),use=vi.fn();const view=await mount(DataGrid,{rows,columns:[{name:'name'}],onPick:pick,onDrag:drag,onCell:action,onUse:use});cleanup=view.unmount;return{grid:fake.grid,pick,drag,action,use}}
describe('grid item gestures',()=>{
 it('cancels range selection while picking one item and forwarding its drag',async()=>{
  const {grid,pick,drag,action}=await render(),native=mouse()
  const down=grid.fire('mousedown',{cell:cell(),NativeEvent:native})
  expect(down.preventDefault).toHaveBeenCalledOnce()
  expect(grid.focus).toHaveBeenCalledOnce();expect(grid.selectRow).toHaveBeenCalledWith(0,false,false)
  expect(pick).toHaveBeenLastCalledWith(rows[0]);expect(drag).toHaveBeenCalledWith(rows[0],native)
  grid.fire('mousemove',{cell:cell(1),NativeEvent:mouse(10,45)})
  const click=grid.fire('click',{cell:cell(1),NativeEvent:mouse(10,45)})
  expect(click.preventDefault).toHaveBeenCalledOnce();expect(action).not.toHaveBeenCalled()
  expect(grid.selectRow).toHaveBeenCalledTimes(1)
 })
 it('preserves click actions, double-click use, and the grid chrome gestures',async()=>{
  const {grid,drag,action,use}=await render()
  for(const name of ['preview','load']){
   grid.fire('mousedown',{cell:cell(0,name),NativeEvent:mouse()})
   grid.fire('click',{cell:cell(0,name),NativeEvent:mouse()})
   expect(action).toHaveBeenLastCalledWith(name,rows[0])
  }
  expect(drag).not.toHaveBeenCalled()
  grid.fire('dblclick',{cell:cell(1)});expect(use).toHaveBeenCalledWith(rows[1])
  for(const target of [{isNormal:false,context:'vertical-scroll-box'},{isNormal:false,isColumnHeader:true},{...cell(),dragContext:'ew-resize'}]){
   expect(grid.fire('mousedown',{cell:target,NativeEvent:mouse()}).preventDefault).not.toHaveBeenCalled()
  }
  expect(grid.fire('mousedown',{cell:cell(),NativeEvent:mouse(10,10,2)}).preventDefault).not.toHaveBeenCalled()
 })
})
