// @vitest-environment ./src/testing/environment.ts
import {afterEach,describe,expect,it,vi} from 'vitest'
import {mount,event,find,nodes} from '../testing/componentHost'
import PatchSelector from './PatchSelector.vue'
import ThemePicker from './ThemePicker.vue'
import {themeId,themePreview,currentTheme} from '../theme/current'
import {THEMES} from '../theme/themes'
const cleanups:(()=>void)[]=[]
afterEach(()=>{cleanups.splice(0).forEach(fn=>fn());themePreview.value=null;themeId.value='noir49'})
describe('compact header',()=>{
 it('navigates duplicate or renamed display names by stable identity and reports unassigned MIDI programs',async()=>{
  const select=vi.fn(),view=await mount(PatchSelector,{name:'Renamed',currentId:'second',choices:[{id:'first',name:'Same name'},{id:'second',name:'Same name'}],program:127,unassigned:true,onSelect:select});cleanups.push(view.unmount)
  await event(find(view.root,n=>n.props['aria-label']==='Next patch'),'click')
  expect(select).toHaveBeenLastCalledWith('first')
  expect(find(view.root,n=>n.tag==='output').props.title).toContain('127 (unassigned)')
 })
 it('keeps patch names read-only and wraps previous/next selection',async()=>{
  const select=vi.fn(),choices=[{id:'a',name:'Alpha'},{id:'b',name:'Beta'}]
  const props={name:'Alpha',choices,onSelect:select},view=await mount(PatchSelector,props);cleanups.push(view.unmount)
  expect(nodes(view.root,n=>n.tag==='input')).toHaveLength(0)
  await event(find(view.root,n=>n.props['aria-label']==='Previous patch'),'click')
  expect(select).toHaveBeenLastCalledWith('b')
  await view.update({...props,name:'Beta'})
  await event(find(view.root,n=>n.props['aria-label']==='Next patch'),'click')
  expect(select).toHaveBeenLastCalledWith('a')
  await view.update({...props,name:'Unsaved'})
  await event(find(view.root,n=>n.props['aria-label']==='Next patch'),'click')
  expect(select).toHaveBeenLastCalledWith('a')
  await view.update({...props,busy:true})
  select.mockClear();await event(find(view.root,n=>n.props['aria-label']==='Next patch'),'click')
  expect(select).not.toHaveBeenCalled()
 })
 it('previews themes without changing the committed theme and restores on close',async()=>{
  const select=vi.fn(),props={modelValue:true,onSelect:select},view=await mount(ThemePicker,props);cleanups.push(view.unmount)
  const tile=find(view.root,n=>n.props['data-theme']==='diner55')
  await event(tile,'mouseenter')
  expect(currentTheme.value.id).toBe('diner55');expect(themeId.value).toBe('noir49');expect(select).not.toHaveBeenCalled()
  await event(find(view.root,n=>n.props.class==='body'),'mouseleave')
  expect(currentTheme.value.id).toBe('noir49')
  await event(tile,'focus');await view.update({...props,modelValue:false})
  expect(themePreview.value).toBeNull()
  await event(tile,'click');expect(select).toHaveBeenLastCalledWith('diner55')
 })
 it('wraps theme arrows and drops previews when destroyed',async()=>{
  themeId.value=THEMES[0].id
  const select=vi.fn(),view=await mount(ThemePicker,{modelValue:true,onSelect:select})
  await event(find(view.root,n=>n.props['aria-label']==='Previous theme'),'click')
  expect(select).toHaveBeenLastCalledWith(THEMES.at(-1)!.id)
  await event(find(view.root,n=>n.props['data-theme']==='diner55'),'focus')
  view.unmount();expect(themePreview.value).toBeNull()
 })
})
