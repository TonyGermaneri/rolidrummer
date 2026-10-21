# Roli Drummer

An AU/VST3 MPE drum instrument built around the Roli Seaboard. Each held key enables one drum part from a shared, DAW-synchronized MIDI pattern. Sliding on one key selects that key's pattern zone independently of the others.

The audio engine and controls are adapted from SSB; the force graph and data grid are adapted from jamin. The Seaboard is interactive SVG artwork with 49 sculpted piano keywaves, raised accidentals, three faders and an XY pad. [Open the vector preview](docs/seaboard-preview.svg); regenerate it with `node tools/export-seaboard.mjs`. See [THIRD_PARTY.md](THIRD_PARTY.md) for provenance and licenses, and [IMPLEMENTATION.md](IMPLEMENTATION.md) for implementation evidence and validation limits.

## Build

Requires Node.js, CMake 3.22+, a C++20 toolchain, and Xcode on macOS. Install libarchive headers with `brew install libarchive`; macOS builds link Apple's universal system library. Other native platforms require libarchive and SQLite development packages. JUCE 8.0.15 is fetched by CMake unless an existing source directory is supplied.

```sh
npm ci
npm run build
python3 native/tools/fetch-neural-assets.py
cmake -S native -B native/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build native/build -j 4
ctest --test-dir native/build --output-on-failure
npm test
```

For an existing JUCE checkout, add `-DFETCHCONTENT_SOURCE_DIR_JUCE=/absolute/path/to/JUCE` to configuration. The AU, VST3 and app appear in `native/build/plugin/RoliDrummer_artefacts/Debug`. The `_web` build targets copy the current `dist` into each bundle and verify its ad-hoc signature.

The pinned asset fetch verifies MiniLM and universal ONNX Runtime downloads. Native bundles include the model, tokenizer, runtime and license notices; no Python process runs in the instrument. Neural indexing and queries require macOS 13.4 or later because of the bundled runtime. Resolved note data plays without loading that runtime. Core-only builds (`-DRD_BUILD_PLUGIN=OFF`) need no neural assets; `-DRD_REQUIRE_NEURAL_ASSETS=OFF` is an explicit development option for an importer/player build without inference.

The web preview (`npm run dev`) supports interface development. Filesystem browsing, MIDI and audio run in the native app or plug-in. `RD_WEB_DIR=/absolute/path/to/dist` overrides the bundled editor for local development; `RD_DATA_DIR` redirects shared settings and the asset cache, especially useful for isolated tests. `RD_NEURAL_DIR` selects another copy of the same pinned assets.

A universal macOS build and local development archives:

```sh
cmake -S native -B native/build-universal -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' -DBUILD_TESTING=OFF
cmake --build native/build-universal -j 4
native/tools/package-macos.sh
```

The packaging script verifies signatures and both CPU slices before creating archives under `release`, then checks clean ZIP extraction, bundle metadata, executable permissions and macOS/Finder package recognition. GitHub Actions builds native artifacts on `main` and publishes signed, notarized macOS releases on matching `v*` version tags. The editor preview deploys separately to GitHub Pages. Credentials and manual signing/notarization are described in [SIGNING.md](native/packaging/SIGNING.md).

Release bundles use the stable distribution names `RoliDrummer.component`, `RoliDrummer.vst3` and `RoliDrummer.app`; archive names append `-v<version>.zip`, matching SSB's convention. Tagged releases publish the ZIPs produced after notarization without re-archiving them. Their internal executable names and plug-in identities stay unchanged.

## Play and edit

The **Circuit kit** contains 16 synth drum voices bound consecutively in chromatic pitch order, from MIDI 48 through 63, including sharps and flats. The first six positions remain kick, closed hat, snare and high/mid/low toms; the next ten are clap, rim, open hat, pedal hat, crash, ride, cowbell, shaker, high conga and low conga. They reuse the four existing generated waveforms with individual tuning, envelopes, filters and effects. Hat voices share a choke group. MPE pitch ranges preserve each voice's tuning.

All other MIDI pitches repeat that ordered bank as **parallel keys**, wrapping at both edges. Parallel keys stay visually unbound and reuse their origin’s sound, patterns, filters, expression mappings and per-key latch/ownership rules. Playing one lights both the physical key and its origin; **TRACK** selects the origin’s editor. Physical pitches remain distinct for note-off and per-note pressure, including MPE channel reuse. Existing patches and sound kits migrate their ordered bindings to the chromatic layout once, preserving the musical settings and neural metadata.

On the on-screen Seaboard, black keys use their own shorter height for slide/zone gestures: the top is 100% and their bottom is 0%. Initial touches below a black key belong to the neighboring white key according to horizontal position. White keys use only their physical key body for the zone matrix, keeping E/F and B/C independently playable.

