/**
 * Drums, and the fact that nobody agrees where they live.
 *
 * A drum note is not a pitch. 38 is not "D2", it is "snare" -- and only on a kit
 * that put the snare there. The corpus was played on a Roland TD-11, General
 * MIDI put the same drums in different places, and every sampler since has had
 * its own opinion. A groove sent to the wrong map does not sound transposed, it
 * sounds like somebody hitting the wrong drums, which is worse because it reads
 * as a broken feature rather than a wrong setting.
 *
 * So nothing is mapped pitch-to-pitch. Everything goes through a vocabulary of
 * fourteen voices: the corpus is read *into* it, and a kit map writes it back
 * out. Two small tables instead of one large one per pair, and the half that
 * comes from the corpus is published by the people who recorded it.
 *
 * @see https://magenta.tensorflow.org/datasets/groove
 */

/**
 * What a drummer is actually doing, independent of where any kit put it.
 *
 * Fourteen rather than the nine the Groove paper reduces to: that reduction is
 * for training a model, and it throws away the side stick, the pedal hat and
 * the ride bell -- which are not details to a listener, they are the difference
 * between a bossa and a rock beat.
 */
export const DRUM_VOICES = [
  { id: 'kick', name: 'Kick' },
  { id: 'snare', name: 'Snare' },
  { id: 'snareRim', name: 'Snare rimshot' },
  { id: 'sideStick', name: 'Side stick' },
  { id: 'tomHigh', name: 'High tom' },
  { id: 'tomMid', name: 'Mid tom' },
  { id: 'tomFloor', name: 'Floor tom' },
  { id: 'hatClosed', name: 'Closed hi-hat' },
  { id: 'hatOpen', name: 'Open hi-hat' },
  { id: 'hatPedal', name: 'Pedal hi-hat' },
  { id: 'crash1', name: 'Crash 1' },
  { id: 'crash2', name: 'Crash 2' },
  { id: 'ride', name: 'Ride' },
  { id: 'rideBell', name: 'Ride bell' },

  /*
   * And the rest of General MIDI percussion.
   *
   * Appended, never inserted. A DAW remembers automation by parameter index
   * and the plugin publishes one mute switch per voice in this order, so the
   * fourteen above keep their places and their automation for ever.
   * @see native/plugin/PluginProcessor.cpp
   *
   * These exist because without them a sixth of the collection is silent.
   * Counted over all 774,268 files: 14.6% of every note played is a General
   * MIDI percussion instrument this vocabulary had no voice for, so it was
   * read, filed, drawn in the list -- and dropped on the way to the
   * synthesiser. Tambourine alone is 2.24% of the corpus, congas 2.45%,
   * maracas 1.67%. Four libraries are more than half percussion; `Africa` is
   * 77% and played nothing at all.
   *
   * One voice per instrument the standard names, rather than a judgement
   * about which are alike enough to fold. Folding is how a vocabulary comes
   * to disagree with the thing it is describing, and the standard is not
   * ambiguous about these.
   */
  { id: 'clap', name: 'Hand clap', percussion: true },
  { id: 'tambourine', name: 'Tambourine', percussion: true },
  { id: 'cowbell', name: 'Cowbell', percussion: true },
  { id: 'vibraslap', name: 'Vibraslap', percussion: true },
  { id: 'bongoHigh', name: 'High bongo', percussion: true },
  { id: 'bongoLow', name: 'Low bongo', percussion: true },
  { id: 'congaMute', name: 'Muted conga', percussion: true },
  { id: 'congaHigh', name: 'High conga', percussion: true },
  { id: 'congaLow', name: 'Low conga', percussion: true },
  { id: 'timbaleHigh', name: 'High timbale', percussion: true },
  { id: 'timbaleLow', name: 'Low timbale', percussion: true },
  { id: 'agogoHigh', name: 'High agogo', percussion: true },
  { id: 'agogoLow', name: 'Low agogo', percussion: true },
  { id: 'cabasa', name: 'Cabasa', percussion: true },
  { id: 'maracas', name: 'Maracas', percussion: true },
  { id: 'whistleShort', name: 'Short whistle', percussion: true },
  { id: 'whistleLong', name: 'Long whistle', percussion: true },
  { id: 'guiroShort', name: 'Short guiro', percussion: true },
  { id: 'guiroLong', name: 'Long guiro', percussion: true },
  { id: 'claves', name: 'Claves', percussion: true },
  { id: 'woodBlockHigh', name: 'High wood block', percussion: true },
  { id: 'woodBlockLow', name: 'Low wood block', percussion: true },
  { id: 'cuicaMute', name: 'Muted cuica', percussion: true },
  { id: 'cuicaOpen', name: 'Open cuica', percussion: true },
  { id: 'triangleMute', name: 'Muted triangle', percussion: true },
  { id: 'triangleOpen', name: 'Open triangle', percussion: true },
]

/**
 * The fourteen a drum kit has, as against the percussion around it.
 *
 * The piano roll draws every one of these whether the pattern uses it or not
 * -- the thing most worth knowing about a beat is often that there is *no*
 * ride in it, and a row that comes and goes cannot say that. It cannot do the
 * same for forty voices, so the percussion is drawn only where it is played.
 * @see components/DrumBook.vue
 */
export const KIT_VOICES = DRUM_VOICES.slice(0, 14).map((one) => one.id)

const VOICE_IDS = new Set(DRUM_VOICES.map((voice) => voice.id))

