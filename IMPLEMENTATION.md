# Implementation and verification ledger

Objective: execute all six phases of the Roli Drummer instrument plan. The implementation is delivered; the table below distinguishes automated evidence from live checks that were not performed.

On October 2, the user clarified that unavailable computer-use automation is not a project blocker. Verification therefore continues through native tests, actual Vue component handlers rendered in memory, source review, SVG rendering and host validators. These tests do not claim to be a manual hardware or native-window walkthrough.

- [x] 1. AU/VST3/standalone foundation, native sound, session recall without editor.
- [x] 2. Precise MIDI import, deduplicated clip bus, host position, quantized independent part gates.
- [x] 3. Contact ownership, per-key zones, all five MPE dimensions, global CC learn/mappings.
- [x] 4. Four tabs, SSB sound controls, persistent Seaboard SVG, directory graph/grid, drag/drop, resizing, undo.
- [x] 5. Native shared revisioned persistence, multi-process conflicts, patches, portable export, missing assets.
- [x] 6. Native/component tests, AU/VST3 host validation, lifecycle/offline/automation/stress tests, development packaging.

Working defaults: natural release tails, 1/16 launch and zone switching, Slide selects zones. The lower zone editor is an apply-to-all template. Quantization, release policy, mappings and zone sources are configurable.

## Requirement audit

| Requirement | Implementation and evidence | Validation limits |
| --- | --- | --- |
| AU/VST3 sound independent of the editor | JUCE processor, native SSB engine, integration recall/audio tests; universal AU/VST3 pass pluginval strictness 5 and AU passes auval | Local development builds are signed ad hoc; no public release/notarization |
| Piano-like ROLI SVG and five left controls | `Seaboard.vue`, 49 shaped hit targets, sculpted natural/accidental paths, faders and XY; exported `docs/seaboard-preview.svg` was rasterized and inspected | Latest entire editor has not had a manual native-window visual pass |
| Sound/pattern OS directory browsing in jamin graph and grid | `AssetBrowser.vue`, actual jamin graph and canvas-datagrid, native directory API; component tests exercise navigation and stale responses | Graph/grid drawing and native file-picker interactions are not end-to-end automated |
| Shared graph positions/camera and grid state without unload races | SQLite WAL conditional revisions, SharedView tests, three-process native conflict test, two mounted browser components exercising camera/position propagation, deferred remote changes and stale gestures | Component test uses deterministic graph-renderer and persistence doubles; it does not claim two visible host windows |
| Sound drops assign keys; pattern drops assign zones | `drag.ts`, key/zone HTML drop handlers, import regression tests and component zone-drop test | Real mouse drag gestures across canvas/WebKit have not had a fresh walkthrough |
| Independent part gates and resizable zones | Native scheduler tests; real ZoneEditor handlers tested for pointer and keyboard resizing, split/remove and assignment; apply-to-all clones template zones | Tests generate handler events in memory |
| One bus per distinct bound pattern, aligned to DAW PPQ | Bound-content deduplication tests, scheduler seek/loop/stop/tempo tests and full-processor block-invariance renders | No known native timing gap |
| All five MPE dimensions and five global CC mappings | Expression audio suite; master-channel CC learn/telemetry tests; Seaboard keyboard/member-channel isolation and XY parameter tests; source review of knob bindings | No physical Seaboard hardware session was run |
| SSB sound panel and generation | Adapted panel/engine, 42 knob-equivalence comparisons plus bend/choke checks, live grain/filter/envelope/effect controls | Full-polyphony resource limits documented below |
| Patches, DAW recall, portable sounds and relinking | Native state/cache/archive tests, fresh-process recovery after deleting originals, editor revision/undo tests, actual MIDI chooser and relink component contracts; native file-dialog dispatch reviewed | Save/open OS dialogs were not clicked in this final pass |
| Universal deliverables | AU/VST3/standalone builds, signature/architecture/ZIP/checksum checks, embedded production editor | Development distribution only |

## Automated evidence

