/**
 * Original sample rate from a file header. decodeAudioData resamples to the context rate, but SFZ offsets and loop
 * points are counted in the file's own samples, so we need the source rate to convert them to seconds.
 */
export function sniffSampleRate(buf: ArrayBuffer): number | null {
  const v = new DataView(buf)
  const tag = (o: number, n = 4) => String.fromCharCode(...new Uint8Array(buf, o, Math.min(n, buf.byteLength - o)))
  if (buf.byteLength < 12) return null

  // WAV: RIFF....WAVE, then chunks; "fmt " holds the rate at +12
  if (tag(0) === 'RIFF' && tag(8) === 'WAVE') {
    let o = 12
    while (o + 8 <= buf.byteLength) {
      const id = tag(o)
      const size = v.getUint32(o + 4, true)
      if (id === 'fmt ' && o + 16 <= buf.byteLength) return v.getUint32(o + 12, true)
      o += 8 + size + (size & 1)
    }
    return null
  }
  // FLAC: "fLaC" + STREAMINFO; rate is the top 20 bits at byte 18 of the file
  if (tag(0) === 'fLaC' && buf.byteLength >= 22) {
    return (v.getUint8(18) << 12) | (v.getUint8(19) << 4) | (v.getUint8(20) >> 4)
  }
  // Ogg Vorbis: identification header "\x01vorbis", rate at +12 after the packet type
  if (tag(0) === 'OggS') {
    const bytes = new Uint8Array(buf, 0, Math.min(buf.byteLength, 256))
    for (let i = 0; i < bytes.length - 16; i++) {
      if (bytes[i] === 1 && tag(i + 1, 6) === 'vorbis') return v.getUint32(i + 12, true)
      if (tag(i, 8) === 'OpusHead') return 48000 // opus always decodes at 48k
    }
  }
  return null
}