/**
 * The corpus, read into the vocabulary.
 *
 * This is Magenta's own published table for the Groove MIDI Dataset -- the kit
 * was a Roland TD-11 and its 22 pitches are listed with what each one is. The
 * bow and edge of a cymbal are one voice here because no sampler in the list
 * below has a separate note for the edge of a crash; the distinction is real and
 * there is nowhere to put it.
 */
export const TD11_TO_VOICE = {
  36: 'kick',
  38: 'snare',
  40: 'snareRim',
  37: 'sideStick',
  48: 'tomHigh',  50: 'tomHigh',      // head, rim
  45: 'tomMid',   47: 'tomMid',
  43: 'tomFloor', 58: 'tomFloor',
  46: 'hatOpen',  26: 'hatOpen',      // bow, edge
  42: 'hatClosed', 22: 'hatClosed',
  44: 'hatPedal',
  49: 'crash1',   55: 'crash1',
  57: 'crash2',   52: 'crash2',
  51: 'ride',     59: 'ride',
  53: 'rideBell',
}

/**
 * General MIDI percussion, which is the answer more often than it looks.
 *
 * Roland's TR-8S ships with exactly these numbers for all eleven voices it has.
 * Addictive Drums 2 includes a "General MIDI (GM)" map preset among its sixty-
 * odd, and XLN's own advice for a kit not in that list is to use it. Native
 * Instruments' Abbey Road drummers have a MIDI mapping page that does the same
 * job. Live's Drum Rack labels every pad with "standard GM drum equivalents".
 *
 * So jamin sends GM and lets the instrument's own mapper do the last step --
 * which is both more accurate than guessing at a proprietary layout and correct
 * when somebody changes it, because the mapper is where they would change it.
 */
export const GENERAL_MIDI = {
  kick: 36,
  snare: 38,
  // 38, not 40. A rimshot is a snare hit with the stick across head and rim --
  // louder and sharper, but a snare. General MIDI 40 is *Electric Snare*, an
  // entirely different instrument, and on an acoustic-kit sampler it is either
  // missing or something unrelated. Magenta collapse this to the snare in their
  // own paper mapping for the same reason. The accent is lost; the drum is not,
  // and 3,614 hits in the corpus land here -- many of them the backbeat.
  snareRim: 38,
  sideStick: 37,
  tomHigh: 50,
  tomMid: 47,
  tomFloor: 43,
  hatClosed: 42,
  hatOpen: 46,
  hatPedal: 44,
  crash1: 49,
  crash2: 57,
  ride: 51,
  rideBell: 53,

  // The percussion, at the numbers the standard gives them.
  clap: 39,
  tambourine: 54,
  cowbell: 56,
  vibraslap: 58,
  bongoHigh: 60,
  bongoLow: 61,
  congaMute: 62,
  congaHigh: 63,
  congaLow: 64,
  timbaleHigh: 65,
  timbaleLow: 66,
  agogoHigh: 67,
  agogoLow: 68,
  cabasa: 69,
  maracas: 70,
  whistleShort: 71,
  whistleLong: 72,
  guiroShort: 73,
  guiroLong: 74,
  claves: 75,
  woodBlockHigh: 76,
  woodBlockLow: 77,
  cuicaMute: 78,
  cuicaOpen: 79,
  triangleMute: 80,
  triangleOpen: 81,
}

/**
 * General MIDI, read *inwards*.
 *
 * The other direction. `GENERAL_MIDI` says where to send a voice; this says what
 * an arriving note means. They are not inverses of each other and cannot be: the
 * standard defines six toms and two snares where this vocabulary has three and
 * two, so several notes fold onto one voice on the way in and only one of them
 * comes back out.
 *
 * It matters because an imported library is read with this and not with the
 * corpus's own map. Without it a General MIDI pack's acoustic bass drum (35),
 * low floor tom (41) and low tom (45) are notes nothing understands, and they
 * are dropped in silence.
 */
export const GENERAL_MIDI_IN = {
  35: 'kick', 36: 'kick',
  37: 'sideStick',
  38: 'snare', 40: 'snareRim',
  41: 'tomFloor', 43: 'tomFloor',
  45: 'tomMid', 47: 'tomMid',
  48: 'tomHigh', 50: 'tomHigh',
  42: 'hatClosed', 44: 'hatPedal', 46: 'hatOpen',

  /*
   * And the hi-hat edge, which General MIDI does not define and almost every
   * drum library sends anyway.
   *
   * 22 and 26 are not in the standard -- they sit below its range entirely --
   * but a hi-hat with a separate edge articulation has to put it somewhere,
   * and these two are where the industry put it. Measured across the
   * collection they are 1.5% of every note played, in libraries whose toms
   * are at General MIDI's numbers: General MIDI kits with a Roland-style hat,
   * not Roland kits. Read as anything else they are silence.
   *
   * Which voice each is, is a judgement jamin has already made once, for the
   * Roland kit the shipped corpus was played on -- edge-closed and edge-open.
   * Agreeing with itself is worth more here than a second opinion.
   * @see TD11_TO_VOICE
   */
  22: 'hatClosed', 26: 'hatOpen',
  49: 'crash1', 55: 'crash1',
  52: 'crash2', 57: 'crash2',
  51: 'ride', 59: 'ride', 53: 'rideBell',

  /*
   * And the percussion, 39 to 81, which this table used to stop short of.
   *
   * It read twenty-one of the standard's forty-seven instruments. The other
   * twenty-six are every hand percussion General MIDI defines, and they are
   * 14.6% of every note in the collection -- read in, filed, listed, and
   * dropped in silence on the way out. A pack of congas was a pack of nothing.
   */
  39: 'clap',
  54: 'tambourine',
  56: 'cowbell',
  58: 'vibraslap',
  60: 'bongoHigh', 61: 'bongoLow',
  62: 'congaMute', 63: 'congaHigh', 64: 'congaLow',
  65: 'timbaleHigh', 66: 'timbaleLow',
  67: 'agogoHigh', 68: 'agogoLow',
  69: 'cabasa', 70: 'maracas',
  71: 'whistleShort', 72: 'whistleLong',
  73: 'guiroShort', 74: 'guiroLong',
  75: 'claves',
  76: 'woodBlockHigh', 77: 'woodBlockLow',
  78: 'cuicaMute', 79: 'cuicaOpen',
  80: 'triangleMute', 81: 'triangleOpen',
}

