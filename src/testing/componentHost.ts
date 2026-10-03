import { createRenderer, h, nextTick, type Component } from 'vue'

// A Vue renderer for component contract tests. No browser or native UI is
// controlled: events exercise the real component handlers against in-memory nodes.
export class TestNode {
  parent: TestNode | null = null
  children: TestNode[] = []
  props: Record<string, any> = {}
  text = ''
  value: any = ''
  selected = false
  open = false
  listeners = new Map<string, Set<(event: any) => void>>()
  bounds = { top: 0, left: 0, width: 400, height: 200 }
  constructor(public tag: string) {}
  get tagName() { return this.tag.toUpperCase() }
  get parentElement() { return this.parent }
  get options(): TestNode[] { return this.children.filter(c => c.tag === 'option') }
  get multiple() { return Boolean(this.props.multiple) }
  get clientWidth() { return this.bounds.width }
  get clientHeight() { return this.bounds.height }
  get textContent(): string { return this.text + this.children.map(c => c.textContent).join('') }
  set textContent(value: string) { this.text = value; this.children = [] }
  getBoundingClientRect() { return this.bounds }
  getRootNode() { return { activeElement: null } }
  setPointerCapture(_id: number) {}
  showModal() { this.open = true }
  close() { this.open = false }
  addEventListener(name: string, fn: (event: any) => void) {
    if (!this.listeners.has(name)) this.listeners.set(name, new Set())
    this.listeners.get(name)!.add(fn)
  }
  removeEventListener(name: string, fn: (event: any) => void) { this.listeners.get(name)?.delete(fn) }
  setAttribute(name: string, value: unknown) { this.props[name] = value }
  removeAttribute(name: string) { delete this.props[name] }
  dispatchEvent(event: any) { this.listeners.get(event.type)?.forEach(fn => fn(event)); return true }
}

const renderer = createRenderer<TestNode, TestNode>({
  createElement: tag => new TestNode(tag),
  createText: text => Object.assign(new TestNode('#text'), { text }),
  createComment: () => new TestNode('#comment'),
  insertStaticContent(content, parent, anchor) {
    // Static artwork contains no component event handlers. Keep it opaque;
    // SVG geometry is separately rendered by export-seaboard.mjs.
    const node = Object.assign(new TestNode('#static'), { text: content, parent })
    const index = anchor ? parent.children.indexOf(anchor) : -1
    if (index < 0) parent.children.push(node); else parent.children.splice(index, 0, node)
    return [node, node]
  },
  setText: (node, text) => { node.text = text },
  setElementText: (node, text) => { node.textContent = text },
  parentNode: node => node.parent,
  nextSibling: node => node.parent?.children[node.parent.children.indexOf(node) + 1] ?? null,
  patchProp(node, key, _old, value) { node.props[key] = value; if (key === 'value') node.value = value },
  insert(node, parent, anchor = null) {
    if (node.parent) node.parent.children.splice(node.parent.children.indexOf(node), 1)
    node.parent = parent
    const index = anchor ? parent.children.indexOf(anchor) : -1
    if (index < 0) parent.children.push(node); else parent.children.splice(index, 0, node)
  },
  remove(node) { if (node.parent) node.parent.children.splice(node.parent.children.indexOf(node), 1); node.parent = null },
})

export function nodes(root: TestNode, predicate: (node: TestNode) => boolean): TestNode[] {
  return [...(predicate(root) ? [root] : []), ...root.children.flatMap(n => nodes(n, predicate))]
}
export function find(root: TestNode, predicate: (node: TestNode) => boolean) {
  const result = nodes(root, predicate)[0]
  if (!result) throw Error('Component element not found')
  return result
}
export async function settle() { for (let i = 0; i < 8; i++) { await Promise.resolve(); await nextTick() } }
export async function event(node: TestNode, name: string, detail: Record<string, unknown> = {}) {
  const e = { target: node, currentTarget: node, preventDefault() {}, stopPropagation() {}, ...detail }
  const handler = node.props[`on${name[0].toUpperCase()}${name.slice(1)}`]
  if (Array.isArray(handler)) for (const fn of handler) fn(e); else handler?.(e)
  node.listeners.get(name)?.forEach(fn => fn(e))
  await settle()
}
export async function mount(component: Component, props: Record<string, unknown> = {}) {
  const root = new TestNode('root')
  renderer.render(h(component, props), root)
  await settle()
  return { root, update: async (next: Record<string, unknown>) => { renderer.render(h(component, next), root); await settle() }, unmount: () => renderer.render(null, root) }
}
