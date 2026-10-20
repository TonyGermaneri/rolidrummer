# Roli Drummer 0.7.4

Every finger touching the Seaboard now has its own glowing circle. Playback, latch and queued commands appear as separate echo rings, with connecting traces and emitted pulses showing which finger created each command.

- Physical finger circles follow Slide, pitch and pressure across the keys and ribbons. They stay visible when a zone changes, a keywave toggles off or a command waits for its quantized boundary, including when fingers share a parallel origin.
- Lifting a finger removes its physical circle. A latched or sustained command leaves its echo at the captured position until that command ends. Queued starts and stops retain their green and red indicators.
- On-screen fingers appear immediately at their pointer positions. Hardware and on-screen touches remain distinct, and command graphics leave keyboard hit testing unchanged. Reduced-motion preferences disable the emitted animation.
- Existing independent keywave toggles, one-zone ownership, parallel keys, per-key ribbons and latch quantization continue to apply.
- Release packaging now retries transient Apple ticket delivery and validation failures, including the CloudKit network error that interrupted an earlier release. Publishing still requires successful signing, notarization and ticket validation.

The reported nearby-key playback dropout has not been reproduced. Regression tests confirm four adjacent keywaves can play concurrently; this release does not claim to resolve that specific hardware symptom.

The macOS downloads contain universal Apple Silicon and Intel AU, VST3 and standalone builds. The AU archive is **RoliDrummer.component-v0.7.4.zip**, containing the **RoliDrummer.component** bundle. The original notarized archives are published directly, with checks for clean extraction, macOS package recognition, signatures and stapled tickets.

Neural indexing and search require macOS 13.4 or later. Your personal MIDI library is not included. Use a portable **.rolidrummer** export to transfer a prepared patch with its patterns, mappings and sounds.

## Validation

Local validation passed 191 frontend tests, all 22 native suites and five portable core suites, plus universal builds and AU initialization on Apple Silicon and Intel. Nearby-key regressions cover 288 arrival cases across Normal, Hold and Toggle modes and latch quantization settings. A processor test confirms four adjacent contacts produce at least four audible voices. WebKit checks at three window sizes verified distinct physical fingers, source-linked command echoes, persistent node identities, release behavior and unchanged hit testing.

Release CI repeats the frontend and native suites, Apple auval, VST3 pluginval at strictness 5, universal bundle checks, Developer ID signing and Apple notarization before publishing.

**BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