/**
 * The kits offered in the dropdown, and what is actually known about each.
 *
 * `notes` is what a person needs to do at the other end; it is shown next to the
 * choice rather than buried, because "it plays the wrong drums" is nearly always
 * a map preset left on the wrong setting and nearly never jamin.
 */
/**
 * Whether a kit sends exactly what General MIDI sends.
 *
 * Four of the six do. That is not a fault -- General MIDI is the one
 * layout an instrument can be assumed to understand, and naming the
 * instruments that take it is useful -- but a dropdown with four entries
 * that do the same thing is a dropdown that looks broken when you pick one
 * and nothing changes. So the panel says it out loud instead.
 * @see components/DrumKit.vue
 */
export function sameAsGeneralMidi(kit) {
  if (!kit || !kit.map) return false
  return DRUM_VOICES.every((voice) => kit.map[voice.id] === GENERAL_MIDI[voice.id])
}

export const DRUM_KITS = [
  {
    id: 'gm',
    name: 'General MIDI',
    map: { ...GENERAL_MIDI },
    in: { ...GENERAL_MIDI_IN },
    notes: '',
  },
  {
    id: 'tr8s',
    name: 'Roland TR-8S',
    in: { ...GENERAL_MIDI_IN },
    // Verified against Roland's own MIDI implementation chart: the TR-8S ships
    // with GM numbers for every voice it has, and has no rim, pedal hat or bell
    // -- so those fold onto the drum they belong to rather than being silent.
    map: {
      ...GENERAL_MIDI,
      hatPedal: 42,
      rideBell: 51,
      crash2: 49,
      tomHigh: 50, tomMid: 47, tomFloor: 43,
    },
    notes: 'Factory note numbers, which are the General MIDI ones. Changed under '
         + 'UTILITY ▸ MIDI ▸ Inst Note, so check there if a voice is silent.',
  },
  {
    id: 'ableton',
    name: 'Ableton Drum Rack',
    map: { ...GENERAL_MIDI },
    in: { ...GENERAL_MIDI_IN },
    notes: 'A Drum Rack is whatever its pads were filled with. Live labels each '
         + 'pad with its General MIDI name, so a GM-laid-out kit lands correctly '
         + 'and a hand-built one may not.',
  },
  {
    id: 'addictive2',
    name: 'Addictive Drums 2',
    in: { ...GENERAL_MIDI_IN },
    /*
     * AD2's own layout, read off the keymap XLN ship with the product.
     *
     * Source: `Addictive Drums 2 Keymap.pdf`, dated 2 June 2021, installed
     * at `/Library/Application Support/XLN Audio/Addictive Drums 2/App/
     * ADBV0002/Manuals/`. Not a guess and not a recollection -- every number
     * below is a row of that table.
     *
     * This is the *Addictive Drums* map, which is what AD2 is on unless
     * somebody changed it. If your copy is set to the General MIDI preset
     * in its MIDI Mapping window, choose General MIDI here instead.
     *
     * Two judgements, both of them stated because they are judgements:
     *
     * AD2 has four toms and this vocabulary has three, so high/mid/floor go
     * to Tom 1, Tom 2 and Tom 3 rather than skipping to Tom 4. Contiguous,
     * and an AD2 kit with only three toms loaded still sounds all three.
     *
     * The hats and ride have several articulations each and one of them has
     * to be the plain one: Closed 1 Tip for closed, Open A for open, Pedal
     * Closed for the foot, Ride 1 Tip for the ride. The louder shafts,
     * bells, chokes and double hits are the rows this vocabulary has no
     * word for, which is the honest shape of the difference between AD2 and
     * a fourteen-voice description of a drummer.
     */
    map: {
      kick: 36,
      snare: 38,        // Snare Open Hit
      snareRim: 37,     // Snare Rimshot
      sideStick: 42,    // Snare SideStick
      tomHigh: 71,      // Tom 1 Open Hit
      tomMid: 69,       // Tom 2 Open Hit
      tomFloor: 67,     // Tom 3 Open Hit
      hatClosed: 49,    // HiHat Closed 1 Tip
      hatOpen: 54,      // HiHat Open A
      hatPedal: 48,     // HiHat Pedal Closed
      crash1: 77,       // Cymbal 1 Hit
      crash2: 79,       // Cymbal 2 Hit
      ride: 60,         // Ride 1 Tip
      rideBell: 61,     // Ride 1 Bell

      /*
       * And no percussion, because AD2 has none.
       *
       * Not an omission to be filled in later. AD2 is a drum kit: there is
       * no conga in it, no tambourine, no cowbell, and no note that would
       * make one. The V-Drums kit sends its percussion at General MIDI
       * numbers on the reasoning that an unmapped voice is silent and a
       * standard number is the best guess -- true there, because a TD-11
       * leaves those numbers empty. It is false here. AD2's own layout
       * *occupies* the General MIDI percussion range with real kit pieces:
       * 63 is a ride choke, 56 an open hat, 39 a snare rimshot. Sending a
       * conga to 63 is not a guess that might land, it is a ride choke in
       * the middle of the bar.
       *
       * So they are left unmapped and dropped, which is what a kit with no
       * congas sounds like. It costs: 14.6% of every note in a real
       * collection is General MIDI percussion. The Flexi slots are where
       * somebody who has loaded a tambourine into one points this table at
       * it. @see mapDrumNotes, which leaves behind what the kit cannot play
       */
    },
    /*
     * The same notes as General MIDI, and it says so.
     *
     * This used to tell somebody to go and set AD2's Map Preset to
     * "General MIDI" -- which is jamin instructing a plugin it cannot see
     * about a layout it cannot read, to make true a claim it had already
     * made. Backwards twice over: jamin does not know what is on the other
     * end of the cable, so the most it can honestly do is send the one
     * layout everything understands and let the notes be corrected here.
     *
     * Nothing here is AD2's own layout, which has far more articulations
     * than this vocabulary has voices and is not written down anywhere
     * jamin can read. Correct the notes below against your own copy and
     * save it, and the saved one is what plays. @see components/DrumKit.vue
     */
    /*
     * What the notes are called on the instrument, for the fourteen this
     * vocabulary reaches. Same source as the numbers.
     *
     * The column beside the number exists to catch a mapping that is
     * plausible and wrong -- a rimshot sent to "Electric Snare" is obvious
     * in words and invisible in numbers. Naming them in General MIDI's
     * vocabulary only works while the kit *is* General MIDI: on AD2's own
     * layout note 71 is Tom 1 and General MIDI calls 71 a short whistle,
     * so the column that was there to catch mistakes was inventing them.
     */
    calls: {
      36: 'Kick',
      37: 'Snare Rimshot',
      38: 'Snare Open Hit',
      42: 'Snare SideStick',
      48: 'HiHat Pedal Closed',
      49: 'HiHat Closed 1 Tip',
      54: 'HiHat Open A',
      60: 'Ride 1 Tip',
      61: 'Ride 1 Bell',
      67: 'Tom 3 Open Hit',
      69: 'Tom 2 Open Hit',
      71: 'Tom 1 Open Hit',
      77: 'Cymbal 1 Hit',
      79: 'Cymbal 2 Hit',
    },
    notes: 'AD2\u2019s own layout, read off the keymap XLN ship with it. If your '
         + 'copy is on the General MIDI preset in its MIDI Mapping window, '
         + 'choose General MIDI here instead.',
  },
  {
    id: 'abbeyroad',
    name: 'Abbey Road Drummer',
    map: { ...GENERAL_MIDI },
    in: { ...GENERAL_MIDI_IN },
    notes: 'Its factory map carries articulations these voices have no name '
         + 'for, and is not something jamin can read.',
  },
  {
    /*
     * One layout to point every drum program at.
     *
     * The other way round from the rest of this list. Those try to match an
     * instrument's own numbering, which jamin cannot read and can only be
     * told; this is a layout to *configure an instrument to*, so that one
     * setting works across all of them and nothing has to be guessed again.
     *
     * It is General MIDI with one note moved, and that is not a compromise
     * -- it is the answer. General MIDI already names every one of these
     * forty voices and gives thirty-nine of them their own number; the one
     * exception is the rimshot, which the standard has no instrument for
     * and which therefore folds onto the snare. Move it to 40 and the map
     * is one note per voice with nothing lost.
     *
     * 40 rather than one of the empty numbers above 81, because a layout
     * that is General MIDI everywhere it can be degrades well: point a
     * sampler at this, and a *different* sampler that was never configured
     * still plays every drum correctly bar one -- and that one lands on
     * Electric Snare, which is at least a snare. A rimshot parked at 94
     * would be silence on everything that had not been told.
     *
     * Free in General MIDI and deliberately left free here: 35 acoustic
     * bass drum, 41 low floor tom, 45 low tom, 48 hi-mid tom, 52 Chinese,
     * 55 splash, 59 ride 2. They are room for voices this vocabulary does
     * not have yet, and filling them with nothing to send would be a chart
     * that lies about what jamin can play.
     */
    id: 'maximal',
    name: 'Jamin Maximal',
    in: { ...GENERAL_MIDI_IN },
    map: { ...GENERAL_MIDI, snareRim: 40 },
    notes: 'One note per voice, with nothing folded. General MIDI everywhere '
         + 'it can be, so an instrument that was never told still plays it — '
         + 'only the rimshot moves, to 40. Set your drum programs to this and '
         + 'the same setting works for all of them.',
  },
  {
    id: 'vdrums',
    name: 'Roland V-Drums (TD-11)',
    /*
     * The kit the shipped corpus was played on, so its own table is what reads
     * a note coming in -- but only where it has something to say.
     *
     * A TD-11 has twenty pads and that table has twenty notes in it. Used
     * alone it is not a map of the kit, it is a map of the *kit's pads*, and
     * everything a pattern plays that is not one of those pads reads as
     * nothing: a conga at 63, a kick at 35, a clap at 39. Superior Drummer
     * shelves get classified here on the strength of their hat edges at 22 and
     * 26 -- correctly, that is where those notes live -- and then lost every
     * other note they had, which is a two-bar funk groove of thirty-two hits
     * arriving completely silent.
     *
     * So General MIDI underneath and the TD-11's own pads on top. Where Roland
     * disagrees with the standard, Roland wins -- 58 is a floor tom rim here
     * and a vibraslap there, and a shelf judged to be a Roland kit means the
     * former. Where Roland is silent, the standard answers instead of nobody.
     */
    in: { ...GENERAL_MIDI_IN, ...TD11_TO_VOICE },
    // The kit the corpus was recorded on: playing a groove straight back at one
    // is the one case where nothing should be translated at all.
    map: {
      kick: 36, snare: 38, snareRim: 40, sideStick: 37,
      tomHigh: 48, tomMid: 45, tomFloor: 43,
      hatClosed: 42, hatOpen: 46, hatPedal: 44,
      crash1: 49, crash2: 57, ride: 51, rideBell: 53,

      /*
       * And the percussion, at General MIDI's numbers.
       *
       * A TD-11 has no congas: there is no pad to hit and no Roland number to
       * send. But a voice with no note at all is a drum that silently never
       * sounds, and a conga in an imported library is a real note that has to
       * go somewhere -- so it goes to the number the standard gives it, which
       * is what whatever is listening downstream is most likely to understand.
       * The alternative is dropping it, which is what used to happen.
       */
      clap: 39, tambourine: 54, cowbell: 56, vibraslap: 58,
      bongoHigh: 60, bongoLow: 61,
      congaMute: 62, congaHigh: 63, congaLow: 64,
      timbaleHigh: 65, timbaleLow: 66,
      agogoHigh: 67, agogoLow: 68,
      cabasa: 69, maracas: 70,
      whistleShort: 71, whistleLong: 72,
      guiroShort: 73, guiroLong: 74,
      claves: 75,
      woodBlockHigh: 76, woodBlockLow: 77,
      cuicaMute: 78, cuicaOpen: 79,
      triangleMute: 80, triangleOpen: 81,
    },
    notes: 'Roland\u2019s own pad numbers, which differ from General MIDI on the '
         + 'rimshot and two toms. It has no percussion pads, so those voices go '
         + 'out at their General MIDI numbers rather than being dropped.',
  },
]

