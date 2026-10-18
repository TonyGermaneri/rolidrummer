#!/usr/bin/env python3
"""Verify macOS products before packaging: signatures, slices and shipped assets."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require(value, message):
    if not value:
        raise RuntimeError(message)


def verify(args):
    manifest = json.loads((args.assets / "manifest.json").read_text())
    web = {p.relative_to(args.web_dist) for p in args.web_dist.rglob("*")
           if p.is_file() and p.suffix not in {".eot", ".ttf", ".woff"}}
    require(Path("index.html") in web, "Build the editor before checking bundles")
    results = []
    for kind, name in [("AU", "Roli Drummer.component"), ("VST3", "Roli Drummer.vst3"),
                       ("Standalone", "Roli Drummer.app")]:
        bundle = args.artifacts / kind / name
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)],
                       check=True, capture_output=True, text=True)
        resources = bundle / "Contents" / "Resources"
        neural = resources / "neural"
        require(json.loads((neural / "manifest.json").read_text()) == manifest,
                f"{kind}: bundled neural manifest differs from pinned assets")
        runtime = neural / "onnxruntime/lib" / f"libonnxruntime.{manifest['runtime']['version']}.dylib"
        architectures = []
        for executable in [bundle / "Contents/MacOS/Roli Drummer", runtime]:
            slices = subprocess.check_output(["lipo", "-archs", str(executable)], text=True).split()
            require({"arm64", "x86_64"} <= set(slices), f"{kind}: missing universal slice in {executable}")
            architectures.append(slices)
        actual = {p.relative_to(resources / "web") for p in (resources / "web").rglob("*") if p.is_file()}
        require(actual == web, f"{kind}: bundled editor has missing or stale files")
        for relative in sorted(web):
            require(sha(args.web_dist / relative) == sha(resources / "web" / relative),
                    f"{kind}: stale editor file {relative}")
        for filename, checksum in manifest["files"].items():
            require(sha(neural / "minilm" / filename) == checksum,
                    f"{kind}: model/tokenizer checksum differs for {filename}")
        for relative in ["minilm/LICENSE.txt", "minilm/MODEL_CARD.md", "onnxruntime/LICENSE",
                         "onnxruntime/ThirdPartyNotices.txt", "onnxruntime/VERSION_NUMBER"]:
            require(sha(neural / relative) == sha(args.assets / relative),
                    f"{kind}: missing or changed notice/version {relative}")
        require((neural / "onnxruntime/VERSION_NUMBER").read_text().strip() == manifest["runtime"]["version"],
                f"{kind}: runtime version differs from the pinned manifest")
        results.append({"format": kind, "architecture": architectures[0], "runtimeArchitecture": architectures[1],
                        "deepSignature": True, "webFiles": len(web), "webMatchesBuild": True,
                        "modelFilesMatchPinnedManifest": True, "licensesMatchAssets": True})
    return {"state": "complete", "formats": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", nargs="?", type=Path,
                        default=ROOT / "native/build-universal/plugin/RoliDrummer_artefacts/Release")
    parser.add_argument("--assets", type=Path, default=ROOT / "native/neural-assets")
    parser.add_argument("--web-dist", type=Path, default=ROOT / "dist")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({"state": "running"}) + "\n")
    try:
        report = verify(args)
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, subprocess.SubprocessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        report = {"state": "failed", "error": str(detail)}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["state"] == "complete" else 1


if __name__ == "__main__":
    sys.exit(main())
