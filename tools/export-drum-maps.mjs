import {writeFileSync} from 'node:fs'
import {DRUM_VOICES, GENERAL_MIDI, GENERAL_MIDI_IN, TD11_TO_VOICE} from '../src/core/drumKits.js'

// Native and browser importers share a versioned generated map. Parity is tested.
const data={version:1,voices:DRUM_VOICES,canonical:GENERAL_MIDI,maps:{gm:GENERAL_MIDI_IN,vdrums:{...GENERAL_MIDI_IN,...TD11_TO_VOICE}}}
writeFileSync(new URL('../src/data/drumMaps.json',import.meta.url),JSON.stringify(data,null,2)+'\n')
