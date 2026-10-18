#!/usr/bin/env python3
"""Development-only reference fixtures; inference in the instrument is native.

Requires tokenizers and onnxruntime. Uses the checksum-pinned downloaded model
and the model publisher's tokenizer.json, not the native tokenizer under test.
"""
import json
from pathlib import Path
import numpy as np
import onnxruntime as ort
from tokenizers import Tokenizer

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "neural-assets"
DATA = ROOT / "drummer/tests/data"
tokenizer = Tokenizer.from_file(str(ASSETS / "minilm/tokenizer.json"))
tokenizer.no_padding()
tokenizer.enable_truncation(max_length=256)
reference_path = DATA / "tokenizer-reference.json"
reference = json.loads(reference_path.read_text())
for case in reference["cases"]:
    case["tokens"] = tokenizer.encode(case["text"]).ids
reference_path.write_text(json.dumps(reference, ensure_ascii=False, indent=2) + "\n")

texts = ["heavy rock drum groove", "hard rock drum beat", "gentle bossa nova percussion", "swinging jazz brushes"]
tokenizer.enable_padding()
encoded = tokenizer.encode_batch(texts)
inputs = {
    "input_ids": np.array([row.ids for row in encoded], dtype=np.int64),
    "attention_mask": np.array([row.attention_mask for row in encoded], dtype=np.int64),
    "token_type_ids": np.array([row.type_ids for row in encoded], dtype=np.int64),
}
session = ort.InferenceSession(str(ASSETS / "minilm/model.onnx"), providers=["CPUExecutionProvider"])
hidden = session.run(None, inputs)[0]
mask = inputs["attention_mask"][:, :, None]
pooled = (hidden * mask).sum(axis=1) / mask.sum(axis=1)
vectors = pooled / np.linalg.norm(pooled, axis=1, keepdims=True)
manifest = json.loads((ASSETS / "manifest.json").read_text())
(DATA / "embedding-reference.json").write_text(json.dumps({
    "modelRevision": manifest["revision"], "modelSha256": manifest["files"]["model.onnx"],
    "runtime": ort.__version__, "pooling": "attention-mask-mean-l2",
    "cases": [{"text": text, "vector": vector.tolist()} for text, vector in zip(texts, vectors)],
}, indent=2) + "\n")
print("Regenerated independent tokenizer and ONNX vector references")
