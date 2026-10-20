# Roli Drummer 0.7.0

Use the Seaboard ribbons to open the whole kit or stop it on a musical boundary.

- White-key zone matrices now fit the physical key bodies and exclude both ribbons. Black keys retain their shorter slide range; touches below an on-screen black key still route to the adjacent white key. Each key body spans its full zone range.
- The top **Top Zone · All Keywaves** ribbon starts every bound keywave at its highest zone. Parts remain playing until stopped or replaced by a key press. Parallel keys share their origins, so the group action starts each part once. Repeated sweeps set the requested state without toggling parts off, and opening the top matrix preserves neutral pitch.
- The bottom **Stop All** ribbon stops all playing keywaves and cancels starts that have not sounded yet. Both ribbons follow **Latch Quantize** and its bar count in Normal, Hold and Toggle, or act immediately with Instant/bypass. A short tap is sufficient. Green play and red stop indicators show pending actions on the affected keys.
- The on-screen ribbons support pointer taps, captured sweeps, Enter and Space. Hardware gestures use absolute per-note Slide (CC74): 0–15 addresses the bottom ribbon and 116–127 the top ribbon. The key-body range between them is scaled for each white or black key. Use the Seaboard's absolute Slide mode with full Slide travel.
- Pre-note and post-release CC74 resets do not invoke ribbon commands; hardware ribbon actions require a physical touch. Key-body expression stays per note. Ribbon performance commands leave the saved patch and DAW session document intact.

This release retains chromatic bindings, parallel-key tracking and origin highlighting, sound kit previews, semantic matching, per-key Normal/Hold/Toggle behavior, and the sound-browser sizing fixes.

The macOS downloads contain universal Apple Silicon and Intel AU, VST3 and standalone builds. The AU archive is **RoliDrummer.component-v0.7.0.zip**, containing the **RoliDrummer.component** bundle. The original notarized archives are published directly, with checks for clean extraction, macOS package recognition, signatures and stapled tickets.

Neural indexing and search require macOS 13.4 or later. Your personal MIDI library is not included. Use a portable **.rolidrummer** export to transfer a prepared patch with its patterns, mappings and sounds.

## Validation

Local validation passed 184 frontend tests, all 22 native suites and five portable core suites, plus universal builds and AU initialization on Apple Silicon and Intel. Coverage includes hardware and screen ribbon commands, all keyboard modes, instant and quantized actions, reversal and cancellation, released fingers, all 128 bound keys, MIDI reset isolation, host-buffer timing and allocation-free audio callbacks. Real WebKit checks verified separate key and ribbon geometry at three window sizes and both sound browsers at those sizes.

Release CI repeats the frontend and native suites, Apple auval, VST3 pluginval at strictness 5, universal bundle checks, Developer ID signing and Apple notarization before publishing.

**BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
