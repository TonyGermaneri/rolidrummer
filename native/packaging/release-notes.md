# Roli Drummer 0.1.0

An MPE drum instrument for the Seaboard, with independent per-key parts and slide-controlled pattern zones synchronized to the DAW song position.

Place `Roli Drummer.component` in `~/Library/Audio/Plug-Ins/Components` for AU hosts, or `Roli Drummer.vst3` in `~/Library/Audio/Plug-Ins/VST3` for VST3 hosts. The standalone app can run from any local folder.

The editor uses a vector Seaboard, SSB sound controls, and jamin's graph and data grid. Audio continues with the editor closed. DAW state embeds assigned sample data; portable `.rolidrummer` files include patterns, mappings, zones, and sounds.

Build signing and notarization status is recorded in `BUILD.txt`. Version-tag releases are signed with Developer ID and notarized; local development archives may be signed ad hoc. See `SIGNING.md` for details.

October 2 update: live finger and sample-playback indicators; CC74 diagnostics and held-slide tests for Live's default MPE Lower Zone; distinct factory pattern densities for every part; sound/pattern audition buttons; SSB themes and coordinated zone colors; automatic five-surface mappings; a clearer Drum part selector. Restart the host after replacing the plug-in. Physical Seaboard verification remains a hardware test.

Circuit kit update: 16 tuned synth drum voices and three musical two-bar grooves. Snare backbeats stay on 2 and 4, with syncopated kicks, accented hats, ghost notes and tom fills. Select Patches → Factory kit in an existing session, or drag individual tuned presets from the built-in Sounds library. Custom kits are preserved.

CC74 correction: removed master-channel slide broadcasting. Slide is isolated to its receiving channel and current note, including pre-note values and channel reuse; CC74 cannot be learned as a global macro. Existing patches do not need a factory reset.

Stopped-transport performance: held keys now play at the DAW's current or last reported tempo without starting its transport. All parts share one clock and rejoin DAW song position on playback. Natural-key factory defaults, sampler Play/Stop, and expanded effects are included. The window has wider resize targets, more edge padding, responsive controls, and a native background matching the selected theme. No factory reset is needed for these playback/window changes.