/**
 * The distinct ways a note can be read, and which kits share each one.
 *
 * Six kits are offered and five of them read a note identically -- General
 * MIDI, the TR-8S, Ableton's Drum Rack, Addictive Drums 2 and Abbey Road
 * all use `GENERAL_MIDI_IN`, because that is the correct answer for all of
 * them: the samplers ship a General MIDI preset and their own layouts have
 * more articulations than this vocabulary has voices. Only the V-Drums read
 * differently, and only because the shipped corpus was played on one.
 *
 * Offering six names for two behaviours is a dropdown that appears to do
 * something and does not. Worse, it makes the classifier look broken --
 * every library comes back "General MIDI" and it is impossible to tell a
 * correct verdict from a detector that only knows one word.
 *
 * So the choice offered is the reading, and the kits that share it are named
 * on it. Where jamin *sends* notes is a different question with a different
 * answer, and it is asked in the Kit tab.
 */
export function inboundReadings() {
  const seen = new Map()
  for (const kit of DRUM_KITS) {
    const key = JSON.stringify(Object.entries(kit.in).sort())
    if (!seen.has(key)) seen.set(key, [])
    seen.get(key).push(kit)
  }

  return [...seen.values()].map((kits) => {
    const others = kits.slice(1).map((one) => one.name)
    return {
      id: kits[0].id,
      name: kits[0].name,
      also: others,
      label: others.length ? `${kits[0].name} — also ${others.join(', ')}` : kits[0].name,
      notes: kits[0].notes,
      reads: Object.keys(kits[0].in).length,
    }
  })
}