Both ribbons act **per keywave**. The top ribbon behaves like touching that keywave's highest matrix zone, following Normal, Hold or Toggle mode. The bottom ribbon stops only the touched keywave, using **Latch Quantize** and its bar count, or Instant/bypass. Every white and black key has a lane in both ribbons. Below a black key's short body is white-key territory; the black key's stop lane reappears only in the bottom ribbon. A sweep encounters individual chromatic lanes in order, including intermediate lanes crossed between pointer/MIDI updates. Multiple keywaves can remain independently toggled on, with one zone/pattern per origin. Re-entering the same active zone toggles only that key off; a different zone replaces only its current pattern. Parallel keys retain their shared-origin rules. Green/red pending indicators show the affected keywave. Individual on-screen ribbon lanes also support Enter and Space.

Hardware decoding uses absolute per-note CC74: 0–15 is the stop ribbon, 116–127 the top ribbon, and the body between them is scaled for white and black keys. Continuous ribbon sweeps use note pitch plus Glide, with the Seaboard's default ±48-semitone range and member-channel RPN 0 pitch sensitivity when supplied. A swept touch retains its original note-off identity while monitoring and Track follow the current lane. Pre-note and post-release CC74 resets do not invoke ribbon actions. Use absolute Slide with full Slide travel ([ROLI Slide behavior](https://support.roli.com/en/support/solutions/articles/36000025050-slide-absolute-vs-relative), [Glide and pitch range](https://support.roli.com/en/support/solutions/articles/36000019142-what-midi-messages-do-seaboards-and-lightpads-transmit-)).

The sampler’s **Play / Stop** button auditions the selected sound with its current tuning and effects, including while the DAW is stopped. EQ, delay and reverb start expanded. The Seaboard and its controls scale together to fit the available width and height.

The **Keywave** tab splits its scope between the sound waveform and a piano roll of that keywave’s current or last pattern, with a playback marker. Zone rows show miniature versions. These previews apply the key’s MIDI drum-note filter and follow the actual gesture-selected variation. Shorter patterns repeat across the longer displayed phrase, with loop boundaries and a repeat count; their playback remains on their own loop length. **TRACK** follows the last keywave touched on the controller; turn it off to keep the editor on its selected keywave. Track starts off and is an editor preference, separate from the patch.

The Keywave tab also has a right-hand **Sounds** browser with Grid and Graph views. Preview a sound, select it and choose **Assign to**, double-click it, or drag it onto a keywave. Its semantic search finds a sound for the selected keywave. Sound folders chosen here or in Settings are included in Kit matching. Grid and Graph follow their container dimensions, including tab switches and initially collapsed panels; Graph also offers **Fit** to recover an offscreen layout. Scheduled drum hits flash their source keywaves on the Seaboard, independently of the touch and toggle indicators.

The **Kit** tab saves and loads sound-only kits. A kit stores each keywave’s sound and sound controls; applying it leaves patterns, drum-note filters, expression routes, keyboard mode and bus controls intact. Enter a description and choose **Auto bind** to match all keywaves, or target only the selected keywave. Matching uses the existing pretrained text embedding model against sound names and paths, with drum-part gates and a persistent local vector cache. Saved kits are shared between instances. A saved sound on a previously unassigned keywave is restored with no pattern or drum filter attached. Semantic Auto Bind immediately auditions the proposed bindings in a temporary **Preview**, with each keywave’s matched sound listed. Play the keywaves, then choose **Apply Kit** to keep the sounds or **Cancel** to restore the previous sounds. Editing the instrument or leaving the search also cancels the preview. Previewing does not change the saved patch, host session or Undo history. Apply creates one undoable edit; apply a preview before saving it as a kit. Browsing and matching do not train a model or generate audio.

The **Piano Roll** tab monitors the drum notes triggered by all keywaves. Notes grow from the fixed playhead as the history scrolls left, with a MIDI keyboard on the left and colors based on the source keywave. Choose a one-, two-, four- or eight-bar view, scroll vertically to other pitches, or clear the display. Monitoring follows scheduled note durations and releases; it does not change playback or record into the patch.

Choose **Settings → Defaults → Restore factory defaults** to load the complete kit into an existing instance (this replaces the current patch; Undo restores it). The **Settings → Sounds** browser's built-in library also offers all 16 tuned presets for preview and individual dragging. Pocket, Syncopated, and Fill & ghosts are two-bar variations of the same 4/4 backbeat. Extra percussion sounds only when its own key is held. The single JSON factory definition is shared by native initialization and the editor.

