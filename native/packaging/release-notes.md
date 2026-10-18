# Roli Drummer 0.1.0

An MPE drum instrument for the Seaboard, with independent per-key parts and slide-controlled pattern zones synchronized to the DAW song position.

Neural pattern library: Patterns → Indexed library imports MIDI ZIP archives and folders without extracting every file. Enter a musical name such as Heavy Rock and resolve compatible sparse, steady and fill regions for the kit's unlocked keys. Each key retains its drum-note filter. Press, Glide and Strike select related candidates inside the Slide region; Lift shapes release. Saved patches retain their actual notes and reproduce after the library grows.

Resolution checks each key's part, phrase cycle and feel before sampling patterns, so whole-kit density cannot hide a usable percussion part. The updated policy recovers clap, side-stick and low-conga material in the personal Heavy Rock catalog and preserves the final per-key drum-note gate.

Drum release endings now follow the same sample across host buffer sizes, while delay/reverb tails continue independently. Ninety-second, fully held two-instance tests cover tempo changes, stopped transport, seeks, loops and prepared pattern refreshes.

Pattern families match new imports to existing musical queries. Review ambiguous matches, include or exclude patterns, and accept, rename, merge or dismiss suggested family names. Optional seeded generation recombines compatible original fragments and saves the resulting notes and provenance. MIDI program slots remain stable as patches are renamed or added; unassigned numbers leave the current patch playing.

Catalog preparation runs in the background and can pause/resume. Older complete indexes remain searchable while compact candidate indexes are prepared. Multiple instances coordinate queued preparation, and another initialized instance can open while a catalog writer is busy. A newly resolved performance waits for a musical boundary when contacts are held.

Family resolution batches per-key candidate queries and reuses a prepared catalog snapshot with committed features. Current warm Heavy Rock resolution/compilation measured about 0.62 seconds on the development machine, with unchanged resolved notes.

Development validation: all seventeen native suites, frontend checks, full-catalog reproducibility and universal resources/signatures pass at their recorded scope. Exact-bundle AU state/MIDI/render/lifecycle checks and headless VST3 strictness 5 pass both architecture slices. The earlier Apple validator run remains inconclusive; physical-controller feel and subjective listening quality are unmeasured. See the implementation ledger for coverage and operating limits.

Place `Roli Drummer.component` in `~/Library/Audio/Plug-Ins/Components` for AU hosts, or `Roli Drummer.vst3` in `~/Library/Audio/Plug-Ins/VST3` for VST3 hosts. The standalone app can run from any local folder.

The editor uses a vector Seaboard, SSB sound controls, and jamin's graph and data grid. Audio continues with the editor closed. DAW state embeds assigned sample data; portable `.rolidrummer` files include patterns, mappings, zones, and sounds.

CI builds record signing and notarization status in `BUILD.txt`. Version-tag releases are signed with Developer ID and notarized; local development archives may be signed ad hoc. See `SIGNING.md` for details.

October 2 update: live finger and sample-playback indicators; CC74 diagnostics and held-slide tests for Live's default MPE Lower Zone; distinct factory pattern densities for every part; sound/pattern audition buttons; SSB themes and coordinated zone colors; automatic five-surface mappings; a clearer Drum part selector. Restart the host after replacing the plug-in. Physical Seaboard verification remains a hardware test.

Circuit kit update: 16 tuned synth drum voices and three musical two-bar grooves. Snare backbeats stay on 2 and 4, with syncopated kicks, accented hats, ghost notes and tom fills. Select Patches → Factory kit in an existing session, or drag individual tuned presets from the built-in Sounds library. Custom kits are preserved.

CC74 correction: removed master-channel slide broadcasting. Slide is isolated to its receiving channel and current note, including pre-note values and channel reuse; CC74 cannot be learned as a global macro. Existing patches do not need a factory reset.

Stopped-transport performance: held keys now play at the DAW's current or last reported tempo without starting its transport. All parts share one clock and rejoin DAW song position on playback. Natural-key factory defaults, sampler Play/Stop, and expanded effects are included. The window has wider resize targets, more edge padding, responsive controls, and a native background matching the selected theme. No factory reset is needed for these playback/window changes.

Granulator update: adopted SSB aabc13d's four-slot stereo grain engine, Hermite sample interpolation, new window/spray/tuning controls, FM, and scanning. New numeric controls can be bound independently to each key's MPE surfaces. Old grain patches migrate size and position; the remaining cloud controls use new defaults. Legacy MPE grain bindings are retained with approximate conversions. Expect old granular patches to sound different.

Sustain and layout: CC64 holds lifted parts until pedal release, including while the DAW is stopped. Reassign it with Sustain pedal CC / Learn pedal under the Seaboard's Global MIDI bindings. Held owners stay independent when MPE channels are reused. The Seaboard header is removed, its tools sit above the zones, and the image fits the available space without stretching or overflowing.

Sustain markers: lifted notes retain their finger markers at the captured position until the pedal is released. Pedal-held markers use the theme's primary color with a dashed outline; touching fingers use the secondary color. MIDI channel reuse does not move the sustained markers.

SSB interface update: expression mappings now live inside the sampler as a collapsible module, with surface enum buttons, parameter menus, FROM/TO/SLEW dials and draggable response-curve canvases. Double-click a curve for linear response; arrows adjust it precisely. The header, transport displays, output, tabs, theme selector, footer and Seaboard zone tools now share the SSB styling.

Compact layout: selected-key name, part, playback and note-filter controls now sit beside the waveform instead of above the sampler. The patch/transport header and tabs are shallower, giving the sound controls more vertical space.

Project tile and patch selector: the compact ROLI DRUMMER tile now opens SSB’s grouped theme swatches with live previews and previous/next controls. The header patch display is read-only, with previous/next arrows for the factory kit and saved patches. Patch naming remains in the Patches tab.

Key Zones now matches the Expression module’s height, including when expression mappings wrap. This removes excess vertical space above Grain, Delay and Reverb.

Main bus controls: compact header dials for reverb, delay, chorus, cutoff and resonance. BUS FX opens effect settings and assignments for the three faders and XY pad. Bound controls update the whole mix with smoothing; turning a header dial restores manual control. Patch/session recall includes settings and bindings.

Preset buttons: master-channel MIDI Program Change selects persistent slots with the editor closed. The RISE sends absolute 0–127 positions; unassigned slots leave the current patch playing. Header and device-artwork arrows browse the visible patch list independently. Hardware verification in Live remains necessary because hosts can filter Program Change.

Patch library: replaced patch cards with a searchable canvas grid and force graph. Load from the grid cell, double-click/Enter, or the selection button. Browser state and graph layout synchronize between instances. Naming, kit level, quantization, release behavior and file actions now use compact SSB modules, dials, enum controls and hardware-style buttons.
