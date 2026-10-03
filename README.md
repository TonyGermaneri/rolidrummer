# Roli Drummer

An AU/VST3 MPE drum instrument built around the Roli Seaboard. Each held key enables one drum part from a shared, DAW-synchronized MIDI pattern. Sliding on one key selects that key's pattern zone independently of the others.

The audio engine and controls are adapted from SSB; the force graph and data grid are adapted from jamin. The Seaboard is interactive SVG artwork with 49 sculpted piano keywaves, raised accidentals, three faders and an XY pad. [Open the vector preview](docs/seaboard-preview.svg); regenerate it with `node tools/export-seaboard.mjs`. See [THIRD_PARTY.md](THIRD_PARTY.md) for provenance and licenses, and [IMPLEMENTATION.md](IMPLEMENTATION.md) for implementation evidence and validation limits.

## Build

Requires Node.js, CMake 3.22+, a C++20 toolchain, and Xcode on macOS. JUCE 8.0.15 is fetched by CMake unless an existing source directory is supplied.

```sh
npm ci
npm run build
cmake -S native -B native/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build native/build -j 4
ctest --test-dir native/build --output-on-failure
npm test
```

For an existing JUCE checkout, add `-DFETCHCONTENT_SOURCE_DIR_JUCE=/absolute/path/to/JUCE` to configuration. The AU, VST3 and app appear in `native/build/plugin/RoliDrummer_artefacts/Debug`. The `_web` build targets copy the current `dist` into each bundle and verify its ad-hoc signature.

The web preview (`npm run dev`) supports interface development. Filesystem browsing, MIDI and audio run in the native app or plug-in. `RD_WEB_DIR=/absolute/path/to/dist` overrides the bundled editor for local development; `RD_DATA_DIR` redirects shared settings and the asset cache, especially useful for isolated tests.

A universal macOS build and local development archives:

```sh
cmake -S native -B native/build-universal -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' -DBUILD_TESTING=OFF
cmake --build native/build-universal -j 4
native/tools/package-macos.sh
```

The packaging script verifies signatures and both CPU slices before creating archives under `release`. GitHub Actions builds native artifacts on `main` and publishes signed, notarized macOS releases on matching `v*` version tags. The editor preview deploys separately to GitHub Pages. Credentials and manual signing/notarization are described in [SIGNING.md](native/packaging/SIGNING.md).

## Play and edit

The **Circuit kit** contains 16 synth drum voices on natural keys only, from MIDI 48 (C) through 74 (D): 48, 50, 52, 53, 55, 57, 59, 60, 62, 64, 65, 67, 69, 71, 72, 74. The first six positions remain kick, closed hat, snare and high/mid/low toms; the next ten are clap, rim, open hat, pedal hat, crash, ride, cowbell, shaker, high conga and low conga. They reuse the four existing generated waveforms with individual tuning, envelopes, filters and effects. Hat voices share a choke group. MPE pitch ranges preserve each voice's tuning.

The sampler’s **Play / Stop** button auditions the selected sound with its current tuning and effects, including while the DAW is stopped. EQ, delay and reverb start expanded. The Seaboard and its controls scale together to fit the available width and height.

Choose **Patches → Factory kit** to load the complete kit into an existing instance (this replaces the current patch; Undo restores it). The Sounds browser's built-in library also offers all 16 tuned presets for preview and individual dragging. Pocket, Syncopated, and Fill & ghosts are two-bar variations of the same 4/4 backbeat. Extra percussion sounds only when its own key is held. The single JSON factory definition is shared by native initialization and the editor.

1. Load the instrument on a MIDI track and enable the controller's MPE input. The DAW transport can be stopped: held keys play their parts using its current reported tempo, or the last valid tempo if the host omits it (120 BPM before any tempo is known). The standalone app uses the patch tempo.
2. In Sounds, choose a folder and browse using Grid or Graph. Use ▶ to preview a sound, then drag it onto a Seaboard key. Pattern previews use your current kit and work with the DAW stopped. Stop the preview with ■, or leave the browser tab.
3. In Patterns, browse MIDI files and drag one into a zone. Multitrack files offer a track/channel chooser; channel 10 is suggested when present. The file’s complete loop length and note timing are preserved. Split or remove zones using their toolbar (1–8 zones per key). Resize zone boundaries to change the slide ranges. The lower zone editor is a template; Apply zones to all keys copies it to each assigned key.
4. Hold a key to enable its part at the next launch boundary. Slide independently on each key to change its pattern. The shared pattern clock keeps all held parts in phase. While the DAW plays it follows song position; while stopped it advances locally, and it resynchronizes when the DAW starts. Releasing keys prevents new hits while existing tails follow the patch release policy. Launch and zone-change quantization still apply.
5. Select a key to edit its SSB sound, Drum part, expression routes and zones. Drum part filters the drum notes inside the MIDI pattern; Edit note filter exposes custom note numbers, independently of the Seaboard key pitch. Choose Bind a surface and click a supported knob to make a mapping. The controls at the left of the Seaboard open global CC learn/mapping settings and follow incoming CCs and host automation. The Bend knob sets Glide’s pitch range when Glide has no other mapping; assigning Glide to cutoff, reverse or another destination replaces that pitch behavior. Output gain is separate from the kit level saved in a patch.
6. Save a patch in Patches, or Export portable to include its sound files. DAW session state also embeds assigned samples and restores without opening the editor. Missing assets appear in the sound panel with Locate sound.

Shared browser settings live in a revisioned SQLite database under the system application-data directory, in `WaveContour/RoliDrummer`. Patches and kit state belong to each instrument instance; graph/grid browsing state communicates between instances. Sample files are copied into a content-addressed cache when assigned.

