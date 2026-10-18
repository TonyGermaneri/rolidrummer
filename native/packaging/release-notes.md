# Roli Drummer 0.2.0

Hold and Toggle modes bring hands-free, per-key pattern control to the main keyboard.

- **Normal** plays a part while its key is held, with ordinary sustain-pedal support.
- **Hold** keeps released keys playing, like CC64 sustain.
- **Toggle** latches each key independently. Press the same zone again to stop that key; press another zone to replace its old pattern. One active owner per key is maintained across MPE member-channel changes, and stale finger input cannot change the replacement.
- Toggled keys have purple backgrounds and matching finger markers. Released Hold keys retain ordinary sustain styling.
- Pedal-up clears released latches while physically held keys continue. The selected mode stays armed for subsequent releases. Pedal reassignment and Panic remain supported.
- Mode selections are saved in patches and DAW sessions. Live latched notes are not restored; older patches default to Normal. Existing sound settings, drum-note filters and pattern timing remain intact.
- Release archive names now match the filenames in **SHA256SUMS** after downloading from GitHub.

This release retains the neural MIDI library search, resolved per-key pattern regions, seeded variations and SSB 0.6.2 sound engine from 0.1.0.

## Install the AU on another Mac

1. Download and unzip the **AU archive** (its filename contains **.component**) from the release assets.
2. Place **Roli Drummer.component** in **~/Library/Audio/Plug-Ins/Components/**. Create that folder if necessary.
3. Restart Ableton Live or rescan Audio Units in its plug-in settings. Load **Roli Drummer** from the Audio Units list and enable MPE for your Seaboard.

The VST3 archive installs into **~/Library/Audio/Plug-Ins/VST3/**. The standalone app can run from a local Applications folder. All three builds support Apple Silicon and Intel. Neural indexing/search requires macOS 13.4 or later because of the bundled inference runtime.

Your personal MIDI library is not included in these downloads. Use a portable **.rolidrummer** export to transfer a prepared patch with its patterns, mappings and sounds, or import the library on the other Mac.

## Validation and limits

All 17 native suites, 121 editor tests and universal bundle checks pass. Hold/Toggle scheduler, allocation-guard and processor checks also pass under x86_64. Apple auval and VST3 pluginval strictness 5 pass. Hardware feel and musical listening remain dependent on the controller, host and material.

The instrument supports up to 64 voices. Extreme CLOUD settings with 20 grains per side across all 64 voices exceed real time at 96 kHz on the measured development machine; reduce density/polyphony or use a lower sample rate for that workload. See IMPLEMENTATION.md in the repository for measured operating limits.

Version-tag CI requires Developer ID signing and Apple notarization before publication. **BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
