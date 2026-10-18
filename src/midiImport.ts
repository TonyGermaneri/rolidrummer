import { reactive } from 'vue'
import { request } from './native/bridge'
import type { Asset, Clip } from './model'
export interface MidiTrack { index: number; name: string; notes: number; channels: number[] }
export interface MidiInfo { tracks: MidiTrack[]; channels: number[]; autoChannel: number; ppq: number }
export interface MidiSelection { channel: number; track: number }
export const midiImport = reactive({ open: false, name: '', info: null as MidiInfo | null, channel: 0, track: -1 })
let resolveSelection: ((selection: MidiSelection | null) => void) | undefined
export function finishMidiImport(accept: boolean) {
 const resolve = resolveSelection; resolveSelection = undefined
 midiImport.open = false
 resolve?.(accept ? { channel: midiImport.channel, track: midiImport.track } : null)
}
export async function importPattern(asset: Asset): Promise<Clip | null> {
 if (asset.libraryId) return request<Clip>('libraryPattern', asset.libraryId)
 if (resolveSelection) throw Error('Finish selecting the current MIDI pattern first.')
 const info = await request<MidiInfo>('inspectMidi', asset.path)
 if (!info.channels.length) throw Error('This MIDI file has no notes.')
 let selection: MidiSelection | null = { channel: info.autoChannel, track: -1 }
 if (info.channels.length > 1 || info.tracks.filter(t => t.notes).length > 1) {
  if (resolveSelection) throw Error('Finish selecting the current MIDI pattern first.')
  Object.assign(midiImport, { open: true, name: asset.name, info, channel: info.autoChannel > 0 ? info.autoChannel : 0, track: -1 })
  selection = await new Promise<MidiSelection | null>(resolve => { resolveSelection = resolve })
 }
 return selection ? request<Clip>('importMidi', { path: asset.path, ...selection }) : null
}