Each key supports up to 16 expression mappings including the global mappings. When several mappings target the same parameter, the last mapping wins; global mappings are applied after per-key mappings. Lift shapes release tails; Strike, Glide, Slide or Press can select playing zones.

The SSB theme menu changes the sound panel, browser, waveforms, zones, editor colors and the native window background/resize grip. A 12-pixel resize border and 30-pixel corner grip remain outside the web controls; narrow editors reflow controls and scroll the upper section. Host-owned title bars retain the host’s appearance. Theme selection is shared between instances. The selected Seaboard key has a fill highlight; live finger markers follow CC74 slide and pitch, with pressure controlling marker size. Sample playback markers and granular traces use native voice telemetry. The status bar reports the latest CC74 value and channel.

New sounds start with Strike → volume, Glide → pitch, Slide → cutoff, Press → reverb mix, and Lift → release. Auto map restores these five mappings. Saved custom mappings are retained. Unmodified legacy factory patterns are upgraded to two-bar grooves with snare backbeats on 2 and 4, syncopated kicks, accented hats, ghost notes and tom fills; edited MIDI patterns are preserved. Zone switches remain quantized by the Zone changes setting and do not require lifting the finger.

The default release policy lets existing drum/reverb tails finish. Enable Stop voices on release in Patches for a tighter cutoff. Launch and zone-change quantization are configurable there as well.

Per-contact delay and reverb use preallocated buffers. Reverb size/decay can therefore follow expression continuously without loading a new impulse response. There are 32 effect slots for the 16 active MPE channels and released tails; when those are full, the oldest released effect tail is replaced. SSB’s original fixed effects remain available to its inherited engine paths.

The SVG keys support Space/Enter auditioning and the XY pad supports arrow keys. Zone boundaries support arrows (Shift for larger steps). Leaving or closing the editor releases its on-screen contacts without silencing a hardware controller.

To measure full-polyphony DSP cost in a release build:

```sh
cmake --build native/build-universal --target drummer-benchmark -j 4
native/build-universal/plugin/drummer-benchmark
```

The CSV reports callback wall time relative to audio duration, p99/max callback time and the process’s cumulative peak resident memory. It sustains 64 voices across 16 contacts; sample, effects and four-stream granular profiles run at 48/96 kHz. Run it without other builds/tests for useful timing. These measurements describe the local machine and workload, not a guarantee for every host or grain setting.


## Source map and runtime ownership

- `src/components/Seaboard.vue` owns vector geometry, key hit targets and on-screen contacts. `SoundPanel.vue` and `Knob.vue` adapt the SSB panel; `AssetBrowser.vue`, `DataGrid.vue` and `src/canvas` adapt jamin browsing. `App.vue` assembles the four tabs and the always-visible performance surface.
- `src/store.ts` owns the editable patch, undo/redo and revision acknowledgements. `src/native/bridge.ts` transports commands and telemetry through JUCE's WebView bridge. File import and sample loading stay in the native worker; `dist` contains only the editor.
- `native/drummer/src/State.cpp` compiles patch JSON into an immutable runtime. It deduplicates the content of bound patterns and retains unbound library entries only in the document. `Scheduler.cpp` evaluates each distinct clip timeline once per block against host PPQ, then routes matching drum notes into held contacts. Each contact owns its zone and quantized gate; changing one contact does not restart the bus.
- `native/engine` adapts SSB sample/granular voices, envelopes, filters and effects. Voice ownership carries MPE expression and lift through release tails. `ContactEffects.h` keeps drum delay/reverb independent between contacts.
- `PluginProcessor.cpp` adopts prepared runtimes at block boundaries. Superseded runtimes and sounds retire through bounded queues and are collected outside audio rendering. `PluginEditor.cpp` serializes slow commands through a worker and rejects stale document replacements after host recall.
- `SharedStore.cpp` uses SQLite WAL and conditional revisions for browser state across processes. Graph camera, node positions and open nodes form one layout revision. Editors poll changes and defer remote layout adoption during local gestures; they never flush a stale whole-layout snapshot on unload. `Assets.cpp` handles cached samples and portable archives separately from browser settings.

CC74 always updates only the contact on its incoming MIDI channel, including any pre-note value for that channel. It is never broadcast to other channels or used as a global CC macro; global CC learn excludes 74. Two notes with the same pitch on different channels retain independent slide and voice ownership.

The instrument receives conventional lower-zone MPE: channel 1 is the master and channels 2–16 carry independent notes. Its scheduler has 16 contact slots; the on-screen keyboard allocates available member channels and avoids channels currently used by hardware. Pattern parts match MIDI note numbers, so hat, kick and tom filters are independent of the physical key's pitch.

## Current validation limits

The October 2 release benchmark sustained 64 voices across 16 contacts. The four-stream granular/effects profile used about 29–30% of audio duration at 48 kHz and 61% at 96 kHz. At 96 kHz/64 samples, its p99 callback was 0.762 ms versus a 0.667 ms deadline; at 256 samples, p99 was 2.088 ms versus 2.667 ms. This is a development-machine measurement, not a dropout-free guarantee. The process's cumulative peak RSS reached 321 MiB across all profiles; that is not a per-instance allocation measurement. Raw results are in `native/build/validation/benchmark-current.csv`.

Native tests and host validators cover audio, automation, state and editor lifecycle. The editor has 42 tests, including real Vue component handlers run with an in-memory renderer for zone resizing, pattern drops, MIDI selection, relinking, keyboard auditioning and shared browser state. Browser canvas/grid rendering, physical Seaboard input and a full native-window/dialog walkthrough have not been verified in this final pass because the native UI automation service cannot start. This is a documented QA limitation, not a build or implementation blocker. See the implementation ledger for the scope of each check.
