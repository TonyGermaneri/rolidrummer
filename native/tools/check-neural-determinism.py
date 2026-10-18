#!/usr/bin/env python3
"""Compare real catalog resolution across macOS architectures and search providers.

Development-only orchestration; no Python runs in the instrument. Each native
invocation also compiles/exports its patch and renders synthetic MPE to audio.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--db", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--queries", nargs="+", default=["Heavy Rock", "Bossa Nova", "Jazz Brushes"])
    parser.add_argument("--variations", type=int, choices=range(5), default=2)
    parser.add_argument("--seed", default="determinism-session-21")
    parser.add_argument("--cpu-reference", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    summary_path = args.output / "determinism-report.json"
    summary_path.write_text(json.dumps({"state": "running", "sameAllDocuments": False}) + "\n")
    cases = [("arm64", "gpu"), ("x86_64", "gpu")]
    if args.cpu_reference:
        cases.append(("arm64", "cpu"))
    reports = []
    for query in args.queries:
        slug = hashlib.sha256(query.encode()).hexdigest()[:12]
        documents, runs = [], []
        for architecture, provider in cases:
            folder = args.output / slug / f"{architecture}-{provider}"
            folder.mkdir(parents=True, exist_ok=True)
            environment = dict(os.environ, RD_DATA_DIR=str((args.output / "state").resolve()))
            command = ["arch", f"-{architecture}", str(args.renderer.resolve()), str(args.db.resolve()), query,
                       str(folder.resolve()), str(args.variations), args.seed, provider]
            started = time.monotonic()
            result = subprocess.run(command, capture_output=True, text=True, env=environment)
            (folder / "process.log").write_text(result.stderr)
            if result.returncode:
                raise RuntimeError(f"{query}: {architecture}/{provider} exited {result.returncode}: {result.stderr[-1500:]}")
            report = json.loads(result.stdout)
            (folder / "process-report.json").write_text(json.dumps(report, indent=2) + "\n")
            document = Path(report["document"]).read_bytes()
            documents.append(document)
            runs.append({"architecture": architecture, "provider": provider,
                         "document": report["document"], "sha256": hashlib.sha256(document).hexdigest(),
                         "seconds": time.monotonic() - started, "peak": report["peak"],
                         "indexId": report["indexId"], "compiledClips": report["compiledClips"]})
            print(f"{query}: {architecture}/{provider} rendered", flush=True)
        same = all(document == documents[0] for document in documents[1:])
        reports.append({"query": query, "identicalDocumentBytes": same, "runs": runs})
        summary = {"state": "complete" if same and len(reports) == len(args.queries) else "running" if same else "failed",
                   "sameAllDocuments": all(report["identicalDocumentBytes"] for report in reports),
                   "seed": args.seed, "variations": args.variations, "queries": reports,
                   "scope": "Exact resolved documents; rendered audio is checked for finite, nonzero output, not bit identity."}
        summary_path.write_text(json.dumps(summary, indent=2) + "\n")
        if not same:
            raise RuntimeError(f"{query}: resolved document bytes differ; inspect {summary_path}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
