import { describe, expect, it } from 'vitest'
import { SharedView, type Versioned } from './sharedView'

describe('shared browser views', () => {
  it('rejects a stale gesture even after polling a newer revision', async () => {
    let field = { value: { x: 0 }, revision: 1 }
    let shown = field.value
    const expected: number[] = []
    const view = new SharedView(async (value: { x: number }, revision) => {
      expected.push(revision)
      if (revision !== field.revision) return { ok: false, field }
      field = { value, revision: revision + 1 }
      return { ok: true, field }
    }, value => { shown = value })
    view.receive(field)
    view.begin()
    field = { value: { x: 20 }, revision: 2 }
    view.receive(field)
    expect(shown.x).toBe(0)
    await view.finish({ x: 10 })
    expect(expected).toEqual([1])
    expect(field.value.x).toBe(20)
    expect(shown.x).toBe(20)
  })

  it('serializes edits and rebases only on its own accepted write', async () => {
    let complete!: (r: { ok: boolean; field: Versioned<number> }) => void
    const revisions: number[] = []
    const view = new SharedView<number>((value, revision) => {
      revisions.push(revision)
      if (value === 1) return new Promise(resolve => { complete = resolve })
      return Promise.resolve({ ok: true, field: { value, revision: 3 } })
    }, () => {})
    view.receive({ value: 0, revision: 1 })
    view.begin()
    const saved = view.finish(1)
    view.begin()
    void view.finish(2)
    complete({ ok: true, field: { value: 1, revision: 2 } })
    await saved
    expect(revisions).toEqual([1, 2])
  })

  it('does not write initial/remote restoration or unload snapshots', async () => {
    let writes = 0
    const view = new SharedView<number>(async value => {
      writes++
      return { ok: true, field: { value, revision: writes } }
    }, () => {})
    view.receive({ value: 9, revision: 3 })
    await view.finish(9)
    view.begin()
    view.dispose()
    await view.finish(10)
    expect(writes).toBe(0)
  })
})