- `npm test`: 40 tests in nine files. Includes existing knob/zone math, stale shared revisions, queued writes, no unload snapshots, patch acknowledgement ordering, undo during a save, host recall protection and factory/track pattern assignment.
- New component tests mount the real Vue render functions on `src/testing/componentHost.ts`, an in-memory renderer. They exercise zone pointer/keyboard handlers and drops; 49 Seaboard key targets; keyboard note-on/off, hardware channel avoidance, unmount cleanup and XY controls; MIDI track/channel selection and cancel; missing-sound relinking; directory input/navigation races; and two browser components sharing graph arrangements. No browser/OS automation is used by these tests.
- `npm run build`: TypeScript and production editor build pass. Vite reports a bundle-size advisory; no build error.
- All five native suites pass: scheduler, inherited SSB core/engine, integration and expression. The latest DSP reciprocal-window assertion was rebuilt and the engine suite rerun successfully. The October 2 hardware-feedback update also changes native scheduling, telemetry, factory data and preview rendering; all five suites were rerun successfully.
- Integration covers idle silence, held-key sound, editor-free DAW recall, exact MIDI PPQ/trailing silence, explicit track/channel choices, MIDI CC persistence across blocks and recall, host automation override and concurrent settings visibility. Three simultaneous child processes compete for a layout revision; exactly one commits.
- Separate child processes restore from embedded DAW assets and portable `.rolidrummer` archives after the source WAV and cache were deleted. Both render audio. Decoded samples are shared across control edits; one missing sound leaves other keys playable.
- Scheduler tests cover seeks, loops, stop/restart, negative PPQ, block-boundary rounding, short notes, member-channel resets and dense-event overflow. Only bound clips compile into buses; equal bound content shares a bus.
- Expression tests compare 42 mapped controls with equivalent knob settings, all five contact/global sources, Lift tails, Glide routing, independent contacts and buffer-invariant smoothing/effects sweeps. Mixed effect output is compared with the sum of separate contacts. Live choke/grain stream/drift/size/rate changes have audible assertions.
- C++ allocation/deallocation guards cover 20,000 current-kit triggers without collection, 12,000 unique retiring sounds with collector starvation/recovery and 128 effect-contact turnovers. These cover C++ heap operations, not every allocator or lock in JUCE/host code.
- Full-processor offline rendering covers 15 contacts, expression/CC traffic, tempo changes, backwards seeking and stop/resume. Audio agrees across 64-, 257- and 2048-sample blocks.
- AU and VST3 pluginval strictness 5 cover editor open/close and editor during audio, state, automation and bus tests. Validation logs are under `native/build/validation`. The AU wrapper reports current program -1 as a warning; checks pass. Standalone and both plug-in bundles contain arm64/x86_64 slices.

## Final editor fixes

- Factory patterns assign directly without being sent to the MIDI file importer.
- Typed directory paths remain separate from loaded paths and persistence keys. Boot/load generations reject stale results. Failed navigation now ends the local edit and accepts remote navigation again.
- A folder dialog opened in Sounds cannot change Patterns after a tab switch. A late Locate sound result cannot overwrite a different selected key or a sound replaced while the dialog was open.
- On-screen notes initialize pitch and pressure and avoid occupied hardware member channels. Closing/blurring the editor releases only its own contacts.
- Lift can shape release tails; playing zone sources are Strike, Glide, Slide and Press. Old Lift-zone documents migrate to Slide.
- The editor enforces the combined 16-route-per-key limit. Global routes apply after per-key routes; the last route targeting a parameter wins.

## Runtime and resource behavior

SSB delay/reverb for drum voices use 32 preallocated contact slots with continuous parameter changes. When full, the oldest released effect tail is replaced, preserving active contacts. Source ownership retires through bounded queues; audio rendering waits for adoption capacity rather than freeing retired assets on the audio thread.

The release benchmark sustains 64 voices across 16 contacts at 48/96 kHz and blocks of 64/256 samples. The four-stream granular/effects profile averages 29–30% of audio duration at 48 kHz and about 61% at 96 kHz. At 96 kHz/64 samples, p99 is 0.762 ms against a 0.667 ms deadline; this stress profile is not consistently within deadline. At 256 samples, p99 is 2.088 ms against 2.667 ms. Cumulative process peak RSS reached 321 MiB across all profiles, not per-instance memory. Raw measurements: `native/build/validation/benchmark-current.csv`.

Prepared grain-window lookup error is checked below 2e-7 against the original cosine equation, and cached pan coefficients are compared with the original mono/stereo laws. The benchmark is a local workload measurement, not a guarantee for all hosts or granular settings.

## Handoff

