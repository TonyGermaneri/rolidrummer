#!/usr/bin/env python3
"""Exercise notarization/stapling with synthetic credentials and mocked Apple tools."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MOCK = r'''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
path=Path(os.environ["RD_NOTARY_TEST_STATE"])
state=json.loads(path.read_text())
tool=Path(sys.argv[0]).name
args=sys.argv[1:]
state["calls"].append([tool,*args])
status=0
if tool=="ditto":
    if len(args)>6: status=1
    else: Path(args[-1]).write_bytes(b"synthetic archive")
elif tool=="xcrun" and args[:2]==["notarytool","submit"]:
    status=int(os.environ.get("FAIL_SUBMIT","0"))
elif tool=="xcrun" and args[:2]==["stapler","staple"]:
    bundle=args[-1]
    count=state["staples"].get(bundle,0)+1
    state["staples"][bundle]=count
    if count<=int(os.environ.get("FAIL_STAPLES","0")):
        print("ticket not yet available",file=sys.stderr)
        status=1
elif tool=="sleep":
    state["delays"].append(int(args[0]))
path.write_text(json.dumps(state))
sys.exit(status)
'''


class NotarizationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="rd notary test ")
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.state_path = self.folder / "state.json"
        self.state_path.write_text(json.dumps({"calls": [], "staples": {}, "delays": []}))
        commands = self.folder / "commands"
        commands.mkdir()
        for tool in ("xcrun", "sleep", "ditto", "spctl"):
            path = commands / tool
            path.write_text(MOCK)
            path.chmod(0o755)
        self.plugins = self.folder / "plugin"
        self.bundles = []
        for kind, name in (("AU", "Roli Drummer.component"), ("VST3", "Roli Drummer.vst3"),
                           ("Standalone", "Roli Drummer.app")):
            bundle = self.plugins / "RoliDrummer_artefacts/Release" / kind / name
            bundle.mkdir(parents=True)
            self.bundles.append(str(bundle))
        self.env = {k: v for k, v in os.environ.items() if not k.startswith("NOTARY_")}
        self.env.update(PATH=str(commands) + os.pathsep + self.env["PATH"],
                        TMPDIR=str(self.folder), RD_NOTARY_TEST_STATE=str(self.state_path),
                        NOTARY_APPLE_ID="synthetic@example.test", NOTARY_PASSWORD="synthetic",
                        NOTARY_TEAM_ID="EXAMPLE", VERSION="v0.1.0")
        self.out = self.folder / "release"

    def run_script(self):
        return subprocess.run(["/bin/bash", str(ROOT / "native/tools/macos-notarize.sh"),
                               str(self.plugins), str(self.out), "Release"],
                              env=self.env, capture_output=True, text=True, timeout=30)

    def state(self):
        return json.loads(self.state_path.read_text())

    def test_delayed_tickets_retry_each_bundle_then_package(self):
        self.env["FAIL_STAPLES"] = "2"
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.state()["staples"], {b: 3 for b in self.bundles})
        self.assertEqual(self.state()["delays"], [15, 30] * 3)
        self.assertEqual(len(list(self.out.glob("*.zip"))), 3)
        self.assertFalse(list(self.folder.glob("rolidrummer-notarize-*")))

    def test_unavailable_ticket_fails_with_diagnostic_and_no_packages(self):
        self.env["FAIL_STAPLES"] = "99"
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ticket not yet available", result.stderr)
        self.assertEqual(self.state()["staples"], {self.bundles[0]: 6})
        self.assertEqual(self.state()["delays"], [15, 30, 45, 60, 60])
        self.assertFalse(self.out.exists())
        self.assertFalse(list(self.folder.glob("rolidrummer-notarize-*")))

    def test_rejected_submission_never_staples_or_packages(self):
        self.env["FAIL_SUBMIT"] = "1"
        self.assertNotEqual(self.run_script().returncode, 0)
        self.assertFalse(self.state()["staples"])
        self.assertFalse(self.out.exists())

    def test_missing_credentials_never_submits(self):
        for key in tuple(self.env):
            if key.startswith("NOTARY_"):
                del self.env[key]
        self.assertNotEqual(self.run_script().returncode, 0)
        self.assertFalse(self.state()["calls"])


if __name__ == "__main__":
    unittest.main()
