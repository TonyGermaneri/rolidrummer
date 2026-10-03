import { beforeEach, describe, expect, it, vi } from 'vitest'
import { factory, factoryAssets, clone, type Patch, type Asset, type Clip } from './model'
const fake = vi.hoisted(() => ({ state: { patch: undefined as unknown as Patch, selected: 48 }, importPattern: vi.fn() }))
vi.mock('./store', () => ({ state: fake.state, attempt: (job: () => Promise<unknown>) => job() }))
vi.mock('./midiImport', () => ({ importPattern: fake.importPattern }))
import { drop } from './drag'
beforeEach(() => { fake.state.patch = factory(); fake.importPattern.mockReset() })
const disk: Asset = { id: '/test/groove.mid', path: '/test/groove.mid', name: 'groove.mid', folder: false, size: 200 }
describe('pattern assignment', () => {
 it('assigns internal library clips without trying to open their IDs as files', async () => {
  const clip = fake.state.patch.clips[2]
  await drop({ id: clip.id, path: clip.id, name: clip.name, folder: false, size: 3 }, true, 'zone:template:0')
  expect(fake.state.patch.templateZones[0].clipId).toBe(clip.id)
  expect(fake.importPattern).not.toHaveBeenCalled()
  expect(fake.state.patch.clips).toHaveLength(3)
 })
 it('keeps distinct tracks from one file and reuses identical imports', async () => {
  const one: Clip = { ...clone(fake.state.patch.clips[0]), id: 'track-one', path: disk.path, track: 0, channel: 10 }
  const two = { ...clone(one), id: 'track-two', track: 1 }
  fake.importPattern.mockResolvedValueOnce(one).mockResolvedValueOnce(two).mockResolvedValueOnce({ ...clone(one), id: 'duplicate' })
  await drop(disk, true, 'zone:48:0'); await drop(disk, true, 'zone:48:1'); await drop(disk, true, 'zone:48:2')
  expect(fake.state.patch.keys[0].zones.map(z => z.clipId)).toEqual(['track-one', 'track-two', 'track-one'])
  expect(fake.state.patch.clips).toHaveLength(5)
 })
 it('does not apply a delayed import to a recalled patch or a removed zone', async () => {
  let complete!: (clip: Clip) => void
  fake.importPattern.mockImplementation(() => new Promise(resolve => { complete = resolve }))
  const importing = drop(disk, true, 'zone:48:0')
  fake.state.patch = factory()
  complete({ ...clone(fake.state.patch.clips[0]), id: 'late-import' })
  await expect(importing).rejects.toThrow('destination changed')
  expect(fake.state.patch.clips).toHaveLength(3)
 })
})

it('assigns the complete tuned synth preset while retaining the destination zones',async()=>{
 const preset=factoryAssets().find(a=>a.name==='Low tom')!,zones=clone(fake.state.patch.keys[0].zones)
 await drop(preset,false,'key:48')
 const key=fake.state.patch.keys[0]
 expect(key.sound.s.pitch).toBe(-5);expect(key.parts).toEqual([41,43]);expect(key.routes).toEqual(preset.preset!.routes);expect(key.zones).toEqual(zones)
 key.sound.s.pitch=-8;expect(preset.preset!.sound.s.pitch).toBe(-5)
})
