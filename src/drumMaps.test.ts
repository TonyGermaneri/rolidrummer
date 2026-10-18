import {expect,it} from 'vitest'
import maps from './data/drumMaps.json'
import {DRUM_VOICES,GENERAL_MIDI,GENERAL_MIDI_IN,TD11_TO_VOICE} from './core/drumKits.js'

it('keeps the native source-note vocabulary identical to the browser vocabulary',()=>{
 expect(maps.voices).toEqual(DRUM_VOICES)
 expect(maps.canonical).toEqual(GENERAL_MIDI)
 expect(maps.maps.gm).toEqual(GENERAL_MIDI_IN)
 expect(maps.maps.vdrums).toEqual({...GENERAL_MIDI_IN,...TD11_TO_VOICE})
})
