# Roli Drummer 0.4.0

Watch the patterns each keywave plays and follow your performance with the new piano rolls.

- The Key panel splits the waveform area equally between the sound waveform and the current or last pattern, with a moving playback marker. The preview follows the actual gesture-selected variation and applies that keywave’s MIDI drum-note filter.
- Zone rows show miniature piano rolls, including the current or last variation’s transport position.
- The new **Piano Roll** tab shows live drum output on a scrolling MIDI keyboard grid. Notes grow from a fixed playhead; colors follow the source keywave. Choose one, two, four or eight bars of history, scroll through pitches, or clear the display.
- **TRACK** in the Key panel follows the last keywave touched. Turn it off to pin the selected keywave. It starts off and is separate from the saved patch.
- Monitoring preserves note timing, duration and releases, including short taps. Its rolling clock stays continuous across host seeks and tempo changes. The bounded monitoring queue never makes the audio callback wait or allocate memory.

This release retains Patterns Only as the default, the optional 1–4 generation choices, Auto/manual matching, patch saving, Settings, per-key Normal/Hold/Toggle behavior and the SSB sound engine.

The macOS downloads contain universal Apple Silicon and Intel AU, VST3 and standalone builds. The AU archive is **RoliDrummer.component-v0.4.0.zip**, containing the **RoliDrummer.component** bundle. The original notarized archives are published directly, with checks for clean extraction, macOS package recognition, signatures and stapled tickets.

Neural indexing and search require macOS 13.4 or later. Your personal MIDI library is not included. Use a portable **.rolidrummer** export to transfer a prepared patch with its patterns, mappings and sounds.

## Validation

Local validation passed 146 frontend tests, all 18 native suites and universal builds. Coverage includes filtered pattern previews, live note geometry, Track behavior, sample timing, note endings, short taps, queued activation, host seeks, tempo changes and audio restart. The real-time stress suite checks allocation-free callbacks and instance isolation.

Release CI repeats the frontend and native suites, Apple auval, VST3 pluginval at strictness 5, universal bundle checks, Developer ID signing and Apple notarization before publishing.

**BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
