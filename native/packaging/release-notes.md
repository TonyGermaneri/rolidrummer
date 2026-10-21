# Roli Drummer 0.7.5

Pattern families keep up with searching. Before, every search registered its query as a family and started rematching the whole catalog. A pending match then made the next search fail with "Family membership is updating", and Settings could show "Matching updates…" indefinitely.

- Searching no longer registers a family. Saving a searched groove with SAVE does, so each search runs once and starts no catalog work.
- Matching is incremental. A new family or decision recomputes only the families it affects; name suggestions reuse one catalog scan per search index. A match that a later change makes obsolete stops at once instead of finishing first. Memberships, ambiguity and suggestions are identical to 0.7.4's.
- Searches never fail while matching catches up: a family resolves from its last confirmed membership, and a family not matched yet resolves as a plain search.
- The Pattern families summary says what out-of-date membership is waiting for: "Matching updates… n/m" only while matching runs, otherwise "Matching failed", "Matching paused", "Waiting for search to be refreshed" or "Out of date", with MATCH NOW. Settings offers RETRY FAMILIES after a failure. Matching that only waited for another instance's database lock is retried automatically.
- Startup rebuilds a search index that an interrupted import left behind the catalog. Before, family matching waited on it indefinitely.
- Remove forgets a family and its decisions. Remove search families clears the families earlier versions registered from searches, if they were never saved, renamed, edited or accepted from a suggestion. Those near-duplicates also made each other's patterns ambiguous. Both actions need a second click to confirm.
- Abandoned matching builds and superseded suggestions are deleted instead of accumulating in the library database.

After updating, the first match processes every registered family once. Later changes are incremental.

The macOS downloads contain universal Apple Silicon and Intel AU, VST3 and standalone builds. The AU archive is **RoliDrummer.component-v0.7.5.zip**, containing the **RoliDrummer.component** bundle. The original notarized archives are published directly, with checks for clean extraction, macOS package recognition, signatures and stapled tickets.

Neural indexing and search require macOS 13.4 or later. Your personal MIDI library is not included. Use a portable **.rolidrummer** export to transfer a prepared patch with its patterns, mappings and sounds.

## Validation

Local validation passed 194 frontend tests and all 22 native suites. The family suite adds checks for these cases:

- a registration recomputes only its own family;
- an incremental build equals one from nothing;
- resolution works while matching is pending;
- dead builds and old suggestions are pruned;
- saved provenance is kept;
- a removed family is gone.

On a synthetic 15,000-pattern catalog, 0.7.4 and 0.7.5 produced byte-identical memberships and suggestions. Rematching after one new family took 4.2 s instead of 13.5 s in a fresh command-line process. About 3.9 s of that is the cold model load, which the instrument keeps loaded.

Release CI repeats the frontend and native suites, Apple auval, VST3 pluginval at strictness 5, universal bundle checks, Developer ID signing and Apple notarization before publishing.

**BUILD.txt** records the release commit and signing/notarization status; **SHA256SUMS** contains archive checksums.
