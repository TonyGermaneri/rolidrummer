# Roli Drummer 0.5.0

Audition sound kits before applying them, browse sounds beside the Keywave editor, and latch parts on musical boundaries.

- The new **Kit** tab saves and loads sound-only kits. Semantic **Auto Bind** matches sounds for all keywaves or just the selected keywave, with drum-part filtering and a persistent local embedding cache. Kits preserve patterns, MIDI drum-note filters, expression routes and bus settings.
- Auto Bind immediately auditions proposed sound bindings in **Preview** and lists the matched sounds. Play the keywaves, then choose **Apply Kit** to keep the bindings or **Cancel** to restore the previous sounds. Leaving the search or editing the instrument cancels the preview. Previewing leaves the saved patch, DAW session and Undo history untouched; Apply creates one undoable edit. Apply a preview before saving it as a named kit.
- The renamed **Keywave** tab includes a right-hand sound browser with Grid and Graph views. Select and assign sounds, double-click them, or drag them onto a keywave while its controls stay visible. Its semantic sound search also offers Preview, Apply and Cancel.
- Actual scheduled drum hits flash their source keywaves on the Seaboard, independently of held, toggled and queued indicators.
- **Latch Quantize** defaults to one bar in Hold and Toggle. Choose Instant or 1–16 bars. Queued starts, stops and different-zone replacements take effect together at the next selected boundary; a second press can cancel a pending toggle. Pedal-up uses the same grid for released keys. Panic clears active and queued notes immediately. Normal mode retains ordinary press/release behavior. Bar lengths follow the patch meter, including odd meters.
- Short patterns repeat independently of longer patterns, and piano-roll previews show their repeated passes and loop boundaries. MIDI imports use the selected part’s own track length, keeping authored rests without inheriting padding from unrelated or conductor tracks. Existing catalog bindings retain their identities when their playable loop is corrected.

This release retains Patterns Only as the default, optional 1–4 generative tracks, Auto/manual pattern matching, patch saving, the live Piano Roll and Track controls, Settings, per-key Normal/Hold/Toggle behavior and the SSB sound engine.

The macOS downloads contain universal Apple Silicon and Intel AU, VST3 and standalone builds. The AU archive is **RoliDrummer.component-v0.5.0.zip**, containing the **RoliDrummer.component** bundle. The original notarized archives are published directly, with checks for clean extraction, macOS package recognition, signatures and stapled tickets.

Neural indexing and search require macOS 13.4 or later. Your personal MIDI library is not included. Use a portable **.rolidrummer** export to transfer a prepared patch with its patterns, mappings and sounds.

## Validation

Local validation passed 168 frontend tests and all 20 native suites, plus universal builds and AU initialization on Apple Silicon and Intel. Coverage includes live kit audition, untouched session state, Apply/Cancel, stale search results, host recalls, sound-only kit edits, quantized latch transitions, independent loops, legacy catalog bindings, live monitoring and allocation-free audio callbacks.

Release CI repeats the frontend and native suites, Apple auval, VST3 pluginval at strictness 5, universal bundle checks, Developer ID signing and Apple notarization before publishing.

**BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