**Latch quantize** is enabled by default, with a one-bar interval. In Hold and Toggle, queued latch/unlatch changes take effect at the next chosen bar boundary. Choose Instant or switch the control off to apply changes immediately. Queued changes show a dashed keywave outline plus a green play indicator for a pending start or a red stop indicator for a pending stop. Indicators appear immediately when queued, survive short taps, and clear at the boundary or on cancellation. Parallel keys show the indicator on their origin too. A different-zone press replaces the old owner at the boundary; double presses can cancel a pending toggle. Pedal-up clears released keys on the same grid, while Panic clears active and queued notes immediately. Normal key mode keeps its usual press/release behavior. The bar length follows the resolved patch’s meter, including odd meters.

1. Load the instrument on a MIDI track and enable the controller's MPE input. The DAW transport can be stopped: held keys play their parts using its current reported tempo, or the last valid tempo if the host omits it (120 BPM before any tempo is known). The standalone app uses the patch tempo.
2. In Settings → Sounds, choose a folder and browse using Grid or Graph. Use ▶ to preview a sound, then drag it onto a Seaboard key. Pattern previews use your current kit and work with the DAW stopped. Stop the preview with ■, or leave the browser tab.
3. In Patterns, enter a musical description in the default **Auto** mode. Change the seed or click the die to find another deterministic groove; all drum keys and slide regions are assigned automatically. The dropdown beside the search defaults to **Patterns Only**. Choose **1 Generative** through **4 Generative** to add that many generated tracks across the kit; seed rerolls keep the selected generation setting. **Save** stores the current search and its actual notes as a new patch. Turn Auto off for manual search, roles, zone assignment, phrase length and generation controls. To browse loose MIDI files, open Settings → Pattern libraries → Browse MIDI files and patch patterns, then drag one into a zone. Multitrack files offer a track/channel chooser; channel 10 is suggested when present. The selected part’s own track length and note timing are preserved; a longer unrelated or conductor track does not pad its loop. Historical catalog identities remain valid when a cached part’s playable loop is corrected. Split or remove zones using their toolbar (1–8 zones per key). Resize zone boundaries to change the slide ranges. The lower zone editor, hidden during Auto matching, is a template; Apply zones to all keys copies it to each assigned key.
4. Hold a key to enable its part at the next launch boundary. Slide independently on each key to change its pattern. The shared pattern clock keeps all held parts in phase. While the DAW plays it follows song position; while stopped it advances locally, and it resynchronizes when the DAW starts. Releasing keys prevents new hits while existing tails follow the patch release policy. Launch and zone-change quantization still apply.
5. Select a key to edit its SSB sound, Drum part, expression routes and zones. Drum part filters the drum notes inside the MIDI pattern; Edit note filter exposes custom note numbers, independently of the Seaboard key pitch. Choose Bind a surface and click a supported knob to make a mapping. The controls at the left of the Seaboard open global CC learn/mapping settings and follow incoming CCs and host automation. The Bend knob sets Glide’s pitch range when Glide has no other mapping; assigning Glide to cutoff, reverse or another destination replaces that pitch behavior. Output gain is separate from the kit level saved in a patch.
6. Browse saved patches in the Patches tab using the searchable grid and force graph. Settings contains pattern imports and worker status, sound management, playback, MIDI, appearance, storage statistics and factory reset. Click LOAD, double-click a patch, or press Enter to load it. Name and save patches in the compact SSB panel, or Export portable to include their sound files. DAW session state also embeds assigned samples and restores without opening the editor. Missing assets appear in the sound panel with Locate sound.

Shared browser settings live in a revisioned SQLite database under the system application-data directory, in `WaveContour/RoliDrummer`. Patches and kit state belong to each instrument instance; graph/grid browsing state communicates between instances. The patch browser remembers its own search, view, grid layout, graph positions and camera independently of Sounds and Patterns. Sample files are copied into a content-addressed cache when assigned.

Settings → Pattern libraries imports ZIP archives, including ZIP64 collections, or folders without extracting a loose copy of every MIDI file. Imports run on a separate native worker and continue when the editor closes. Pause and Resume preserve completed batches; a new source revision becomes searchable only when its import finishes. The catalog lives in `library.sqlite`, separately from shared browser settings. Results group equivalent mapped performances, keep their source aliases, and provide preview, paging and assignment to a selected key zone. Files and patch patterns remain available in the MIDI browser in the same Settings category.

Source Map chooses inferred, General MIDI, Roland V-Drums or custom note mappings. Note overrides can be restricted to a relative folder prefix. Unknown articulations remain unmapped, and the Mapped column reports recognized source notes rather than how many instruments the current kit contains. Import Report lists unsupported entries. Reimport with corrected mappings to build a new revision; assigned note data remains part of saved patches.

