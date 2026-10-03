/** What pressing a pad does while that sound is already playing. */
export type TriggerMode = 'stack' | 'stop' | 'restart' | 'hold'

export const TRIGGER_MODES: TriggerMode[] = ['stop', 'restart', 'stack', 'hold']

export const TRIGGER_MODE_INFO: Record<TriggerMode, { label: string; icon: string; hint: string }> = {
  stop: { label: 'STOP', icon: 'mdi-stop', hint: 'Press again to stop (with release)' },
  restart: { label: 'RESTART', icon: 'mdi-restart', hint: 'Press again to restart from the top' },
  stack: { label: 'STACK', icon: 'mdi-layers-triple', hint: 'Press again to layer another copy' },
  hold: { label: 'HOLD', icon: 'mdi-gesture-tap-hold', hint: 'Plays only while held down' },
}

export type FilterType = 'lowpass' | 'highpass' | 'bandpass'
export const FILTER_TYPES: FilterType[] = ['lowpass', 'highpass', 'bandpass']
export const FILTER_LABEL: Record<FilterType, string> = { lowpass: 'LP', highpass: 'HP', bandpass: 'BP' }

// ── modulation ─────────────────────────────────────────────────────────────
export type ModSource =
  | 'lfo1' | 'lfo2' | 'mod' | 'aftertouch' | 'velocity' | 'bend' | 'timbre' | 'vco1' | 'vco2' | 'vco3'
export type ModDest =
  | 'pitch' | 'cutoff' | 'resonance' | 'volume' | 'pan' | 'grainPos' | 'grainSize' | 'delayMix' | 'reverbMix'
export type LfoShape = 'sine' | 'triangle' | 'square' | 'sawtooth' | 'rampDown' | 'random' | 'smooth'

export const MOD_SOURCES: { id: ModSource; label: string }[] = [
  { id: 'lfo1', label: 'LFO 1' },
  { id: 'lfo2', label: 'LFO 2' },
  { id: 'mod', label: 'MOD WHEEL' },
  { id: 'aftertouch', label: 'AFTERTOUCH' },
  { id: 'velocity', label: 'VELOCITY' },
  { id: 'bend', label: 'PITCH BEND' },
  { id: 'timbre', label: 'TIMBRE (CC74)' },
  { id: 'vco1', label: 'VCO 1' },
  { id: 'vco2', label: 'VCO 2' },
  { id: 'vco3', label: 'VCO 3' },
]
export const VCO_SOURCES = ['vco1', 'vco2', 'vco3'] as const

/**
 * Another pad layered onto this one as an oscillator: played with every note of this pad, heard (MIX) and/or used
 * as an audio-rate mod source (VCO 1–3 in the matrix).
 */
export interface VcoSlot {
  soundId: string | null
  level: number // 0..1.5 mix level
  audible: boolean // false = mod only
  track: boolean // follow the played key; false = play fixedNote
  transpose: number // semitones
  fine: number // cents
  fixedNote: number // MIDI note when not tracking
}
export const defaultVco = (): VcoSlot => ({
  soundId: null,
  level: 1,
  audible: true,
  track: true,
  transpose: 0,
  fine: 0,
  fixedNote: 60,
})
export const MAX_VCOS = 3

/**
 * scale: destination units at amount ±1.
 * audio: wired as an AudioParam connection (otherwise read when each grain starts).
 */
