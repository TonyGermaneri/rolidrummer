# Roli Drummer 0.3.1

Generation is now an explicit choice in the pattern matcher. The dropdown beside the search offers **Patterns Only** (the default), **1 Generative**, **2 Generative**, **3 Generative** and **4 Generative**.

- New searches and native resolver calls default to zero generated tracks. Seed rerolls change pattern selection without turning generation on.
- Generation remains available through the 1–4 choices. Auto uses that count across the whole kit; manual mode keeps its existing per-region generation behavior.
- Choosing Patterns Only in Auto reruns matching and replaces the previous generated pool with library patterns. A late generated search result cannot overwrite the newer choice.
- Saving and recalling a patch retains its selected generation count and actual notes. Existing saved patches keep their notes until explicitly changed.

This update retains the Auto/manual matcher, independent patch Save, compact Settings workspace, per-key Normal/Hold/Toggle behavior, drum-note filters and SSB 0.6.2 sound engine from 0.3.0.

The macOS downloads contain universal Apple Silicon and Intel AU, VST3 and standalone builds. The AU archive is **RoliDrummer.component-v0.3.1.zip**, containing the **RoliDrummer.component** bundle. The release publishes the original notarized archives and checks clean extraction, package recognition, signatures and stapled tickets.

Neural indexing and search require macOS 13.4 or later. Your personal MIDI library is not included. Use a portable **.rolidrummer** export to transfer a prepared patch with its patterns, mappings and sounds.

## Validation

Regression coverage checks Patterns Only defaults, explicit generation counts, seed rerolls, generation-to-original transitions, saved-choice recall and rejection of stale generated results. Release CI runs frontend and native suites, Apple auval, VST3 pluginval at strictness 5, universal bundle checks, Developer ID signing and Apple notarization before publishing.

**BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