The headless `drummer-library` target uses the same importer and catalog as the instrument:

```sh
cmake --build native/build --target drummer-library -j 4
native/build/drummer-library import /absolute/path/library.zip --db /private/tmp/rd-library/library.sqlite
native/build/drummer-library browse --db /private/tmp/rd-library/library.sqlite --query 'hard rock' --role fill
native/build/drummer-library status --db /private/tmp/rd-library/library.sqlite
```

Imports also build a resumable semantic index on the library worker. Semantic similarity embeds the patch query with the pinned MiniLM model and searches normalized description vectors on Metal, with canonical CPU rescoring and a CPU fallback. A versioned, checksummed snapshot keeps those immutable vectors together for faster process startup. Indexing prepares it off audio; missing or damaged snapshots rebuild from the source rows. Disposable snapshots retain at most the two newest completed revisions plus the currently requested revision; older pinned searches still work through their source rows. The background worker also prepares compact catalog indexes for candidate lookups. It can pause/resume preparation, keeps the previous index searchable, and coordinates pending work between instances. Initialized catalogs open through reads while preparation holds the writer. Description filter retains literal browsing. Refresh search includes new imports while existing patches retain their resolved notes.

With **Auto** turned off, enter a name such as **Heavy Rock**, search, then choose **Resolve patch** to assign sparse, steady and fill slide regions to the kit's unlocked keys. Matching uses compatible meter/feel, phrase length and the activity of each key's actual drum part. Choose a phrase cycle to exclude incompatible song-length material. Where a part has no compatible fill, high slide retains its steady groove. Fill-only parts have silent low/middle regions; parts without any compatible material stay silent. Part fallbacks explains these choices. Editing or dropping a pattern into a resolved key locks its assignments; the zone toolbar's Locked/Auto button controls subsequent resolution. The query, index/model versions, region choices, mappings and actual selected MIDI notes are retained in the patch and DAW state.

Part presence, phrase cycle and feel are checked before sampling candidates, with activity measured for the key's own drum filter. This keeps a sparse percussion part eligible even when the rest of its source kit is dense. Resolver version 12 keeps seeded Auto selection while requiring an explicit generation count; saved patches continue to recall their stored notes.

Each region prepares up to 16 pattern candidates. Press selects activity and accents, Glide navigates variations ordered in a fitted rhythm space (with seeded navigation in Auto), and Strike establishes initial intensity. Gesture Variations exposes the weights, smoothing and minimum dwell. Slide keeps its sparse/steady/fill ordering. The scheduler evaluates active clips and keeps outstanding note endings independently of runtime changes.

Resolving while notes are held prepares the new patterns for the next bar of the resolved meter. The library panel shows when they are waiting. Releasing all notes and the sustain pedal applies them immediately; ordinary sound edits retain the waiting boundary. An explicit patch load, undo, MIDI program selection or DAW recall replaces that pending resolution. Saved documents contain the prepared notes, so recall is independent of a fresh search.

**Pattern families** opens a review dialog and keeps a musical query's membership separate from saved patches. Imports update the matched family in the background. Review ambiguous matches with Include, exclude unwanted patterns, or return a manual decision to Auto. The manual view retains exclusions so they can be changed later. Select a result in the main library grid to include it in the chosen family even when its name falls outside the query's closest matches. Resolve family prepares its latest confirmed patterns; existing saved notes retain their pinned membership snapshot. Renaming a family changes its display name and preserves musical identity. Saving a searched groove with **SAVE** registers its query as a family; searching alone never does. Matching is incremental: a new family or decision recomputes only the families it affects, and while matching catches up, resolution uses each family's last confirmed membership. The panel's summary says what it is waiting for (matching, a failure, a pause or a search refresh) and offers **MATCH NOW** when nothing is running. **Remove** forgets a family and its decisions; **Remove search families** clears the families that searches registered in earlier versions and that were never saved, renamed, edited or accepted from a suggestion.

**Name suggestions** groups sufficiently supported, coherent material that existing families cover poorly. Inspect supporting paths and overlap, edit a proposed name before accepting it, choose a compatible destination before merging, or dismiss it. Accepting a family creates a query association; saving a patch assigns a MIDI program if a slot is available. Suggestions do not rearrange existing program positions. Text-affinity and cohesion values are similarity measurements, not probabilities or a guarantee of musical relevance.