export const MOD_DESTS: Record<ModDest, { label: string; scale: number; unit: string; audio: boolean }> = {
  pitch: { label: 'PITCH', scale: 12, unit: 'st', audio: true },
  cutoff: { label: 'CUTOFF', scale: 5, unit: 'oct', audio: true },
  resonance: { label: 'RESONANCE', scale: 10, unit: 'Q', audio: true },
  volume: { label: 'VOLUME', scale: 1, unit: '', audio: true },
  pan: { label: 'PAN', scale: 1, unit: '', audio: true },
  grainPos: { label: 'GRAIN POS', scale: 0.5, unit: '', audio: false },
  grainSize: { label: 'GRAIN SIZE', scale: 250, unit: 'ms', audio: false },
  delayMix: { label: 'DELAY MIX', scale: 1, unit: '', audio: true },
  reverbMix: { label: 'REVERB MIX', scale: 1, unit: '', audio: true },
}
export const MOD_DEST_IDS = Object.keys(MOD_DESTS) as ModDest[]
export const LFO_SHAPES: LfoShape[] = ['sine', 'triangle', 'square', 'sawtooth', 'rampDown', 'random', 'smooth']

/** Tempo divisions for synced LFOs / delay, in beats (quarter notes). */
export const DIVISIONS: { id: string; beats: number }[] = [
  { id: '4 BAR', beats: 16 },
  { id: '2 BAR', beats: 8 },
  { id: '1/1', beats: 4 },
  { id: '1/2', beats: 2 },
  { id: '1/2T', beats: 4 / 3 },
  { id: '1/4.', beats: 1.5 },
  { id: '1/4', beats: 1 },
  { id: '1/4T', beats: 2 / 3 },
  { id: '1/8.', beats: 0.75 },
  { id: '1/8', beats: 0.5 },
  { id: '1/8T', beats: 1 / 3 },
  { id: '1/16.', beats: 0.375 },
  { id: '1/16', beats: 0.25 },
  { id: '1/16T', beats: 1 / 6 },
  { id: '1/32', beats: 0.125 },
]
export const divisionBeats = (id: string) => DIVISIONS.find((d) => d.id === id)?.beats ?? 1

export interface Lfo {
  shape: LfoShape
  rate: number // Hz, when not synced
  sync: boolean
  division: string // DIVISIONS id, when synced
}

/** LFO frequency in Hz: free rate, or one cycle per division at the current tempo. */
export const lfoHz = (lfo: Lfo, bpm: number) => (lfo.sync ? bpm / 60 / divisionBeats(lfo.division) : lfo.rate)
export interface ModRoute {
  source: ModSource
  dest: ModDest
  amount: number // -1..1 of the destination's scale
}
export interface ModMatrix {
  lfo1: Lfo
  lfo2: Lfo
  routes: ModRoute[]
}
export const defaultMatrix = (): ModMatrix => ({
  lfo1: { shape: 'sine', rate: 5, sync: false, division: '1/8' },
  lfo2: { shape: 'triangle', rate: 0.5, sync: false, division: '1/1' },
  routes: [],
})
const migrateMatrix = (m?: Partial<ModMatrix>): ModMatrix => {
  const d = defaultMatrix()
  return { lfo1: { ...d.lfo1, ...m?.lfo1 }, lfo2: { ...d.lfo2, ...m?.lfo2 }, routes: m?.routes ?? [] }
}

/** tape: pitch and speed both change playback rate. stretch: granular, speed and pitch independent. */
export type TimeMode = 'tape' | 'stretch'

