# Source provenance

SSB source initially adapted from commit 75656b6, with the grain engine, controls, math, preset migration and tests updated from aabc13d. Sound engine changes through SSB 0.6.2 (4be313f), including e6c7fc2's Granulator III engine, d4d496c's loop interpolation fix and 4508110's mono/poly note-release fix, are incorporated with Roli Drummer's per-contact adaptations. This update excludes SSB's waveform assets, factory presets, patch levels and preset migrations (MIT, Copyright 2026 Anthony Germaneri). Original MIT license below.

Jamin graph, MIDI and percussion source adapted from commit a93064b (GPL-3.0-or-later).

JUCE and npm dependencies retain their respective licenses.

ZIP64 pattern ingestion uses [libarchive](https://www.libarchive.org/), with its [upstream license notices](https://github.com/libarchive/libarchive/blob/v3.8.1/COPYING). macOS builds dynamically link Apple's system libarchive; Homebrew provides the public build headers. The generated native drum-map data comes from the adapted Jamin percussion definitions in `src/core/drumKits.js`; `node tools/export-drum-maps.mjs` regenerates it and a parity test checks both importers' vocabulary.

Neural search uses the pretrained [Sentence Transformers all-MiniLM-L6-v2](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2) model, revision `1110a243fdf4706b3f48f1d95db1a4f5529b4d41`, under Apache-2.0. Its model card and Apache license ship in `Contents/Resources/neural/minilm`. The official [ONNX Runtime 1.23.1](https://github.com/microsoft/onnxruntime/tree/v1.23.1) universal macOS runtime is under MIT; its LICENSE and ThirdPartyNotices.txt ship in `Contents/Resources/neural/onnxruntime`. `native/tools/fetch-neural-assets.py` pins upstream revisions and verifies the model and runtime archive checksums. Development reference generation uses upstream tokenizer/ONNX implementations; these Python packages are not part of the instrument's runtime.

`native/drummer/src/UnicodeMarks.h` is generated from Python's `unicodedata` Unicode 15.0.0 nonspacing-mark categories by `native/tools/generate-unicode-marks.py`. The tokenizer category version is part of embedding-cache identity. See the [Python Unicode database documentation](https://docs.python.org/3.12/library/unicodedata.html) and [Unicode data license](https://www.unicode.org/license.txt) for the data's provenance and terms.

MIT License

Copyright (c) 2026 Anthony Germaneri

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
