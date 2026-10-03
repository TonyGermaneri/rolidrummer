/**
 * A small Standard MIDI File reader.
 *
 * Enough to get notes out of a file and no more: headers, tracks, running
 * status, note on/off pairing, tempo and time signature. Everything else is
 * skipped by length. Written rather than pulled in because the whole job is a
 * couple of hundred lines and a dependency for this would be silly.
 */

const HEADER = 0x4d546864 // "MThd"
const TRACK = 0x4d54726b // "MTrk"

class Reader {
  constructor(bytes) {
    this.data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes)
    this.at = 0
  }

  get done() {
    return this.at >= this.data.length
  }

  byte() {
    if (this.at >= this.data.length) throw new Error('unexpected end of file')
    return this.data[this.at++]
  }

  uint(bytes) {
    let value = 0
    for (let i = 0; i < bytes; i++) value = (value << 8) | this.byte()
    return value >>> 0
  }

  /** MIDI's variable-length quantity: seven bits per byte, high bit continues. */
  varint() {
    let value = 0
    for (let i = 0; i < 4; i++) {
      const byte = this.byte()
      value = (value << 7) | (byte & 0x7f)
      if ((byte & 0x80) === 0) return value
    }
    return value
  }

  skip(count) {
    this.at += count
  }

  ascii(count) {
    let out = ''
    for (let i = 0; i < count; i++) out += String.fromCharCode(this.byte())
    return out
  }
}

/**
 * @returns {{ppq: number, format: number, tracks: Array, tempos: Array, timeSignature: object}}
 */
export function parseMidiFile(bytes) {
  const reader = new Reader(bytes)

  if (reader.uint(4) !== HEADER) throw new Error('not a MIDI file')
  const headerLength = reader.uint(4)
  const format = reader.uint(2)
  const trackCount = reader.uint(2)
  const division = reader.uint(2)
  reader.skip(headerLength - 6)

  if (division & 0x8000) throw new Error('SMPTE timing is not supported, only ticks per quarter note')
  const ppq = division || 480

  const tracks = []
  const tempos = []
  let timeSignature = { numerator: 4, denominator: 4 }

  for (let index = 0; index < trackCount && !reader.done; index++) {
    if (reader.uint(4) !== TRACK) break
    const length = reader.uint(4)
    const end = reader.at + length
    tracks.push(readTrack(reader, end, tempos, (signature) => { timeSignature = signature }))
    reader.at = end
  }

  return { ppq, format, tracks, tempos, timeSignature }
}

/**
 * The meta events that carry words.
 *
 * A commercial drum library writes its name, its copyright and often the kit it
 * was made for into every file. A folder of several hundred thousand of them is
 * only searchable by what is written inside, so all of it is kept.
 */
const META_TEXT = {
  0x01: 'text',
  0x02: 'copyright',
  0x04: 'instrument',
  0x06: 'marker',
  0x08: 'program',
}

function readTrack(reader, end, tempos, onTimeSignature) {
  const notes = []
  const meta = []
  const open = new Map()
  let tick = 0
  let status = 0
  let name = ''

  while (reader.at < end) {
    tick += reader.varint()
    let byte = reader.byte()

    if (byte < 0x80) {
      // Running status: the previous status byte still applies.
      reader.at--
      byte = status
    } else if (byte < 0xf0) {
      status = byte
    }

    if (byte === 0xff) {
      const type = reader.byte()
      const length = reader.varint()
      const start = reader.at
      // The text a library writes into its files. Copyright and instrument
      // name are where a commercial pack says which kit it was made for, and a
      // marker is often the pattern's own name -- all of it worth keeping,
      // because a folder of eight hundred thousand files is only searchable by
      // what is written in it.
      if (type === 0x03 && !name) name = reader.ascii(length)
      else if (META_TEXT[type]) meta.push({ tick, kind: META_TEXT[type], text: reader.ascii(length) })
      else if (type === 0x51 && length === 3) tempos.push({ tick, usPerQuarter: reader.uint(3) })
      else if (type === 0x58 && length >= 2) {
        onTimeSignature({ numerator: reader.byte(), denominator: 1 << reader.byte() })
      }
      reader.at = start + length
      continue
    }

    if (byte === 0xf0 || byte === 0xf7) {
      reader.skip(reader.varint())
      continue
    }

    const kind = byte & 0xf0
    const channel = byte & 0x0f

    if (kind === 0x90 || kind === 0x80) {
      const note = reader.byte()
      const velocity = reader.byte()
      const key = `${channel}:${note}`
      if (kind === 0x90 && velocity > 0) {
        // A second note-on without a note-off retriggers: close the first.
        closeNote(open, key, notes, tick)
        open.set(key, { tick, note, velocity, channel })
      } else {
        closeNote(open, key, notes, tick)
      }
    } else if (kind === 0xc0 || kind === 0xd0) {
      reader.skip(1)
    } else {
      reader.skip(2)
    }
  }

  for (const key of [...open.keys()]) closeNote(open, key, notes, tick)
  notes.sort((a, b) => a.tick - b.tick || a.note - b.note)
  return { name: name.trim(), notes, meta }
}

function closeNote(open, key, notes, tick) {
  const started = open.get(key)
  if (!started) return
  open.delete(key)
  notes.push({ ...started, duration: Math.max(1, tick - started.tick) })
}

/** Seconds per tick changes with tempo; these walk the map to convert. */
export function tickToSeconds(tick, ppq, tempos) {
  const map = tempos.length ? tempos : [{ tick: 0, usPerQuarter: 500000 }]
  let seconds = 0
  let last = 0
  let us = map[0].tick === 0 ? map[0].usPerQuarter : 500000

  for (const change of map) {
    if (change.tick >= tick) break
    seconds += ((change.tick - last) / ppq) * (us / 1e6)
    last = change.tick
    us = change.usPerQuarter
  }
  return seconds + ((tick - last) / ppq) * (us / 1e6)
}

/**
 * The other direction, for annotations written in seconds -- which is how every
 * corpus I have seen labels its chords.
 */
export function secondsToTick(seconds, ppq, tempos) {
  const map = tempos.length ? tempos : [{ tick: 0, usPerQuarter: 500000 }]
  let elapsed = 0
  let lastTick = 0
  let us = map[0].tick === 0 ? map[0].usPerQuarter : 500000

  for (const change of map) {
    const span = ((change.tick - lastTick) / ppq) * (us / 1e6)
    if (elapsed + span >= seconds) break
    elapsed += span
    lastTick = change.tick
    us = change.usPerQuarter
  }
  return lastTick + ((seconds - elapsed) * 1e6 * ppq) / us
}