/**
 * What General MIDI calls each percussion note.
 *
 * Shown beside the number in the Kit tab. A mapping that sends the rimshot to
 * "Electric Snare" is obviously wrong the moment the words are on screen and
 * nearly impossible to notice from the numbers -- which is exactly how it
 * shipped. Names make the table proofread itself.
 */
export const GM_PERCUSSION = {
  35: 'Acoustic Bass Drum', 36: 'Bass Drum 1', 37: 'Side Stick', 38: 'Acoustic Snare',
  39: 'Hand Clap', 40: 'Electric Snare', 41: 'Low Floor Tom', 42: 'Closed Hi Hat',
  43: 'High Floor Tom', 44: 'Pedal Hi-Hat', 45: 'Low Tom', 46: 'Open Hi-Hat',
  47: 'Low-Mid Tom', 48: 'Hi Mid Tom', 49: 'Crash Cymbal 1', 50: 'High Tom',
  51: 'Ride Cymbal 1', 52: 'Chinese Cymbal', 53: 'Ride Bell', 54: 'Tambourine',
  55: 'Splash Cymbal', 56: 'Cowbell', 57: 'Crash Cymbal 2', 58: 'Vibraslap',
  59: 'Ride Cymbal 2', 60: 'Hi Bongo', 61: 'Low Bongo', 62: 'Mute Hi Conga',
  63: 'Open Hi Conga', 64: 'Low Conga', 65: 'High Timbale', 66: 'Low Timbale',
  67: 'High Agogo', 68: 'Low Agogo', 69: 'Cabasa', 70: 'Maracas',
  71: 'Short Whistle', 72: 'Long Whistle', 73: 'Short Guiro', 74: 'Long Guiro',
  75: 'Claves', 76: 'Hi Wood Block', 77: 'Low Wood Block', 78: 'Mute Cuica',
  79: 'Open Cuica', 80: 'Mute Triangle', 81: 'Open Triangle',
}