**Generated variations** optionally adds up to four seeded variations per region, sharing the same sixteen-candidate limit with originals. Compatible parents exchange beat fragments while preserving source timing, dynamics, downbeat/backbeat anchors and the final beat of fills. Generated notes stay within the key's drum mask and within declared density bounds; duplicate or incompatible results are rejected. With fourteen compatible distinct fills and a request for two generated variations, all fourteen originals remain available alongside the two new fills. The actual notes, source identities, seed and generator version are saved, so recall does not regenerate them. Some groups cannot produce the requested number under these constraints. Manual generation is off by default. Auto defaults to Patterns Only; its explicit 1–4 generation choices set a kit-wide budget, while the seed controls selection and key order; rerolls replace the previous Auto pool while keeping unrelated patch clips and template references. Automatic patch-family suggestions and the remaining delivery gates are tracked in [NEURAL_PLAN.md](NEURAL_PLAN.md).

Headless indexing and retrieval use the same native path:

```sh
native/build/drummer-library index --db /private/tmp/rd-library/library.sqlite
native/build/drummer-library neural-status --db /private/tmp/rd-library/library.sqlite
native/build/drummer-library search --db /private/tmp/rd-library/library.sqlite --query 'Heavy Rock' --role fill
```

Each key supports up to 16 expression mappings including the global mappings. When several mappings target the same parameter, the last mapping wins; global mappings are applied after per-key mappings. Lift shapes release tails; Strike, Glide, Slide or Press can select playing zones.

Click the ROLI DRUMMER project tile to open the SSB theme menu, with grouped swatches, hover previews and previous/next buttons. Only selecting a theme saves it; closing the menu restores the committed theme. The read-only PATCH display and arrows browse the factory kit and saved patches; name or rename patches only in the Patches tab. Save patch updates the same stable identity when renamed. Save a copy creates a separate identity and program assignment. Patch navigation can be undone.

