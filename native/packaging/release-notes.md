# Roli Drummer 0.7.2

Ribbon gestures now act on the individual keywave you touch. Multiple keywaves can stay independently toggled on, with one zone and pattern playing per keywave.

- The top ribbon plays the touched keywave's highest zone, following Normal, Hold or Toggle mode. The bottom ribbon stops only that keywave. Sweeping across the ribbon encounters each keywave in order.
- Both ribbons have separate lanes for every white and black key. White-key bodies exclude the ribbons. Below a black key's shorter body, touches belong to the adjacent white keys; the black key's stop lane returns at the bottom ribbon.
- In Toggle mode, pressing an active zone again stops only that keywave. Pressing another zone replaces only that keywave's pattern. Other toggled keywaves keep playing independently.
- Ribbon actions preserve latch quantization and Instant/bypass behavior. Green and red indicators identify queued starts and stops. A stop cancels a pending start for the touched keywave, and parallel keys retain their shared origin and highlighting.
- Hardware ribbon sweeps follow per-note Slide and Glide across chromatic lanes, including lanes crossed between MIDI updates. Track follows the latest touched lane and its editable origin. Member-channel pitch sensitivity uses the Seaboard's default ±48-semitone range or the controller's RPN 0 setting.
- Individual on-screen ribbon lanes support pointer taps, sweeps, Enter and Space. Moving from a key body into its top ribbon selects the highest zone without an extra toggle strike. Pre-note and post-release MIDI resets do not trigger ribbon actions.

For hardware ribbon gestures, use absolute Slide with full Slide travel: CC74 values 0–15 address the bottom ribbon, 116–127 the top ribbon, and the body between them spans each key's zone matrix. Ribbon gestures preserve the saved patch and DAW session document.

The macOS downloads contain universal Apple Silicon and Intel AU, VST3 and standalone builds. The AU archive is **RoliDrummer.component-v0.7.2.zip**, containing the **RoliDrummer.component** bundle. The original notarized archives are published directly, with checks for clean extraction, macOS package recognition, signatures and stapled tickets.

Neural indexing and search require macOS 13.4 or later. Your personal MIDI library is not included. Use a portable **.rolidrummer** export to transfer a prepared patch with its patterns, mappings and sounds.

## Validation

Local validation passed 187 frontend tests, all 22 native suites and five portable core suites, plus universal builds and AU initialization on Apple Silicon and Intel. Regression coverage includes independent toggles, per-key ribbon actions, quantized cancellation, hardware chromatic sweeps, black-key gaps, parallel origins, MIDI channel reuse and host-buffer timing. Real WebKit hit testing verified all 98 on-screen ribbon targets, key geometry at three window sizes, and both sound browsers at those sizes.

Release CI repeats the frontend and native suites, Apple auval, VST3 pluginval at strictness 5, universal bundle checks, Developer ID signing and Apple notarization before publishing.

**BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
