#!/usr/bin/env python3
"""Exercise notarization/stapling with synthetic credentials and mocked Apple tools."""
import json
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
MOCK = r'''#!/usr/bin/env python3
import json, os, shutil, sys, zipfile
from pathlib import Path
path=Path(os.environ["RD_NOTARY_TEST_STATE"])
state=json.loads(path.read_text())
tool=Path(sys.argv[0]).name
args=sys.argv[1:]
state["calls"].append([tool,*args])
status=0
if tool=="ditto":
    if len(args)>6: status=1
    elif len(args)==2: shutil.copytree(args[0],args[1],dirs_exist_ok=True)
    else:
        source=Path(args[-2])
        with zipfile.ZipFile(args[-1],"w") as output:
            for item in [source,*source.rglob("*")]:
                output.write(item,item.relative_to(source.parent))
elif tool=="xcrun" and args[:2]==["notarytool","submit"]:
    status=int(os.environ.get("FAIL_SUBMIT","0"))
elif tool=="xcrun" and args[:2]==["stapler","staple"]:
    bundle=args[-1]
    count=state["staples"].get(bundle,0)+1
    state["staples"][bundle]=count
    if count<=int(os.environ.get("FAIL_STAPLES","0")):
        print("ticket not yet available",file=sys.stderr)
        status=int(os.environ.get("STAPLE_STATUS","68"))
elif tool=="xcrun" and args[:2]==["stapler","validate"]:
    bundle=args[-1]
    count=state["validations"].get(bundle,0)+1
    state["validations"][bundle]=count
    if count<=int(os.environ.get("FAIL_VALIDATIONS","0")):
        print('NSURLErrorDomain Code=-1005 "The network connection was lost."',file=sys.stderr)
        status=int(os.environ.get("VALIDATE_STATUS","68"))
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
        self.state_path.write_text(json.dumps({"calls": [], "staples": {}, "validations": {}, "delays": []}))
        commands = self.folder / "commands"
        commands.mkdir()
        for tool in ("xcrun", "sleep", "ditto", "spctl", "codesign"):
            path = commands / tool
            path.write_text(MOCK)
            path.chmod(0o755)
        self.plugins = self.folder / "plugin"
        self.bundles = []
        for kind, name in (("AU", "Roli Drummer.component"), ("VST3", "Roli Drummer.vst3"),
                           ("Standalone", "Roli Drummer.app")):
            bundle = self.plugins / "RoliDrummer_artefacts/Release" / kind / name
            (bundle / "Contents/MacOS").mkdir(parents=True)
            (bundle / "Contents/MacOS/Roli Drummer").write_bytes(b"synthetic original executable")
            (bundle / "Contents/Info.plist").write_bytes(plistlib.dumps({
                "CFBundleShortVersionString": "0.2.2", "CFBundleExecutable": "Roli Drummer",
                "CFBundleIdentifier": "com.wavecontour.rolidrummer",
                "AudioComponents": [{"type": "aumu", "subtype": "Rld1", "manufacturer": "WvCt"}],
            }))
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

    def run_ticket(self, action):
        return subprocess.run(["/bin/bash", str(ROOT / "native/tools/macos-ticket.sh"),
                               action, self.bundles[0]], env=self.env,
                              capture_output=True, text=True, timeout=30)

    def test_delayed_tickets_retry_each_bundle_then_package(self):
        self.env["FAIL_STAPLES"] = "2"
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.state()["staples"], {b: 3 for b in self.bundles})
        self.assertEqual(self.state()["delays"], [15, 30] * 3)
        self.assertEqual(len(list(self.out.glob("*.zip"))), 3)
        self.assertFalse(list(self.folder.glob("rolidrummer-notarize-*")))

    def test_release_names_match_bundle_roots_and_preserve_plugin_identity(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual({p.name for p in self.out.glob("*.zip")}, {
            "RoliDrummer.component-v0.2.2.zip", "RoliDrummer.vst3-v0.2.2.zip", "RoliDrummer.app-v0.2.2.zip",
        })
        for path in self.out.glob("*.zip"):
            name = path.name.removesuffix("-v0.2.2.zip")
            with zipfile.ZipFile(path) as archive:
                self.assertEqual({p.split('/')[0] for p in archive.namelist()}, {name})
                metadata = plistlib.loads(archive.read(name + "/Contents/Info.plist"))
                self.assertEqual(metadata["CFBundleExecutable"], "Roli Drummer")
                self.assertEqual(metadata["CFBundleIdentifier"], "com.wavecontour.rolidrummer")
                self.assertEqual(metadata["AudioComponents"], [{"type": "aumu", "subtype": "Rld1", "manufacturer": "WvCt"}])
                self.assertEqual(archive.read(name + "/Contents/MacOS/Roli Drummer"), b"synthetic original executable")

    def test_unavailable_ticket_fails_with_diagnostic_and_no_packages(self):
        self.env["FAIL_STAPLES"] = "99"
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ticket not yet available", result.stderr)
        self.assertEqual(self.state()["staples"], {self.bundles[0]: 12})
        self.assertEqual(self.state()["delays"], [15, 30, 45] + [60] * 8)
        self.assertFalse(self.out.exists())
        self.assertFalse(list(self.folder.glob("rolidrummer-notarize-*")))

    def test_ticket_delivery_can_recover_after_the_previous_retry_limit(self):
        self.env["FAIL_STAPLES"] = "7"
        result = self.run_ticket("staple")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.state()["staples"], {self.bundles[0]: 8})
        self.assertEqual(self.state()["delays"], [15, 30, 45] + [60] * 4)

    def test_validation_retries_cloudkit_connection_loss(self):
        self.env["FAIL_VALIDATIONS"] = "2"
        result = self.run_ticket("validate")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.state()["validations"], {self.bundles[0]: 3})
        self.assertEqual(self.state()["delays"], [15, 30])
        self.assertIn("Code=-1005", result.stderr)

    def test_validation_connection_failure_is_bounded_and_still_fatal(self):
        self.env["FAIL_VALIDATIONS"] = "99"
        result = self.run_ticket("validate")
        self.assertEqual(result.returncode, 68)
        self.assertEqual(self.state()["validations"], {self.bundles[0]: 12})
        self.assertEqual(self.state()["delays"], [15, 30, 45] + [60] * 8)

    def test_invalid_or_revoked_tickets_fail_without_retrying(self):
        self.env["FAIL_VALIDATIONS"] = "99"
        for status in (65, 66, 77):
            with self.subTest(status=status):
                self.env["VALIDATE_STATUS"] = str(status)
                before = len(self.state()["calls"])
                result = self.run_ticket("validate")
                self.assertEqual(result.returncode, status)
                self.assertEqual(len(self.state()["calls"]), before + 1)
                self.assertFalse(self.state()["delays"])

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