export interface SoundSettings {
  name: string
  tag: string
  // play
  volume: number // 0..1.5 linear gain
  pan: number // -1..1
  repeat: number // 0 = loop forever, N = play N times
  choke: number // 0 = none, 1..8 = choke group
  mode: TriggerMode
  // tune
  pitch: number // semitones -24..24
  fine: number // cents -100..100
  speed: number // 0.25..4
  timeMode: TimeMode
  // clip
  clipIn: number // 0..1 of duration
  clipOut: number // 0..1 of duration
  // amp envelope (seconds, sustain 0..1)
  attack: number
  decay: number
  sustain: number
  release: number
  // filter
  filterType: FilterType
  cutoff: number // Hz
  resonance: number // Q
  // filter envelope
  fEnvAmount: number // octaves -5..5
  fAttack: number
  fDecay: number
  fSustain: number
  fRelease: number
  // grain cloud (audio/grains.ts)
  grain: boolean // on: the pad plays as a cloud of grains
  grainSize: number // ms, down to one sample (GRAIN_MIN_MS)
  grainPos: number // 0..1 within clip
  grainWidth: number // 0..1 of clip, random spread (spray)
  grainRate: number // grains per second per stream, independent of size (gaps when rate × size < 1)
  grainShape: number // window: 0 = square .. 1 = Hann (a Tukey window's taper)
  grainJitter: number // random ± semitones per grain
  grainReverse: number // 0..1 chance a grain plays backwards
  grainSpread: number // 0..1 random stereo pan per grain
  grainStreams: number // 1..8 independent grain streams per note
  grainScatter: number // 0 = a steady rate .. 1 = random (Poisson) times, same average rate
  grainDrift: number // 0..1 each stream wanders through the clip at its own random speed (±1 = real time)
  // delay
  delayTime: number // seconds
  delayFeedback: number // 0..0.9
  delayMix: number // 0..1
  // reverb
  reverbSize: number // seconds of impulse
  reverbDecay: number // decay exponent
  reverbMix: number // 0..1
  // eq (dB)
  eqLow: number
  eqMid: number
  eqHigh: number
  // keys
  rootNote: number // MIDI note that plays the sample at its recorded pitch
  velAmount: number // 0..1 how much velocity changes volume
  bendRange: number // semitones
  // modulation
  mod: ModMatrix
  /** linked pads played as extra oscillators (max 3) */
  vcos: VcoSlot[]
  // midi
  midiNote: number | null
}

export type LoopMode = 'no_loop' | 'one_shot' | 'loop_continuous' | 'loop_sustain'

/** One sample of a multi-sample (SFZ) instrument, mapped to a key × velocity range. Times in seconds. */
export interface Zone {
  audioId: string
  lokey: number
  hikey: number
  lovel: number // 1..127
  hivel: number
  keycenter: number
  keytrack: number // cents per key / 100 (100 = normal)
  transpose: number // semitones
  tune: number // cents
  volume: number // dB
  pan: number // -100..100
  offset: number
  end: number // 0 = to the end of the sample
  loopMode: LoopMode
  loopStart: number
  loopEnd: number
  seqLength: number
  seqPosition: number
  lorand: number
  hirand: number
  env?: { a?: number; d?: number; s?: number; r?: number }
  /** base amplitude % when amplitude_onccN is present */
  amplitude?: number
  /** 'release' regions sound on note-off (piano damper noise, etc.) */
  trigger?: 'attack' | 'release'
  /** release regions: dB lost per second the note was held */
  rtDecay?: number
  /** only sounds while these CCs are inside their ranges (0..127) */
  ccRange?: Record<number, [number, number]>
  /** CC-driven offsets: amount applies at CC = 127 */
  ccMods?: ZoneCcMod[]
  filter?: ZoneFilter
  lfos?: ZoneLfo[]
}

export interface ZoneCcMod {
  cc: number
  target: 'volume' | 'amplitude' | 'pan' | 'pitch' | 'cutoff' | 'resonance'
  amount: number // dB, %, %, cents, cents, dB
}

export interface ZoneFilter {
  type: BiquadFilterType
  cutoff: number // Hz
  resonance: number // dB
  keytrack: number // cents per key from keycenter
  keycenter: number
  veltrack: number // cents at velocity 127
  env?: { a: number; d: number; s: number; r: number; depth: number } // depth in cents
}

export interface ZoneLfo {
  target: 'pitch' | 'cutoff' | 'volume' | 'pan'
  freq: number
  depth: number // cents, cents, dB, %
  delay: number // seconds before it starts
  wave: OscillatorType
  invert?: boolean
}

