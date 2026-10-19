# Roli Drummer 0.3.0

The pattern matcher now opens in **Auto** mode: enter a musical description, then play. A seed input and die button explore repeatable variations without choosing roles, zones or search settings.

- Auto always uses semantic similarity and binds sparse, steady and fill regions across the keyboard. The seed selects original patterns or a kit-wide budget of one to four generated tracks, subject to compatible source material.
- Seed changes affect original selection and gesture navigation as well as generation. Rapid changes keep the latest result; rerolls replace the previous Auto pool instead of accumulating abandoned patterns.
- The **Save** disk button creates an independent named patch with its actual pattern notes, including generated notes. Existing saved patches are retained, and recall does not run a new search.
- Turn Auto off to reveal manual semantic/description search, role and zone selection, phrase length and generation controls.
- A new **Settings** tab follows Patches. Pattern imports, worker status, family maintenance, MIDI-file browsing, sound management, playback and bus options, MIDI mappings, appearance, database statistics and factory reset are collected there.
- Matcher and Settings use compact, flat controls. The manual template-zone editor is hidden while Auto matching is active.

Existing per-key Normal, Hold and Toggle behavior, drum-note filtering and the SSB 0.6.2 sound engine are retained. Older patches recall their stored notes.

Downloads contain universal Apple Silicon and Intel AU, VST3 and standalone builds. The AU archive is **RoliDrummer.component-v0.3.0.zip**, containing the **RoliDrummer.component** bundle. The release publishes the original notarized archives and verifies clean extraction, package recognition, signatures and stapled tickets.

Neural indexing and search require macOS 13.4 or later. Your personal MIDI library is not included. A portable **.rolidrummer** export transfers a prepared patch with its patterns, mappings and sounds.

## Validation

The implementation passed 133 editor tests and all 17 native suites, with additional scheduler coverage confirming that original-only seed changes reach gesture-selected playback. Native UI checks covered search, reroll, manual controls, Settings, independent patch saving and restart/recall using a private catalog.

Release CI additionally runs Apple auval, VST3 pluginval at strictness 5, universal bundle checks, Developer ID signing and Apple notarization before publication. **BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
