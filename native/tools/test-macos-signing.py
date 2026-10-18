#!/usr/bin/env python3
"""Exercise the real workflow shell and signing script without using a keychain."""
import base64
import json
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/native.yml"

# Model the external command boundary: codesign needs the imported keychain in
# its search list even when --keychain explicitly selects the signing identity.
# All files, identities and credentials here are synthetic.
MOCK_TOOL = r'''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
path = Path(os.environ["RD_SIGNING_TEST_STATE"])
state = json.loads(path.read_text())
tool = Path(sys.argv[0]).name
args = sys.argv[1:]
state["calls"].append([tool, *args])
status = 0
if tool == "security":
    if args[0] == "list-keychains":
        if "-s" in args:
            state["search"] = args[args.index("-s") + 1:]
        elif os.environ.get("FAIL_LIST_QUERY"):
            status = 1
        else:
            for entry in state["search"]:
                print('    "' + entry + '"')
    elif args[0] == "create-keychain":
        if os.environ.get("AUTO_ADD_KEYCHAIN"):
            state["search"].append(args[-1])
    elif args[0] == "delete-keychain":
        state["search"] = [entry for entry in state["search"] if entry != args[-1]]
        state["deleted"].append(args[-1])
    elif args[0] == "import" and os.environ.get("FAIL_IMPORT"):
        status = 1
    elif args[0] == "find-identity":
        if os.environ.get("NO_VALID_IDENTITY"):
            print("    0 valid identities found")
        else:
            print('  1) ' + "A" * 40 + ' "Developer ID Application: Test (EXAMPLE)"')
            print("    1 valid identities found")
elif tool == "codesign" and "--sign" in args:
    if os.environ.get("KEYCHAIN_PATH") not in state["search"]:
        print("error: The specified item could not be found in the keychain.", file=sys.stderr)
        status = 1
    elif os.environ.get("FAIL_SIGN"):
        status = 1
    else:
        state["signed"].append(args[-1])
elif tool == "spctl":
    print("rejected: not notarized")
    status = 1
path.write_text(json.dumps(state))
sys.exit(status)
'''


def workflow_shell(name):
    """Read the actual run block; no second copy of the setup/cleanup logic."""
    lines = WORKFLOW.read_text().splitlines()
    first = lines.index("      - name: " + name) + 1
    last = next((i for i in range(first, len(lines))
                 if lines[i].startswith("      - ")), len(lines))
    run = lines.index("        run: |", first, last) + 1
    last = next((i for i in range(run, last)
                 if lines[i].strip() and not lines[i].startswith("          ")), last)
    return textwrap.dedent("\n".join(lines[run:last])) + "\n"


class SigningWorkflowTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="rd signing test ")
        self.addCleanup(self.scratch.cleanup)
        self.folder = Path(self.scratch.name)
        self.original = [
            "/runner/Library/Keychains/login.keychain-db",
            "/runner/keychains/team signing (legacy).keychain-db",
        ]
        self.state_file = self.folder / "state.json"
        self.state_file.write_text(json.dumps({
            "search": self.original, "calls": [], "signed": [], "deleted": [],
        }))
        commands = self.folder / "commands"
        commands.mkdir()
        for name in ("security", "codesign", "spctl"):
            path = commands / name
            path.write_text(MOCK_TOOL)
            path.chmod(0o755)
        self.env = dict(os.environ)
        self.env.pop("KEYCHAIN_PATH", None)
        self.env.update(
            PATH=str(commands) + os.pathsep + self.env["PATH"],
            RD_SIGNING_TEST_STATE=str(self.state_file),
            RUNNER_TEMP=str(self.folder),
            GITHUB_ENV=str(self.folder / "github-env"),
            MACOS_CERTIFICATE_P12=base64.b64encode(b"synthetic p12").decode(),
            MACOS_CERTIFICATE_PASSWORD="synthetic password",
            MACOS_SIGN_IDENTITY="Developer ID Application: Test (EXAMPLE)",
        )
        self.keychain = str(self.folder / "rolidrummer-signing.keychain-db")
        self.plugins = self.folder / "plugin"
        self.bundles = []
        for kind, name in (("AU", "Roli Drummer.component"),
                           ("VST3", "Roli Drummer.vst3"),
                           ("Standalone", "Roli Drummer.app")):
            bundle = self.plugins / "RoliDrummer_artefacts/Release" / kind / name
            runtime = bundle / "Contents/Resources/neural/onnxruntime/lib/libonnxruntime.1.23.1.dylib"
            runtime.parent.mkdir(parents=True)
            runtime.write_bytes(b"synthetic runtime")
            self.bundles.append((str(runtime), str(bundle)))

    def state(self):
        return json.loads(self.state_file.read_text())

    def step(self, name):
        result = subprocess.run(
            ["/bin/bash", "-e", "-o", "pipefail", "-c", workflow_shell(name)],
            cwd=ROOT, env=self.env, capture_output=True, text=True, timeout=30,
        )
        exported = Path(self.env["GITHUB_ENV"])
        if exported.exists():
            for line in exported.read_text().splitlines():
                key, value = line.split("=", 1)
                self.env[key] = value
        return result

    def sign(self):
        return subprocess.run(
            ["/bin/bash", str(ROOT / "native/tools/macos-sign.sh"), str(self.plugins), "Release"],
            cwd=ROOT, env=self.env, capture_output=True, text=True, timeout=30,
        )

    def success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def cleanup_keychain(self):
        self.success(self.step("Remove signing material"))
        self.assertEqual(self.state()["search"], self.original)
        self.assertIn(self.keychain, self.state()["deleted"])
        self.assertFalse((self.folder / "rolidrummer-certificate.p12").exists())

    def test_import_sign_and_cleanup_preserve_keychains_and_nested_order(self):
        self.success(self.step("Import signing certificate"))
        self.assertEqual(self.state()["search"], [self.keychain, *self.original])
        self.assertEqual(self.env["KEYCHAIN_PATH"], self.keychain)
        self.assertFalse((self.folder / "rolidrummer-certificate.p12").exists())
        self.success(self.sign())
        self.assertEqual(self.state()["signed"], [p for pair in self.bundles for p in pair])
        for tool, *args in self.state()["calls"]:
            if tool == "codesign" and "--sign" in args:
                self.assertEqual(args[args.index("--keychain") + 1], self.keychain)
                self.assertEqual(args[args.index("--sign") + 1], self.env["MACOS_SIGN_IDENTITY"])
                self.assertIn("--timestamp", args)
                self.assertEqual(args[args.index("--options") + 1], "runtime")
        self.cleanup_keychain()

    def test_restores_search_list_after_signing_fails(self):
        self.success(self.step("Import signing certificate"))
        self.env["FAIL_SIGN"] = "1"
        self.assertNotEqual(self.sign().returncode, 0)
        self.cleanup_keychain()

    def test_import_failure_removes_p12_and_preserves_original_keychains(self):
        self.env["FAIL_IMPORT"] = "1"
        self.assertNotEqual(self.step("Import signing certificate").returncode, 0)
        self.assertFalse((self.folder / "rolidrummer-certificate.p12").exists())
        self.assertFalse(self.state()["signed"])
        self.cleanup_keychain()

    def test_missing_identity_fails_before_signing(self):
        self.env["NO_VALID_IDENTITY"] = "1"
        self.assertNotEqual(self.step("Import signing certificate").returncode, 0)
        self.assertFalse(self.state()["signed"])
        self.cleanup_keychain()

    def test_empty_original_search_list_is_restored(self):
        self.original = []
        state = self.state()
        state["search"] = []
        self.state_file.write_text(json.dumps(state))
        self.success(self.step("Import signing certificate"))
        self.assertEqual(self.state()["search"], [self.keychain])
        self.cleanup_keychain()

    def test_keychain_auto_added_by_creation_is_not_duplicated_or_preserved(self):
        self.env["AUTO_ADD_KEYCHAIN"] = "1"
        self.success(self.step("Import signing certificate"))
        self.assertEqual(self.state()["search"], [self.keychain, *self.original])
        self.cleanup_keychain()

    def test_search_list_query_failure_does_not_publish_an_empty_snapshot(self):
        self.env["FAIL_LIST_QUERY"] = "1"
        self.assertNotEqual(self.step("Import signing certificate").returncode, 0)
        self.assertFalse((self.folder / "rolidrummer-keychain-search-list").exists())
        self.cleanup_keychain()


if __name__ == "__main__":
    unittest.main()