export interface Sound {
  id: string
  /** Audio blobs are shared between duplicates, so they're keyed separately. For SFZ pads: the zone nearest the root. */
  audioId: string
  fileName: string
  settings: SoundSettings
  /** present for SFZ instruments: the pad plays zones chosen by note and velocity */
  zones?: Zone[]
  /** SFZ <control> set_ccN / set_hdccN: controller values (0..1) the instrument expects to start from */
  ccDefaults?: Record<number, number>
  fav?: boolean
}

/** Every audio blob a pad needs (its own plus its zones'). */
export const soundAudioIds = (s: Sound) => [s.audioId, ...(s.zones ?? []).map((z) => z.audioId)]

/** One sample at 48 kHz, the shortest grain (the engines make every grain a whole number of samples, at least one). */
export const GRAIN_MIN_MS = 1000 / 48000
export const GRAIN_RATE_MIN = 0.5
export const GRAIN_RATE_MAX = 1000

/** A cloud that sounds like one: grains sprayed around POS at loosely random times, spread in stereo. */
export const GRAIN_DEFAULTS = {
  grain: false,
  grainSize: 80,
  grainPos: 0.5,
  grainWidth: 0.1,
  grainRate: 30,
  grainShape: 1,
  grainJitter: 0,
  grainReverse: 0,
  grainSpread: 0.3,
  grainStreams: 1,
  grainScatter: 0.5,
  grainDrift: 0,
}

export function defaultSettings(name = 'SOUND'): SoundSettings {
  return {
    name,
    tag: '',
    volume: 1,
    pan: 0,
    repeat: 1,
    choke: 0,
    mode: 'stop',
    pitch: 0,
    fine: 0,
    speed: 1,
    timeMode: 'tape',
    clipIn: 0,
    clipOut: 1,
    attack: 0.001,
    decay: 0.2,
    sustain: 1,
    release: 0.02,
    filterType: 'lowpass',
    cutoff: 20000,
    resonance: 0.7,
    fEnvAmount: 0,
    fAttack: 0.001,
    fDecay: 0.3,
    fSustain: 0,
    fRelease: 0.2,
    ...GRAIN_DEFAULTS,
    delayTime: 0.25,
    delayFeedback: 0.35,
    delayMix: 0,
    reverbSize: 2,
    reverbDecay: 3,
    reverbMix: 0,
    eqLow: 0,
    eqMid: 0,
    eqHigh: 0,
    rootNote: 60,
    velAmount: 1,
    bendRange: 2,
    mod: defaultMatrix(),
    vcos: [],
    midiNote: null,
  }
}

/**
 * Before 0.2 grainSize 0 meant off and grainDensity counted overlapping grains. A pad that had grains keeps
 * them (the same overlap, as a rate); one that never used them gets the new defaults for when it's switched on.
 */
function migrateGrains(raw: Record<string, unknown>) {
  if (raw.grain === undefined) {
    const size = Number(raw.grainSize) || 0
    if (size > 0) {
      raw.grain = true
      raw.grainRate = Math.min(GRAIN_RATE_MAX, Math.max(GRAIN_RATE_MIN, (Number(raw.grainDensity) || 2) / (size / 1000)))
    } else Object.assign(raw, GRAIN_DEFAULTS)
  }
  delete raw.grainDensity
}

/** Fill in settings added since a board was saved, and map renamed ones. */
export function migrateSettings(raw: Partial<SoundSettings> & { fadeIn?: number; fadeOut?: number }): SoundSettings {
  const { fadeIn, fadeOut, ...rest } = raw
  migrateGrains(rest as Record<string, unknown>)
  const s = { ...defaultSettings(raw.name), ...rest, mod: migrateMatrix(raw.mod), vcos: raw.vcos ?? [] }
  if (fadeIn !== undefined && raw.attack === undefined) s.attack = Math.max(0.001, fadeIn)
  if (fadeOut !== undefined && raw.release === undefined) s.release = Math.max(0.001, fadeOut)
  return s
}

