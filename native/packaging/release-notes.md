# Roli Drummer 0.2.1

Every keyboard mode now enforces one active pattern zone per key.

- **Normal** and **Hold** replace that key's previous owner on each new press, including when the Seaboard assigns a different MPE channel. Repeated presses retrigger the part; they do not toggle it off.
- **Toggle** retains its existing rules: the same zone stops the key, while a different zone replaces it.
- Replaced fingers cannot change the active zone, release its new owner or restart through a patch edit. Other keys, pedal behavior, musical quantization and drum-note filters retain their existing rules.
- Release packaging now verifies a fresh extraction of every archive: one correctly named bundle, matching metadata/version, executable permissions, valid code signatures and Finder package recognition. The AU is a `.component` bundle directory, installed as a complete item.

This patch retains Hold/Toggle selection recall, purple toggle indicators, neural library matching and the SSB 0.6.2 sound engine.

## Install the AU on another Mac

1. Download and unzip the **AU archive** (its filename contains **.component**) from the release assets. The extracted item is **Roli Drummer.component**, a complete macOS bundle; Finder should offer **Show Package Contents**.
2. Move the complete **Roli Drummer.component** bundle (not its inner **Contents** folder) into **~/Library/Audio/Plug-Ins/Components/**. Create that folder if necessary.
3. Restart Ableton Live or rescan Audio Units in its plug-in settings. Load **Roli Drummer** from the Audio Units list and enable MPE for your Seaboard.

The VST3 archive installs into **~/Library/Audio/Plug-Ins/VST3/**. The standalone app can run from a local Applications folder. All three builds support Apple Silicon and Intel. Neural indexing/search requires macOS 13.4 or later because of the bundled inference runtime.

Your personal MIDI library is not included in these downloads. Use a portable **.rolidrummer** export to transfer a prepared patch with its patterns, mappings and sounds, or import the library on the other Mac.

## Validation and limits

All 17 native suites, 121 editor tests and universal bundle checks pass. Hold/Toggle scheduler, allocation-guard and processor checks also pass under x86_64. Apple auval and VST3 pluginval strictness 5 pass. Hardware feel and musical listening remain dependent on the controller, host and material.

The instrument supports up to 64 voices. Extreme CLOUD settings with 20 grains per side across all 64 voices exceed real time at 96 kHz on the measured development machine; reduce density/polyphony or use a lower sample rate for that workload. See IMPLEMENTATION.md in the repository for measured operating limits.

Version-tag CI requires Developer ID signing and Apple notarization before publication. **BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