- Source/runtime ownership, usage, build commands and benchmark commands: `README.md`.
- Pure vector preview: `docs/seaboard-preview.svg`; regenerate with `node tools/export-seaboard.mjs`.
- Development AU/VST3/app archives: `release`, with `SHA256SUMS`, license/provenance and signing instructions.
- Native automation still reports `Sky Computer Use native pipe startup failed`. Screen Recording permission was confirmed by the user. This tool failure is a known validation limitation, not a required user permission or a project blocker.
- A live hardware/native-window walkthrough remains useful follow-up QA for visual layout, canvas drag gestures and OS dialogs. It is not represented as having passed.


## October 2 hardware-feedback update

- Live default MPE Lower Zone is retained: master channel 1, member channels 2–16. Regression tests cover initial and held CC74 values on every member channel, upward/downward zone changes without retriggering, independent fingers, master CC74 isolation (corrected in the follow-up below) and physical contacts on unassigned keys.
- Factory grooves now vary every assigned drum part at 4/8/16 hits per bar. The old factory data varied hats only and omitted toms, masking otherwise working slide changes. Only unchanged legacy factory clips are migrated.
- Finger positions and pressure, latest CC74/channel, selected-key fill, native sample playheads and grain traces are visible in the editor. Actual Vue tests verify moving and released contacts, pressure radius, selected fill and reverse playhead movement. The exported SVG was rasterized and inspected.
- Sound and pattern previews are available in the canvas-grid Listen column and graph buttons. A lazily prepared, separate native engine renders previews without starting the DAW or borrowing MPE contacts. Worker request generations reject canceled results; closing the editor stops auditioning. Native tests cover stopped-host audio, stop, canceled work, preserved kit/contact state and sample-rate changes. Frontend tests cover toggle, known pattern routing and cancellation during MIDI selection.
- All SSB themes are connected to the editor, graph/grid, sound panel and related zone palette. New/default sounds receive five MPE routes. Drum part replaces Pattern notes; custom MIDI filters remain available behind an explanation.
- Validation: 40 editor tests, TypeScript/production build, five native suites, universal arm64/x86_64 AU/VST3/standalone builds, pluginval strictness 5 for AU and VST3, and auval pass. Existing AU current-program warning remains. Full physical Seaboard-in-Live verification still belongs to the hardware session; the new diagnostics make its input observable.
- Updated AU installed at `~/Library/Audio/Plug-Ins/Components/Roli Drummer.component`. Prior copy preserved at `/var/folders/rp/q6_vxvrx7xj8mvlvc87wq9s40000gp/T/rolidrummer-previous-au-llzs1n4f/Roli Drummer.component`. Restart Live to load the new binary.


## Circuit kit and musical factory grooves

- A shared `src/data/factoryKit.json` now supplies both native initialization (embedded with JUCE BinaryData) and frontend factory creation. This prevents differences between a fresh AU and the Factory kit button.
- Three two-bar 4/4 grooves replace the uniform density tests: Pocket, Syncopated, and Fill & ghosts. Strong snares fall on 2 and 4, kicks have independent syncopation, hats carry accents/openings, quieter snares provide ghost notes, and descending toms supply fills. Optional percussion remains gated by its own held key.
- The full 16-voice Circuit kit uses the existing kick/hat/snare/tom waveform sources, with distinct pitch, filter, envelope, pan and effect settings. Tuned toms reuse the same decoded sample. All five MPE routes are retained with pitch centered on each voice's tuning. Closed/open/pedal hats share a choke group. Short key labels avoid overlap.
- The built-in Sounds library offers all 16 tuned presets plus four raw sources. Preset audition retains sound settings; dragging copies settings, part filters and expression ranges while preserving destination zones. Existing custom kits are not replaced. Unmodified patterns from both previous factory revisions migrate to the new grooves; edited patterns remain unchanged. Load Patches → Factory kit for the complete kit in an existing session.
- Validation: 42 frontend tests and production build pass. Native integration renders all 16 voices, checks audible finite output and descending tom pitches, and asserts two-bar backbeats and events for every part. The other four native suites also pass. The render check caught a rim envelope longer than its short tuned clip; the preset release is now short enough to sound correctly. Universal AU/VST3/standalone rebuilt and packaged. Installed AU passed pluginval strictness 5 and auval; its binary and web assets match the build. Log: `native/build/validation/circuit-kit-au.txt`.
- Previous AU backup: `/var/folders/rp/q6_vxvrx7xj8mvlvc87wq9s40000gp/T/rolidrummer-previous-au-jf8rydbj/Roli Drummer.component`.


## Per-channel CC74 correction

Removed an incorrect master-channel fallback that copied CC74 into every member contact. CC74 now follows the receiving channel's current note/voice owner only, retains pre-note values on that channel only, and never emits a global-controller event. Global MIDI learn also excludes CC74. Other master-channel controllers retain their existing behavior.