/** What GM calls this note, or a plain number for one it does not name. */
export function gmName(note) {
  return GM_PERCUSSION[note] || (Number.isInteger(note) ? `note ${note}` : '')
}

export const DEFAULT_KIT = 'gm'

/** A kit by id, or General MIDI. Never null: silence is not a useful failure. */
export function kitById(id) {
  return DRUM_KITS.find((kit) => kit.id === id) || DRUM_KITS[0]
}

/**
 * A corpus note, as this kit plays it.
 *
 * Null for a pitch the corpus used and the vocabulary has no voice for, which is
 * a note to drop rather than to guess at -- a wrong drum is louder than a
 * missing one.
 */
export function mapDrumNote(note, map, inbound = TD11_TO_VOICE) {
  const voice = inbound[note]
  if (!voice) return null
  const out = map && map[voice]
  return Number.isInteger(out) && out >= 0 && out <= 127 ? out : null
}

/**
 * A whole groove, once. Notes the kit cannot play are left behind.
 *
 * Each note keeps the voice it came from. Once it is mapped, its number belongs
 * to the kit and says nothing about what was struck -- 38 might be a snare or
 * might be whatever somebody typed into the Kit tab -- so reading it backwards
 * to light up a drum would be reading the wrong end. The voice is the fact; the
 * number is the destination.
 */
export function mapDrumNotes(notes, map, inbound = TD11_TO_VOICE) {
  const out = []
  for (const note of notes || []) {
    const voice = inbound[note.note]
    const pitch = mapDrumNote(note.note, map, inbound)
    if (pitch === null) continue
    out.push({ ...note, note: pitch, voice })
  }
  return out
}

/**
 * Which note map a pile of drum MIDI was written for.
 *
 * Deterministic, and deliberately about the *core kit* rather than the whole
 * histogram. Measured across 760 packs and three quarters of a million files,
 * almost everything is General MIDI where it counts and vendor-specific
 * everywhere else: a sixties drummer library puts its kick on 36, its snare on
 * 38 and its hats on 42 exactly as General MIDI says, and then hangs its own
 * articulations off 92 and 97 where General MIDI has nothing. Judging such a
 * pack by its full range calls it unknown; judging it by its kick and snare
 * calls it what it is.
 *
 * So: where do the kick, the snare and the hi-hats live? That is the question
 * every drum map answers differently and every drum pattern asks constantly.
 *
 *   22 or 26 carrying weight   a Roland V-Drums kit -- General MIDI has no
 *                              percussion at all below 35, so these cannot be
 *                              anything else
 *   the core kit where GM puts it   General MIDI
 *   the weight up in the hand percussion   General MIDI, with no kit in it
 *   none of the above          unknown, and say so rather than guess
 *
 * `coverage` is the number that actually matters to a listener: how much of
 * this pack the chosen map can play at all. A pack can be correctly identified
 * as General MIDI and still lose a third of its notes, because the vocabulary
 * here has fourteen voices and a sampled drum library has ninety.
 */
export function classifyKit(histogram) {
  const entries = Object.entries(histogram || {}).map(([note, n]) => [Number(note), Number(n)])
  const total = entries.reduce((sum, [, n]) => sum + n, 0)
  if (!total) return { kit: DEFAULT_KIT, confidence: 0, coverage: 0, reason: 'nothing to look at' }

  const weight = (test) => entries.reduce((sum, [note, n]) => sum + (test(note) ? n : 0), 0) / total
  const on = (notes) => weight((note) => notes.has(note))

  const hatEdge = on(new Set([22, 26]))
  // The toms are what separates a Roland kit from General MIDI: a TD-11 puts
  // them on 48/45/43 and General MIDI on 50/47/43. The hi-hat edge notes are
  // *not* enough on their own -- measured across the collection, 66,101 files
  // in packs using 22 and 26 put their toms exactly where General MIDI does,
  // and calling those Roland would move every tom on every one of them.
  const rolandToms = on(new Set([48, 45]))
  const gmToms = on(new Set([50, 47]))
  const vdrums = hatEdge >= 0.02 && rolandToms > gmToms ? hatEdge : 0

  const kick = on(new Set([35, 36]))
  const snare = on(new Set([37, 38, 40]))
  const hats = on(new Set([42, 44, 46]))
  const percussion = weight((note) => note >= 60 && note <= 81)
  const core = kick + snare + hats

  // What this kit can make sense of on the way *in*. Not the notes it sends --
  // that is the other direction and answers a different question.
  const playable = (kit) => {
    const understood = kitById(kit).in || GENERAL_MIDI_IN
    return weight((note) => Boolean(understood[note]))
  }

  /**
   * A verdict a kit cannot actually play is not a verdict -- but neither is no
   * verdict at all.
   *
   * Two faults, one after the other. First, every pack was called General MIDI
   * whatever was in it: measured across the collection, `Africa`, `Asia` and
   * `Europe` put **100% of their notes outside anything General MIDI can
   * read** -- hand-percussion packs living between 60 and 81, where a
   * vocabulary of fourteen kit voices has no room at all -- and each was
   * named, filed and silent on playback.
   *
   * The fix for that was to refuse: below a threshold, name nothing and let the
   * library follow whatever the Kit tab said. Which is worse, because it is not
   * an answer. Which numbering a library's files are written in is a **fact
   * about those files**; it does not change when somebody picks a different
   * drum instrument for the track, and a library whose map follows a global
   * setting is one whose notes move under it for reasons that have nothing to
   * do with the library.
   *
   * So there is always a kit. When the obvious one cannot read the notes, every
   * kit is tried and the one that reads the most wins -- and when they are all
   * poor, the best of a bad set is still named and `coverage` says plainly how
   * bad. A wrong answer you can see and change beats a deferral you cannot.
   */
  const ENOUGH = 0.6

  /** Whichever kit makes sense of the most of this, when the likely one does not. */
  const bestOfAll = () => DRUM_KITS
    .map((kit) => ({ id: kit.id, covered: playable(kit.id) }))
    .sort((a, b) => b.covered - a.covered)[0] || { id: DEFAULT_KIT, covered: 0 }

  const verdict = (kit, confidence, reason) => {
    const covered = playable(kit)
    if (covered >= ENOUGH) return { kit, confidence, coverage: covered, reason }

    const best = bestOfAll()
    return {
      kit: best.id,
      confidence: 0,
      coverage: best.covered,
      reason: `${Math.round((1 - covered) * 100)}% of these notes are outside `
        + `${kitById(kit).name}; ${kitById(best.id).name} reads the most of them `
        + `(${Math.round(best.covered * 100)}%)`,
    }
  }

  if (vdrums >= 0.02) {
    return verdict('vdrums', Math.min(1, vdrums * 10),
                   'hi-hat edge notes and toms where a Roland kit puts them')
  }

  if (core >= 0.30) {
    return verdict('gm', Math.min(1, core / 0.5),
                   'kick, snare and hats are where General MIDI puts them')
  }

  if (percussion >= 0.40) {
    return verdict('gm', Math.min(1, percussion), 'hand percussion, in the General MIDI range')
  }

  // Nothing is where a standard kit would be, which is still not a reason to
  // leave the library without a map. @see verdict
  const best = bestOfAll()
  return { kit: best.id, confidence: 0, coverage: best.covered,
           reason: `nothing here is where a standard kit would be; ${kitById(best.id).name} `
             + `reads ${Math.round(best.covered * 100)}% of it` }
}

