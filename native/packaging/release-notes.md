# Roli Drummer 0.7.6

Patterns follow the shared song position, and corresponding tom zones share a complete source performance so tom rolls stay together.

- Host song position is now respected even when the host omits its playing flag. With a stationary stopped cursor, local playback anchors to that song position and keeps advancing while keys are muted; moving the cursor reanchors it. Running transport remains authoritative.
- Keys unmute the current pattern phase at their configured launch or latch boundary. They do not establish a new clip origin. Each pattern repeats at its own loop length, and zone changes retain the shared song phase.
- Piano-roll markers keep following song position while muted, including zones that have not been played yet. Queued starts are shown as pending until their unmute boundary.
- Semantic matching selects each tom zone once for the whole tom group. High, mid and low tom keys use that same source pattern while keeping their individual MIDI note filters, sounds and expression routes. Generated tom patterns also preserve the complete roll.
- Tom zones use a fixed source performance; independent pressure/glide candidate selection cannot split the roll into unrelated patterns. Pressure and glide still shape each sound. Other drum parts and mixed drum-note masks retain independent matching.
- Manual tom pattern drops, zone edits, boundaries and locks apply across the tom group. Recalled patches with different tom matrices adopt the lowest-pitched locked tom's layout, or the lowest-pitched tom's layout when none is locked. Auto matching unlocks and resolves the group together.

The macOS downloads contain universal Apple Silicon and Intel AU, VST3 and standalone builds. The AU archive is **RoliDrummer.component-v0.7.6.zip**, containing the **RoliDrummer.component** bundle. The original notarized archives are published directly, with checks for clean extraction, macOS package recognition, signatures and stapled tickets.

Neural indexing and search require macOS 13.4 or later. Your personal MIDI library is not included. Use a portable **.rolidrummer** export to transfer a prepared patch with its patterns, mappings and sounds.

## Validation

Local validation passed 198 frontend tests and all 22 native suites, plus the production editor build, universal bundle checks and AU initialization on Apple Silicon and Intel.

Song-position regressions cover 21 processor cases across Normal, Hold and Toggle, buffer sizes of 64, 257 and 2,048 samples, mid-song entry, a stationary stopped song cursor, and advancing host PPQ without a playing flag. Existing seek, tempo-change, unequal-loop and transport regressions also pass.

Tom-roll tests verify exact filtered MIDI output when entering midway through a phrase, original and generated patterns, registered and unregistered family searches, CPU/GPU agreement, group locks and saved-patch migration. Song-position and tom suites also pass under Intel translation. Frontend tests cover continuously moving muted markers and synchronized manual tom edits and drops.

Release CI repeats the frontend and native suites, Apple auval, VST3 pluginval at strictness 5, universal bundle checks, Developer ID signing and Apple notarization before publishing.

**BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
