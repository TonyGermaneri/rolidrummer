#!/usr/bin/env python3
"""Check the actual ZIPs users download, including a clean macOS extraction."""
import argparse
import json
from pathlib import Path
import plistlib
import stat
import subprocess
import tempfile
import zipfile

TOOLS = Path(__file__).resolve().parent
FORMATS = [('AU', 'RoliDrummer.component', 'BNDL'),
           ('VST3', 'RoliDrummer.vst3', 'BNDL'),
           ('Standalone', 'RoliDrummer.app', 'APPL')]


def verify(directory, require_notarized=False):
    archives = sorted(directory.glob('*.zip'))
    if len(archives) != len(FORMATS):
        raise RuntimeError('Expected exactly three release ZIPs in a clean output directory')
    subprocess.run(['shasum', '-a', '256', '-c', 'SHA256SUMS'], cwd=directory, check=True)
    report = []
    with tempfile.TemporaryDirectory(prefix='rolidrummer-archive-check-') as temporary:
        bundles = []
        for kind, name, package_type in FORMATS:
            matches = [p for p in archives if p.name.startswith(name + '-v')]
            if len(matches) != 1:
                raise RuntimeError(f'{kind}: missing or ambiguous release archive')
            archive = matches[0]
            with zipfile.ZipFile(archive) as source:
                names = [p.filename for p in source.infolist() if not p.filename.startswith('__MACOSX/')]
                if not names or {n.split('/')[0] for n in names} != {name}:
                    raise RuntimeError(f'{kind}: archive must expose {name} directly, without a wrapper folder')
                if any(n.startswith('/') or '..' in n.split('/') for n in names):
                    raise RuntimeError(f'{kind}: invalid archive path')
            extracted = Path(temporary) / kind
            extracted.mkdir()
            subprocess.run(['ditto', '-x', '-k', str(archive), str(extracted)], check=True)
            bundle = extracted / name
            meta = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
            if meta.get('CFBundleIdentifier') != 'com.wavecontour.rolidrummer' or meta.get('CFBundleExecutable') != 'Roli Drummer':
                raise RuntimeError(f'{kind}: distribution changed the plug-in identity or executable name')
            if kind == 'AU' and [(c.get('type'), c.get('subtype'), c.get('manufacturer'))
                                  for c in meta.get('AudioComponents', [])] != [('aumu', 'Rld1', 'WvCt')]:
                raise RuntimeError('AU: distribution changed its component codes')
            if meta.get('CFBundlePackageType') != package_type:
                raise RuntimeError(f'{kind}: wrong bundle package type')
            if (bundle / 'Contents/PkgInfo').read_bytes()[:4] != package_type.encode():
                raise RuntimeError(f'{kind}: missing or inconsistent PkgInfo')
            executable = bundle / 'Contents/MacOS' / meta['CFBundleExecutable']
            if not executable.is_file() or not executable.stat().st_mode & stat.S_IXUSR:
                raise RuntimeError(f'{kind}: executable or its permissions were lost in extraction')
            expected = name + '-v' + meta['CFBundleShortVersionString'] + '.zip'
            if archive.name != expected:
                raise RuntimeError(f'{kind}: archive and bundle versions do not match')
            subprocess.run(['codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)
            if require_notarized:
                subprocess.run(['xcrun', 'stapler', 'validate', str(bundle)], check=True)
            bundles.append(bundle)
            report.append({'format': kind, 'archive': archive.name, 'version': meta['CFBundleShortVersionString'],
                           'singleBundleRoot': True, 'extractedSignature': True, 'finderPackage': True,
                           'extractedNotarization': require_notarized})
        subprocess.run(['xcrun', 'swift', str(TOOLS / 'verify-macos-packages.swift'), *map(str, bundles)], check=True)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--require-notarized', action='store_true')
    arguments = parser.parse_args()
    print(json.dumps({'archives': verify(arguments.directory.resolve(), arguments.require_notarized)}, indent=2))