/** A custom map, sanitised: known voices, whole numbers, in range. */
export function cleanKitMap(map) {
  const out = {}
  for (const [voice, note] of Object.entries(map || {})) {
    if (!VOICE_IDS.has(voice)) continue
    const pitch = Math.round(Number(note))
    if (Number.isFinite(pitch) && pitch >= 0 && pitch <= 127) out[voice] = pitch
  }
  return out
}

/**
 * The commonest verdict per shelf, and the library's own.
 *
 * A pack is rarely of one mind: a Superior Drummer download has the kit
 * grooves in one shelf and the hand percussion in another, and those are
 * genuinely written in different numbering. So each shelf is judged on its own
 * notes and the library takes whichever verdict the most shelves reached; a
 * shelf that agrees with the library is dropped from the table, because the
 * table exists to record disagreement.
 *
 * Lives here rather than beside the import that calls it so that a checker
 * driving a real library gets the same answer the program does. @see
 * scripts/play_check.py, which had to reimplement this once and got it wrong.
 */
export function classifyFolders(perFolder) {
  const folderKits = {}
  const tally = new Map()
  const reasons = []
  for (const [shelf, hist] of perFolder) {
    const verdict = classifyKit(hist)
    folderKits[shelf] = verdict.kit
    tally.set(verdict.kit, (tally.get(verdict.kit) || 0) + 1)
    if (!verdict.kit && verdict.reason && !reasons.includes(verdict.reason)) {
      reasons.push(verdict.reason)
    }
  }

  const known = [...tally.entries()].filter(([kit]) => kit)
  const majority = known.sort((a, b) => b[1] - a[1])[0]
  const setKit = majority ? majority[0] : ''

  for (const shelf of Object.keys(folderKits)) {
    if (!folderKits[shelf] || folderKits[shelf] === setKit) delete folderKits[shelf]
  }

  return { folderKits, setKit, reason: setKit ? '' : reasons[0] || '' }
}

/**
 * A library's own reading of its notes, sanitised.
 *
 * The mirror of `cleanKitMap`, pointing the other way: that one says which
 * note a voice comes out on, this one says which voice a note read *in* means.
 * Notes arrive as object keys and are therefore strings; they leave as
 * strings too, because that is what an object key is, and every lookup
 * against them is by note number, which coerces.
 */
export function cleanInMap(map) {
  const out = {}
  for (const [note, voice] of Object.entries(map || {})) {
    const pitch = Math.round(Number(note))
    if (!Number.isFinite(pitch) || pitch < 0 || pitch > 127) continue
    if (!VOICE_IDS.has(voice)) continue
    out[pitch] = voice
  }
  return out
}

/**
 * Instrument words, as a library might spell them in a folder name.
 *
 * Used to *suggest* -- never to decide. A sampled library hangs its own
 * articulations off note numbers nobody standardised, and there is no table
 * for them anywhere; but a folder of two hundred patterns that is 90% note 63
 * and is called `48@TIMBALES / 05@SALSA` has been labelled by the people who
 * made it, and that is evidence rather than a guess.
 *
 * Deliberately conservative about which voice a word maps to. A folder called
 * `CONGAS` says congas and does not say which of the three -- muted, high,
 * low -- so it offers the open one and leaves the correction to somebody who
 * can hear it. Where a library distinguishes them it usually says so, and the
 * longer words are tried first for exactly that reason.
 */