export interface GlobalFx {
  chorusRate: number // Hz
  chorusDepth: number // 0..1
  chorusMix: number // 0..1
  delayTime: number
  delayFeedback: number
  delayMix: number
  reverbSize: number
  reverbDecay: number
  reverbMix: number
  delaySync: boolean
  delayDivision: string
}

export const defaultGlobalFx = (): GlobalFx => ({
  chorusRate: 0.6,
  chorusDepth: 0.5,
  chorusMix: 0,
  delayTime: 0.375,
  delayFeedback: 0.4,
  delayMix: 0,
  reverbSize: 3,
  reverbDecay: 3,
  reverbMix: 0,
  delaySync: false,
  delayDivision: '1/8.',
})

export interface MasterState {
  volume: number
  muted: boolean
  scanlines: boolean
  /** pad size multiplier */
  scale: number
  /** keyboard-play mode: MIDI / computer keys play the selected patch chromatically */
  play: boolean
  mono: boolean
  /** seconds to slide between notes */
  glide: number
  /** computer-keyboard piano octave shift */
  octave: number
  selectedId: string | null
  /** MPE lower zone: channel 1 = master, 2-16 = one note each */
  mpe: boolean
  /** per-note pitch bend range in MPE, semitones */
  mpeBendRange: number
  /** MIDI notes map to pads in grid order, starting at midiBase */
  midiAuto: boolean
  midiBase: number
  /** internal tempo; used unless following MIDI clock */
  bpm: number
  clockSource: 'internal' | 'midi'
  theme: string
  /** pads = button grid, grid = spreadsheet (canvas-datagrid) */
  view: 'pads' | 'grid'
  /** show the catalog (cards / grid): beside the rack (right third) when the rack is on, else full width */
  list: boolean
  /** list pane width beside the rack, as a fraction of the window (drag the divider) */
  listWidth: number
  /** control-panel sections folded to their title (shared by every panel) */
  folded: string[]
  /** show only favourites, per catalog */
  favSounds: boolean
  favPatches: boolean
  /** factory sounds + patches have been installed once */
  factory: boolean
  /** show the selected patch (+ its VCOs) panels under the header */
  rack: boolean
  /** which catalog is showing */
  tab: 'sounds' | 'patches'
  /** the patch the keyboard plays and the header ◀ ▶ steps through */
  patchId: string | null
  fx: GlobalFx
  /** global modulation matrix: applies to every voice */
  mod: ModMatrix
}

export const defaultMaster = (): MasterState => ({
  volume: 0.8,
  muted: false,
  scanlines: true,
  scale: 1,
  play: false,
  mono: false,
  glide: 0,
  octave: 0,
  selectedId: null,
  mpe: false,
  mpeBendRange: 48,
  midiAuto: true,
  midiBase: 36,
  bpm: 120,
  clockSource: 'internal',
  theme: 'console85',
  view: 'pads',
  list: true,
  listWidth: 0.33,
  folded: ['eq', 'delay', 'reverb'],
  favSounds: false,
  favPatches: false,
  factory: false,
  rack: false,
  tab: 'sounds',
  patchId: null,
  fx: defaultGlobalFx(),
  mod: defaultMatrix(),
})

export function migrateMaster(raw?: Partial<MasterState>): MasterState {
  return { ...defaultMaster(), ...raw, fx: { ...defaultGlobalFx(), ...raw?.fx }, mod: migrateMatrix(raw?.mod) }
}

// ── presets ────────────────────────────────────────────────────────────────
export interface Preset {
  id: string
  name: string
  settings: Partial<SoundSettings>
}

/** Sample-specific settings a preset leaves alone when copied to another pad. */
export const PRESET_EXCLUDE = ['name', 'tag', 'midiNote', 'rootNote', 'clipIn', 'clipOut', 'vcos'] as const

export function presetSettings(s: SoundSettings): Partial<SoundSettings> {
  const copy: Partial<SoundSettings> = JSON.parse(JSON.stringify(s))
  for (const k of PRESET_EXCLUDE) delete copy[k]
  return copy
}