The header’s **BUS FX** section controls the main mix: reverb, delay, chorus, low-pass cutoff and resonance. Click **BUS FX ▾** to bind each dial to Fader 1–3 or Pad X/Y, or edit delay sync/time/feedback, reverb size/decay, and chorus rate/depth. A blue indicator marks a bound dial; turning that dial returns it to manual control. Settings and bindings are saved with patches and DAW sessions. Existing patches start dry with an open filter. Global CC assignments/learning stay under the Seaboard’s ⚙ menu; its existing sound mappings remain separate from bus effects. In RISE MIDI mode, the faders send CCs; in Expression mode they change the controller’s touch response ([ROLI’s fader guide](https://support.roli.com/en/support/solutions/articles/36000025175-the-touch-faders-and-xy-touchpad)).

The RISE preset arrows send **MIDI Program Change**, with the hardware retaining its own 0–127 position. In MIDI mode these are program numbers, not CC increment/decrement buttons ([ROLI’s manual](https://www.fullcompass.com/common/files/25681-ROLISeaboardRISEUserManual.pdf#page=7), [preset-switch guidance](https://support.roli.com/en/support/solutions/articles/36000025105-equator-no-preset-at-message)). On the MPE master channel (channel 1 by default), program numbers select persistent slots from 0 through 127. Existing occupied positions migrate once from the former factory/name-key order. Renaming a saved patch or adding an earlier name preserves its slot; a copy receives a new identity and a free slot. The header shows the current slot as P000–P127, and the Patches tab reports its number. Missing identities keep their slots reserved. If all positions are occupied, another patch can still be saved and loaded in the browser without evicting an assignment. Unassigned program numbers leave the current patch playing and show a status message; the former out-of-range wrapping behavior is removed. Browser arrows still wrap through the visible patch list independently of MIDI slots. The first hardware message selects its absolute position, which can differ from the currently selected on-screen patch. The selector tooltip reports the last received program number. Sample loading happens off the audio thread and works with the editor closed; rapid messages keep the newest selection. The on-screen device arrows also browse patches. A host must deliver Program Change to the instrument; some VST3 hosts filter it. Octave/NRPN messages do not select patches.

The SSB theme menu changes the sound panel, browser, waveforms, zones, editor colors and the native window background/resize grip. A 12-pixel resize border and 30-pixel corner grip remain outside the web controls; narrow editors reflow controls and scroll the upper section. Host-owned title bars retain the host’s appearance. Theme selection is shared between instances. The selected Seaboard key has a fill highlight. Each physical finger has its own bright glowing circle, following raw Slide and pitch across the key bodies and ribbons, with pressure controlling marker size. Physical markers remain visible when a command is replaced, toggled off or queued, including when multiple fingers share a parallel origin. Command echoes are separate outlined rings; connecting traces and emitted pulses show which finger created a playback or queued latch command. On-screen touches appear immediately at their pointer coordinates. Sample playback markers and granular traces use native voice telemetry. The status bar reports the latest CC74 value and channel.

The main keyboard has **Normal**, **Hold** and **Toggle** modes. Normal releases a part when you lift the key unless the sustain pedal is down. Hold keeps released parts playing, like CC64 sustain. Toggle latches each key independently: press the same zone again to stop that key, or press a different zone to replace its previous zone. Purple key backgrounds and markers identify toggled keys. A key has one active owner and one zone in every mode, including when the Seaboard allocates a different MPE channel to the next press; old fingers cannot change or release its replacement.

In Hold and Toggle, pedal-up clears released latches and leaves physically held keys playing. It works even if the pedal was not previously down. The selected mode stays armed for subsequent releases. Changing modes clears software-held released keys; a physically depressed sustain pedal still holds its own notes. Modes are saved in patches and DAW sessions; live latched notes are not restored. Older patches open in Normal.

The sustain pedal defaults to CC64 (64–127 down, 0–63 up). Lifted keys keep their patterns and last per-key expression until pedal release. The physical finger circle disappears on lift; a dashed command echo stays at the captured position until sustain ends. Fingers still touching the keyboard remain visible and continue playing. MPE master-channel sustain holds all parts, while a member-channel pedal holds only that channel. Use the Seaboard's ⚙ Global MIDI bindings → Sustain pedal CC or Learn pedal to reassign it. Assignments are saved in patches/DAW state; CC74 and channel-mode messages are reserved. Panic clears sustained notes. Reassignment releases latched notes so they cannot get stuck.

Key name, drum part and playback mode sit beside the waveform; NOTES opens the MIDI drum-note filter. Expression is integrated into the sampler beneath the main sound controls. Click a surface chip to cycle Strike/Glide/Slide/Press/Lift, choose its target, and turn FROM/TO/SLEW dials to set the range and smoothing. Drag the miniature curve canvas to change response; double-click resets to linear, and arrow keys adjust it (Shift for fine steps). Expression and Key Zones fold independently. BIND cycles to a surface, then clicking a sound knob creates its mapping. The header, patch/transport displays, output dial, tabs, theme selector and Seaboard tools share the SSB panel styling.

The sampler incorporates SSB 0.6.2's sound engine (4be313f). GRAIN offers CLASSIC overlapping grains, LOOP segments with crossfades and reverse probability, and CLOUD layers with 2–20 grains per side. Controls include skew, variation, stereo detuning, FM and scanning with loop/key tracking. The engine also supports SSB's stereo/ratio/quantized LFOs, expanded modulation sources and destinations, nine stereo state-variable filters and volume/envelope/filter key scaling. New numeric sound controls support per-contact MPE binding; CLOUD density is captured when a note starts. STRETCH remains separate. This engine update leaves factory sounds, patterns and patch levels unchanged; older settings default to CLASSIC with the additions neutral. Existing legacy grain bindings retain their conversions. In particular, legacy `grainReverse` bindings still reverse the whole sample; `grainLoopReverse` binds the new LOOP reverse probability.

New sounds start with Strike → volume, Glide → pitch, Slide → cutoff, Press → reverb mix, and Lift → release. Auto map restores these five mappings. Saved custom mappings are retained. Unmodified legacy factory patterns are upgraded to two-bar grooves with snare backbeats on 2 and 4, syncopated kicks, accented hats, ghost notes and tom fills; edited MIDI patterns are preserved. Zone switches remain quantized by the Zone changes setting and do not require lifting the finger.

The default release policy lets existing drum/reverb tails finish. Enable Stop voices on release in Patches for a tighter cutoff. Launch and zone-change quantization are configurable there as well.

Per-contact delay and reverb use preallocated buffers. Reverb size/decay can therefore follow expression continuously without loading a new impulse response. There are 32 effect slots for the 16 active MPE channels and released tails; when those are full, the oldest released effect tail is replaced. Replaced tails cannot reclaim slots. Extra sustained owners can exceed this pool; their dry voices remain subject to the 64-voice limit while unavailable effect sends are omitted. The drummer skips unused shared convolution preparation; SSB’s original fixed effects remain available to its inherited engine paths.

The SVG keys support Space/Enter auditioning and the XY pad supports arrow keys. Zone boundaries support arrows (Shift for larger steps). Leaving or closing the editor sends note-offs for its on-screen contacts without silencing a hardware controller. Hold, Toggle and the sustain pedal can keep those released parts latched.

To measure full-polyphony DSP cost in a release build:

```sh
cmake --build native/build-universal --target drummer-benchmark -j 4
native/build-universal/plugin/drummer-benchmark
```

The CSV reports callback wall time relative to audio duration, p99/max callback time and the process’s cumulative peak resident memory. It sustains 64 voices across 16 contacts; sample, effects, stereo grain, and live grain-window/FM/scan profiles run at 48/96 kHz. Run it without other builds/tests for useful timing. These measurements describe the local machine and workload, not a guarantee for every host or grain setting.

For combined pattern-intent, sustain, channel-reuse and musical-refresh load:

```sh
cmake --build native/build-universal --target drummer-intent-performance -j 4
RD_DATA_DIR=/private/tmp/rd-intent-benchmark arch -arm64 native/build-universal/plugin/drummer-intent-performance --benchmark
```

Each instance reaches 128 distinct keys: fifteen physical MPE members, 113 sustained owners and 64 voices. The CSV includes architecture/translation status, startup/steady/drain deadline misses, whole-run timing, sampled resident memory and prepared refresh counts. Add an instance count to `--benchmark`, or use `--soak SECONDS INSTANCES BLOCK RATE`:

```sh
RD_DATA_DIR=/private/tmp/rd-intent-benchmark arch -arm64 native/build-universal/plugin/drummer-intent-performance --benchmark 2
RD_DATA_DIR=/private/tmp/rd-intent-soak arch -arm64 native/build-universal/plugin/drummer-intent-performance --soak 60 2 256 96000
```

Multiple instances are processed serially as one host lane for deadline measurements. Correctness mode separately renders independent instances concurrently while editing/clearing one, and checks unaffected audio. It also compares audio across 64-, 257- and 2,048-frame blocks with effects enabled and disabled, under C++ allocation/deletion guards. On the local machine the current native single-instance matrix had no whole-run misses. Two native instances with effects at 96 kHz/64 frames missed 578 of 90,000 callbacks over sixty seconds; the corresponding 256-frame workload met all 22,500 deadlines. These are measured workload limits, not host-wide guarantees. `native/tools/verify-neural-bundles.py` checks the built universal products and is required by macOS packaging.

The longer transport correctness mode varies tempo, stops/resumes the host, seeks backwards and loops while two instances remain fully held. It checks continued audio with the host stopped, retained contacts, all prepared refresh adoptions, final drain and every sample across 64-, 257- and 2,048-frame partitions. Ninety-second arm64 dry/effects and translated x86_64 effects runs are sample-identical within each architecture. Drum voice endings are sample-accurate; their delay/reverb tails remain independent.

```sh
RD_DATA_DIR=/private/tmp/rd-transport-soak arch -arm64 native/build-universal/plugin/drummer-intent-performance --transport-soak 90 2
```

Add a capture directory after the instance count to save interleaved float32 diagnostic audio; append `dry` after that directory for the dry profile. This mode tests correctness and does not establish a host deadline budget.

The separate query benchmark measures a fresh-process call, repeated calls, a shared second instance and concurrent shared instances. It records query-cache state, result hashes, model/matrix sharing, resource preparation phases and process RSS. It can populate query/family caches, so use a development catalog copy for controlled trials:

```sh
cmake --build native/build-universal --target drummer-neural-benchmark -j 4
arch -arm64 native/build-universal/plugin/drummer-neural-benchmark /private/tmp/rd-library/library.sqlite 'Heavy Rock' search 3 2 gpu /private/tmp/rd-search.json
arch -arm64 native/build-universal/plugin/drummer-neural-benchmark /private/tmp/rd-library/library.sqlite 'Heavy Rock' resolve 3 2 gpu /private/tmp/rd-resolve.json
```

Run timed workloads without builds or other benchmarks. A fresh process does not imply a cold filesystem cache. The current three native full-catalog queries measured 2.26–4.48 seconds for a first search and 50–204 ms for serial warm searches. Batched part-conditioned resolver-10 resolve/compile calls measured 2.38–5.04 seconds first. Warm Heavy Rock measured 613–626 ms, Bossa Nova 327–334 ms and unregistered Afro Cuban Conga 1,509–1,534 ms. All repeated/shared/concurrent results agree and retain the previous result hashes. Resolver phase timings remain outside saved patches; prepared catalog reads reuse committed features and one consistent snapshot.

The read-only vector-storage experiment compares float32 with float16 and per-row int8 decoding, reports neighbor fidelity and tests canonical shortlist rescoring. On the complete catalog, reduced precision changed ranking; float32 remains canonical. This development tool measures vector fidelity and encoded payload size, with the same float32 CPU search kernel after decoding:

```sh
cmake --build native/build-universal --target drummer-vector-storage -j 4
arch -arm64 native/build-universal/drummer-vector-storage /private/tmp/rd-library/library.sqlite native/drummer/tests/data/retrieval-queries.json /private/tmp/rd-vector-storage.json
```


## Source map and runtime ownership

- `src/components/Seaboard.vue` owns vector geometry, key hit targets and on-screen contacts. `SoundPanel.vue` and `Knob.vue` adapt the SSB panel; `AssetBrowser.vue`, `DataGrid.vue` and `src/canvas` adapt jamin browsing. `App.vue` assembles the four tabs and the always-visible performance surface.
- `src/store.ts` owns the editable patch, undo/redo and revision acknowledgements. `src/native/bridge.ts` transports commands and telemetry through JUCE's WebView bridge. File import and sample loading stay in the native worker; `dist` contains only the editor.
- `native/drummer/src/State.cpp` compiles patch JSON into an immutable runtime. It deduplicates the content of bound patterns and retains unbound library entries only in the document. `Scheduler.cpp` evaluates each distinct clip timeline once per block against host PPQ, then routes matching drum notes into held contacts. Each contact owns its zone and quantized gate; changing one contact does not restart the bus.
- `native/engine` adapts SSB sample/granular voices, envelopes, filters and effects. Voice ownership carries MPE expression and lift through release tails. `ContactEffects.h` keeps drum delay/reverb independent between contacts.
- `PluginProcessor.cpp` adopts prepared runtimes at block boundaries. Superseded runtimes and sounds retire through bounded queues and are collected outside audio rendering. `PluginEditor.cpp` serializes slow commands through a worker and rejects stale document replacements after host recall.
- `SharedStore.cpp` uses SQLite WAL and conditional revisions for browser state across processes. Graph camera, node positions and open nodes form one layout revision. Editors poll changes and defer remote layout adoption during local gestures; they never flush a stale whole-layout snapshot on unload. `Assets.cpp` handles cached samples and portable archives separately from browser settings.

CC74 always updates only the contact on its incoming MIDI channel, including any pre-note value for that channel. It is never broadcast to other channels or used as a global CC macro; global CC learn excludes 74. Two notes with the same pitch on different channels retain independent slide and voice ownership.

The instrument receives conventional lower-zone MPE: channel 1 is the master and channels 2–16 carry independent notes. Its scheduler has 16 contact slots; the on-screen keyboard allocates available member channels and avoids channels currently used by hardware. Pattern parts match MIDI note numbers, so hat, kick and tom filters are independent of the physical key's pitch.

The macOS AU host tool loads the exact development factory with process-local registration, avoiding an older installed component. First run `drummer-neural-render` on a private catalog to produce a resolved patch and `processor-state.bin`, then pass that state to the host:

```sh
cmake --build native/build-universal --target drummer-au-host -j 4
arch -arm64 native/build-universal/plugin/drummer-au-host 'native/build-universal/plugin/RoliDrummer_artefacts/Release/AU/Roli Drummer.component' /private/tmp/rd-render/processor-state.bin /private/tmp/rd-au-host.json /private/tmp/rd-au-host-state
```

Repeat with `arch -x86_64` to exercise the Intel slice under Rosetta. The tool verifies the loaded image, state/parameter round trips, five-dimension MIDI rendering at three rate/buffer settings, note drain and clean lifecycle. It does not install the AU or invoke Apple's validator.

## Current validation limits

The grain benchmark results and validation details are recorded in `IMPLEMENTATION.md`; raw local measurements are in `native/build/validation/granulator-performance.csv`.

All six neural phases are implemented. Seventeen native suites pass; frontend files retain their 112-test and production-build checkpoint. Three full-catalog queries produce identical resolver-10 documents across CPU/GPU and both architecture slices. Combined intent/contact/effects and ninety-second transport tests cover independent parts, capacity, sustain/reuse, refresh, C++ callback heap guards, exact audio across buffer partitions and concurrent instance isolation. Real standalone fixtures cover import/pause/resume, search, family resolution/growth, naming/manual decisions, zone locks, stable programs and exact portable recall.

Current universal resources/signatures, headless VST3 strictness 5 and exact-bundle AU state/MIDI/render/lifecycle checks pass both slices. The earlier Apple `auval` run remains inconclusive because its process failed to exit; the direct AU host result makes no Apple validator claim. Physical-controller feel and subjective musical quality are unmeasured. Ambiguity, outside-query inclusion and held refresh have native/component coverage without separate final live-editor recordings. Feedback and hardware sign-off are optional follow-up, not completion requirements. The original installation and app data remain preserved; local development archives are not published. See [NEURAL_PLAN.md](NEURAL_PLAN.md) and [IMPLEMENTATION.md](IMPLEMENTATION.md) for evidence and measured operating limits.