export const VOICE_WORDS = [
  ['sidestick', 'sideStick'], ['side stick', 'sideStick'], ['cross stick', 'sideStick'],
  ['rimshot', 'snareRim'], ['snare rim', 'snareRim'],
  ['hihat', 'hatClosed'], ['hi hat', 'hatClosed'], ['hat closed', 'hatClosed'],
  ['closed hat', 'hatClosed'], ['hats closed', 'hatClosed'],
  ['hat open', 'hatOpen'], ['open hat', 'hatOpen'], ['hats open', 'hatOpen'],
  ['hat pedal', 'hatPedal'], ['pedal hat', 'hatPedal'], ['foot hat', 'hatPedal'],
  ['ride bell', 'rideBell'], ['bell', 'rideBell'],
  ['floor tom', 'tomFloor'], ['rack tom', 'tomHigh'],
  ['kick', 'kick'], ['bass drum', 'kick'],
  ['snare', 'snare'], ['ride', 'ride'], ['crash', 'crash1'], ['splash', 'crash1'],
  ['china', 'crash2'], ['tom', 'tomMid'],

  ['tambourine', 'tambourine'], ['tamborine', 'tambourine'],
  ['cowbell', 'cowbell'], ['vibraslap', 'vibraslap'],
  ['bongo', 'bongoHigh'], ['conga', 'congaHigh'], ['tumba', 'congaLow'],
  ['timbale', 'timbaleHigh'], ['agogo', 'agogoHigh'],
  ['cabasa', 'cabasa'], ['afuche', 'cabasa'], ['shekere', 'cabasa'],
  ['shaker', 'cabasa'], ['caxixi', 'cabasa'],
  ['maraca', 'maracas'], ['whistle', 'whistleLong'], ['apito', 'whistleLong'],
  ['guiro', 'guiroLong'], ['clave', 'claves'], ['castanet', 'claves'],
  ['woodblock', 'woodBlockHigh'], ['wood block', 'woodBlockHigh'],
  ['cuica', 'cuicaOpen'], ['triangle', 'triangleOpen'],
  ['clap', 'clap'], ['handclap', 'clap'],
  ['surdo', 'tomFloor'], ['cajon', 'kick'], ['udu', 'kick'], ['frame drum', 'tomFloor'],
].sort((a, b) => b[0].length - a[0].length)

/**
 * What a library's own folder names say about the notes no map can read.
 *
 * `perFolder` is what the import already has in hand: every shelf, and how
 * many times each note was struck on it. For each note with no voice this
 * works out where in the library it lives, and -- where the place has an
 * instrument's name on it and the note is most of what is played there --
 * what that place says it is.
 *
 * Two things come back and they are not the same kind of thing. `hints` is a
 * suggestion -- `{ voice, share, against }`, where `share` is how much of the
 * evidence agreed and `against` how many other instruments were named -- to
 * be offered in the interface for somebody to accept or overrule. `where` is
 * not a suggestion at all: it is the three places each note is most used,
 * which is the evidence a person needs to decide when no word was found, and
 * no word is found more often than not, because the commonest way a library
 * names a folder is after the groove rather than after the drum.
 */
export function learnInbound(perFolder, inbound = GENERAL_MIDI_IN) {
  const votes = new Map()          // note -> Map(voice -> weight)
  const places = new Map()         // note -> [{ shelf, hits, share }]

  for (const [shelf, hist] of perFolder) {
    const counts = Object.entries(hist || {})
    const total = counts.reduce((sum, [, n]) => sum + n, 0)
    if (!total) continue

    const words = String(shelf).toLowerCase().replace(/[^a-z]+/g, ' ')
    const found = VOICE_WORDS.find(([word]) => words.includes(word))

    for (const [note, hits] of counts) {
      const pitch = Number(note)
      if (inbound[pitch]) continue

      const share = hits / total
      const seen = places.get(pitch) || []
      seen.push({ shelf, hits, share })
      places.set(pitch, seen)

      // Most of what a folder plays, or the folder's name is about the
      // groove and not about this note. Half is the bar because a percussion
      // folder is usually one instrument and a kit folder never is.
      if (!found || share < 0.5) continue
      const slate = votes.get(pitch) || new Map()
      slate.set(found[1], (slate.get(found[1]) || 0) + hits)
      votes.set(pitch, slate)
    }
  }

  /*
   * And only where the library agrees with itself.
   *
   * Note 24 on the Superior Drummer download is struck forty-five thousand
   * times across eleven thousand shelves; some of those shelves are called
   * `HATS_OPEN_VARIATIONS` and some are called `Snare Roughs`, and the
   * majority is not the truth, it is the majority. A suggestion made out of
   * a contested vote is a wrong drum offered with the same confidence as a
   * right one, so a contested vote makes no suggestion at all -- and the
   * three folders the note is most played in are reported either way, which
   * is what somebody who knows the library can actually use.
   */
  const hints = {}
  for (const [note, slate] of votes) {
    const ranked = [...slate.entries()].sort((a, b) => b[1] - a[1])
    const all = ranked.reduce((sum, [, weight]) => sum + weight, 0)
    const [voice, weight] = ranked[0]
    const share = all ? weight / all : 0
    if (share < 0.6) continue
    hints[note] = { voice, share: Math.round(share * 100), against: ranked.length - 1 }
  }

  const where = {}
  for (const [note, seen] of places) {
    where[note] = seen.sort((a, b) => b.hits - a.hits).slice(0, 3)
      .map((one) => ({ shelf: one.shelf, share: Math.round(one.share * 100) }))
  }

  return { hints, where }
}
