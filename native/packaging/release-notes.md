# Roli Drummer 0.6.0

Play every key, follow parallel keywaves, and see queued starts and stops immediately.

- Keywaves bind in chromatic pitch order, including sharps and flats. The Circuit kit's 16 voices occupy MIDI 48–63. Existing patches and sound kits migrate their ordered bindings once, preserving their sounds, patterns, drum-note filters, zones and expression mappings.
- Unbound pitches repeat the ordered bank at both edges as **parallel keys**. They stay visually unbound while sharing their origin's sound, patterns, zones and per-key ownership and latch rules. Playing a parallel key lights both it and its origin; **Track** follows the origin's editor. Physical note-off and per-note pressure remain tied to the key actually played.
- The on-screen Seaboard uses each black key's actual shorter height for its full slide range. Touches below a black key route to the adjacent white key according to horizontal position. All chromatic keys remain playable, including E/F and B/C.
- Quantized actions show a green **play** indicator for a queued start and a red **stop** indicator for a queued stop. Indicators appear as soon as the action is queued, remain visible after short taps, and clear when the action executes or is canceled. Parallel keys show the action on their origin too. Normal-mode quantized launches also show their pending start.
- Sound grids now fill their containers on first display, resize and tab changes. Graph view initializes after its canvas is mounted, fits when the panel first becomes visible, and preserves a usable view while resizing. A **Fit** control brings the graph back into view. These fixes cover both the Keywave sound browser and Kit browser.

The existing Normal, Hold and Toggle modes, quantization choices, kit previews, semantic matching and pattern-generation choices remain available.

The macOS downloads contain universal Apple Silicon and Intel AU, VST3 and standalone builds. The AU archive is **RoliDrummer.component-v0.6.0.zip**, containing the **RoliDrummer.component** bundle. The original notarized archives are published directly, with checks for clean extraction, macOS package recognition, signatures and stapled tickets.

Neural indexing and search require macOS 13.4 or later. Your personal MIDI library is not included. Use a portable **.rolidrummer** export to transfer a prepared patch with its patterns, mappings and sounds.

## Validation

Local validation passed 182 frontend tests and all 21 native suites, plus universal builds and AU initialization on Apple Silicon and Intel. Coverage includes all 128 MIDI pitches, parallel-key ownership and expression, legacy-layout migration, black-key hit testing, queued-action execution and cancellation, and allocation-free audio callbacks. Real WebKit checks verified both sound browsers at three window sizes, including grid canvas dimensions and visible graph labels.

Release CI repeats the frontend and native suites, Apple auval, VST3 pluginval at strictness 5, universal bundle checks, Developer ID signing and Apple notarization before publishing.

**BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
