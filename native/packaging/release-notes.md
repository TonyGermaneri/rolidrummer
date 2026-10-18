# Roli Drummer 0.1.0

An MPE drum instrument for the ROLI Seaboard, with independent drum parts and expression for each held key.

- Import MIDI folders or ZIP libraries, then search a musical name such as **Heavy Rock** to resolve compatible sparse grooves, steady beats and fills. Each key keeps its drum-note filter.
- Slide selects pattern regions; Strike, Glide and Press shape intent and sound independently per contact. Lift shapes release, and CC64 sustains lifted parts. Playback follows the DAW or runs from the current tempo while transport is stopped.
- Optional seeded variations recombine compatible pattern fragments. Saved patches and DAW sessions retain the resolved MIDI notes, mappings and assigned sample data.
- The sound engine incorporates SSB 0.6.2: Granulator III CLASSIC/LOOP/CLOUD modes, variation and scanning, expanded modulation, stereo state-variable filters and key scaling. Loop interpolation and mono/poly note-release fixes are included.
- Existing presets, patch contents and levels are preserved by this sound-engine update. Legacy grainReverse gesture bindings still reverse the whole sample; grainLoopReverse controls LOOP segment reverse probability.
- Delay and room tails remain independent between contacts, with exact drum-release timing across host buffer sizes. Panic clears held and sustained voices.

## Install the AU on another Mac

1. Download and unzip the **AU archive** (its filename contains **.component**) from the release assets.
2. Place **Roli Drummer.component** in **~/Library/Audio/Plug-Ins/Components/**. Create that folder if necessary.
3. Restart Ableton Live or rescan Audio Units in its plug-in settings. Load **Roli Drummer** from the Audio Units list and enable MPE for your Seaboard.

The VST3 archive installs into **~/Library/Audio/Plug-Ins/VST3/**. The standalone app can run from a local Applications folder. All three builds support Apple Silicon and Intel. Neural indexing/search requires macOS 13.4 or later because of the bundled inference runtime.

Your personal MIDI library is not included in these downloads. Use a portable **.rolidrummer** export to transfer a prepared patch with its patterns, mappings and sounds, or import the library on the other Mac.

## Validation and limits

All 17 native suites, 119 editor tests and universal bundle checks pass. Engine and expression checks pass on Apple Silicon and Intel; Apple auval and VST3 pluginval strictness 5 pass. Hardware feel and musical listening remain dependent on the controller, host and material.

The instrument supports up to 64 voices. Extreme CLOUD settings with 20 grains per side across all 64 voices exceed real time at 96 kHz on the measured development machine; reduce density/polyphony or use a lower sample rate for that workload. See IMPLEMENTATION.md in the repository for measured operating limits.

Version-tag CI requires Developer ID signing and Apple notarization before publication. **BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
