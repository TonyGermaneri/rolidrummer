#!/usr/bin/env python3
"""Fetch pinned public MiniLM and ONNX Runtime assets for the native instrument."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1] / "neural-assets"
MODEL_REVISION = "1110a243fdf4706b3f48f1d95db1a4f5529b4d41"
MODEL_SHA = "6fd5d72fe4589f189f8ebc006442dbb529bb7ce38f8082112682524616046452"
SMALL_FILES_SHA = {'vocab.txt': '07eced375cec144d27c900241f3e339478dec958f92fddbc551f295c992038a3', 'tokenizer.json': 'be50c3628f2bf5bb5e3a7f17b1f74611b2561a3a27eeab05e5aa30f411572037', 'tokenizer_config.json': 'acb92769e8195aabd29b7b2137a9e6d6e25c476a4f15aa4355c233426c61576b', 'config.json': '953f9c0d463486b10a6871cc2fd59f223b2c70184f49815e7efbcab5d8908b41', 'pooling.json': '4be450dde3b0273bb9787637cfbd28fe04a7ba6ab9d36ac48e92b11e350ffc23'}
RUNTIME_VERSION = "1.23.1"
RUNTIME_ARCHIVE = f"onnxruntime-osx-universal2-{RUNTIME_VERSION}.tgz"
RUNTIME_SHA = "e9fcd200bcf69377cc2c15e529368ca840d625669f34b1cf296a56b175665fc2"


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fetch(url, destination, expected=None):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and (expected is None or sha(destination) == expected):
        return
    partial = destination.with_name(destination.name + ".partial")
    print(f"Downloading {destination.name}", flush=True)
    request = urllib.request.Request(url, headers={"User-Agent": "RoliDrummer-native-assets"})
    with urllib.request.urlopen(request, timeout=60) as source, partial.open("wb") as target:
        shutil.copyfileobj(source, target, length=1024 * 1024)
    if expected and sha(partial) != expected:
        raise RuntimeError(f"Checksum mismatch for {destination.name}; the partial download was not installed")
    partial.replace(destination)


def main():
    model = ROOT / "minilm"
    base = f"https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/{MODEL_REVISION}/"
    for source, name in [
        ("onnx/model.onnx", "model.onnx"),
        ("vocab.txt", "vocab.txt"),
        ("tokenizer.json", "tokenizer.json"),
        ("tokenizer_config.json", "tokenizer_config.json"),
        ("config.json", "config.json"),
        ("1_Pooling/config.json", "pooling.json"),
        ("README.md", "MODEL_CARD.md"),
    ]:
        fetch(base + source, model / name, MODEL_SHA if name == "model.onnx" else SMALL_FILES_SHA.get(name))
    fetch("https://www.apache.org/licenses/LICENSE-2.0.txt", model / "LICENSE.txt")
    archive = ROOT / RUNTIME_ARCHIVE
    fetch(f"https://github.com/microsoft/onnxruntime/releases/download/v{RUNTIME_VERSION}/{RUNTIME_ARCHIVE}", archive, RUNTIME_SHA)
    runtime = ROOT / "onnxruntime"
    with tarfile.open(archive, "r:gz") as package:
        for member in package:
            relative = PurePosixPath(*PurePosixPath(member.name).parts[1:])
            if not member.isfile() or relative.is_absolute() or ".." in relative.parts:
                continue
            if not (str(relative).startswith("include/") or (str(relative).startswith("lib/") and str(relative).endswith(".dylib")) or relative.name in ("LICENSE", "ThirdPartyNotices.txt", "VERSION_NUMBER")):
                continue
            destination = runtime.joinpath(*relative.parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            source = package.extractfile(member)
            if source is None:
                raise RuntimeError(f"Cannot read runtime asset {relative}")
            with source, destination.open("wb") as output:
                shutil.copyfileobj(source, output)
    manifest = {
        "version": 1,
        "model": "sentence-transformers/all-MiniLM-L6-v2",
        "revision": MODEL_REVISION,
        "dimensions": 384,
        "maxTokens": 256,
        "pooling": "attention-mask-mean-l2",
        "files": {name: sha(model / name) for name in ("model.onnx", "vocab.txt", "tokenizer.json", "tokenizer_config.json", "config.json", "pooling.json")},
        "runtime": {"version": RUNTIME_VERSION, "archiveSha256": RUNTIME_SHA, "platform": "osx-universal2"},
    }
    (ROOT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Verified assets saved to {ROOT}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Neural asset download failed: {error}", file=sys.stderr)
        sys.exit(1)