// ── patches ──────────────────────────────────────────────────────────────────
/** One sound inside a patch: the patch's own copy of its settings, playing that sound's audio. */
export interface PatchLayer {
  id: string
  soundId: string
  settings: SoundSettings
}

/** The header knobs a patch remembers. */
export interface PatchHeader {
  volume: number
  glide: number
  mono: boolean
  mpe: boolean
  mpeBendRange: number
  bpm: number
  fx: GlobalFx
  mod: ModMatrix
}

/**
 * A patch: layers[0] is the main voice; its settings.vcos slots point at the other layers by layer id.
 * Plus the header settings that go with it.
 */
/**
 * A patch slot: a layer plus how it joins the patch. Slot 1 is the main voice (always heard, follows the keyboard);
 * slots 2–3 layer on top of it. In the mod matrix, VCO n is slot n's live signal.
 */
export interface PatchSlot {
  layer: PatchLayer
  level: number
  audible: boolean // false = mod only
  track: boolean // follow the played key; false = play fixedNote
  transpose: number
  fine: number
  fixedNote: number
}
export const PATCH_SLOTS = 3

export interface Patch {
  id: string
  name: string
  tag: string
  fav?: boolean
  /** always PATCH_SLOTS long; null = empty slot */
  slots: (PatchSlot | null)[]
  header: PatchHeader
}

export const patchLayers = (p: Patch) => p.slots.filter((x): x is PatchSlot => !!x).map((x) => x.layer)
/** the slot that plays as the main voice: slot 1, or the first filled one */
export const carrierSlot = (p: Patch) => p.slots.find((x): x is PatchSlot => !!x) ?? null
export const newSlot = (layer: PatchLayer): PatchSlot => ({ layer, ...defaultVco() })

export const headerOf = (m: MasterState): PatchHeader =>
  JSON.parse(
    JSON.stringify({
      volume: m.volume,
      glide: m.glide,
      mono: m.mono,
      mpe: m.mpe,
      mpeBendRange: m.mpeBendRange,
      bpm: m.bpm,
      fx: m.fx,
      mod: m.mod,
    }),
  )

export function migratePatch(p: Patch & { layers?: PatchLayer[] }): Patch {
  const h = p.header ?? ({} as Partial<PatchHeader>)
  const d = defaultMaster()
  const fix = (l: PatchLayer): PatchLayer => ({ ...l, settings: migrateSettings(l.settings) })
  let slots: (PatchSlot | null)[]
  if (p.slots) slots = p.slots.map((x) => (x ? { ...defaultVco(), ...x, layer: fix(x.layer) } : null))
  else {
    // older patches: layers[0] + its settings.vcos → slots 1..3
    const [main, ...rest] = p.layers ?? []
    slots = main ? [newSlot(fix(main))] : [null]
    for (const v of main?.settings.vcos ?? []) {
      const l = rest.find((x) => x.id === v.soundId)
      if (l && slots.length < PATCH_SLOTS) slots.push({ ...v, layer: fix(l) } as PatchSlot)
    }
    if (main) slots[0]!.layer.settings.vcos = []
  }
  while (slots.length < PATCH_SLOTS) slots.push(null)
  const { layers: _old, ...rest } = p
  void _old
  return {
    ...rest,
    tag: p.tag ?? '',
    slots: slots.slice(0, PATCH_SLOTS),
    header: {
      ...headerOf(d),
      ...h,
      fx: { ...defaultGlobalFx(), ...h.fx },
      mod: migrateMatrix(h.mod),
    },
  }
}

/** A saved preset's settings, with grain fields from before 0.2 mapped (see migrateGrains). */
export function migratePresetSettings(p: Partial<SoundSettings>): Partial<SoundSettings> {
  const copy = { ...p } as Record<string, unknown>
  migrateGrains(copy)
  return copy as Partial<SoundSettings>
}