A new scheduler regression failed against the previous implementation with “Member slide affected another finger.” It covers all 16 receiving channels, opposite initial slide values, same-pitch notes on different channels, held changes, master-channel isolation and note/channel reuse. Processor integration additionally checks independent key-zone and slide telemetry and prevents CC74 from driving an existing global macro binding. Existing shared-clip timing and block-invariant rendering remain covered. All 42 editor tests, the production editor build and the targeted native scheduler/integration suites pass.

The corrected universal AU was installed and verified byte-for-byte against the build. Pluginval strictness 5 and auval passed (`native/build/validation/cc74-isolation-au.txt`). All three release archives were refreshed and their embedded editors verified. The previous AU is preserved at `/var/folders/rp/q6_vxvrx7xj8mvlvc87wq9s40000gp/T/rolidrummer-previous-au-ms1qnukb/Roli Drummer.component`.


## Seaboard fit, natural-key defaults, and sampler audition

- The performance area now allocates a bounded height. A ResizeObserver fits the entire Seaboard to both available dimensions while retaining its aspect ratio; sliders and the XY pad share that fitted coordinate space. Removed conflicting old sizing rules. Zones stretch within the same parent instead of imposing a minimum height beyond it.
- All 16 factory voices now occupy natural keys: MIDI 48, 50, 52, 53, 55, 57, 59, 60, 62, 64, 65, 67, 69, 71, 72, 74. Native and editor share the definition. Existing patches retain their assignments; Patches → Factory kit loads the new layout.
- The sampler has Play / Stop audition using its current sound settings and the existing independent preview engine. Switching keys or closing the sampler stops its preview. Completion tracking now lives in the shared preview module, so it works without the browser mounted. EQ, delay, and reverb start expanded.
- Validation: all 45 editor tests and production build pass. Component tests cover wide/tall/short parent resize, preview payload/toggle/key switching, and expanded effects. Native integration passes with all 16 natural-key voices and musical groove assertions. The actual exported Seaboard SVG was rasterized and inspected; full editor layout in Live remains a user check. Universal AU/VST3/standalone rebuilt and packaged; installed AU matches the build and current web assets. Pluginval strictness 5 and auval pass (`native/build/validation/natural-keys-au.txt`), with the pre-existing current-program warning.
- Installed at `~/Library/Audio/Plug-Ins/Components/Roli Drummer.component`. Previous copy: `/var/folders/rp/q6_vxvrx7xj8mvlvc87wq9s40000gp/T/rolidrummer-previous-au-ur1dwvfe/Roli Drummer.component`.


## Stopped-host performance and native window sizing

- The shared clip clock advances while host transport is stopped. Per-channel held contacts gate each part as before, including quantized launch/zone switches and the existing release-tail policy. Current finite positive host BPM is used even while stopped; missing/invalid tempo retains the last valid value (120 initially). Standalone uses patch BPM. Host playback re-enters reported song position with an explicit scheduler discontinuity, preserving held contacts and clearing stale voices. A KEYS indicator distinguishes local key playback from host transport.
- Added a native 12-pixel resize border and a 30-pixel fully clickable corner. Browser content reserves those margins. Removed the web editor's fixed minimum width, allowed panel columns and expression ranges to shrink/reflow, added scroll/focus padding, and let narrow footers wrap. Native background and grip color follow the selected shared theme through the bridge; host-owned title bars remain host-controlled.
- Validation: 45 frontend tests, TypeScript/production build, scheduler and processor integration pass. New native tests compare free-running audio with live-host audio at 93.75/137.5 BPM, change/omit tempo while stopped, join/release independent contacts, change CC74, resume/stop host playback, use panic, and compare 64/257/2048-sample partitions. Universal AU/VST3/standalone archives rebuilt and verified; installed AU matches all bundle files/current web assets. Pluginval strictness 5 and auval pass (`native/build/validation/stopped-transport-au.txt`), retaining the known current-program warning. Native editor lifecycle passes; final visual resize/theme checks in Live remain part of the hardware session.
- Updated AU installed at `~/Library/Audio/Plug-Ins/Components/Roli Drummer.component`. Previous copy preserved at `/var/folders/rp/q6_vxvrx7xj8mvlvc87wq9s40000gp/T/rolidrummer-previous-au-lfwmqiz_/Roli Drummer.component`. Existing patches need no reset.
