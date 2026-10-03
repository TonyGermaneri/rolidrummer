#!/usr/bin/env python3
"""Install the built AU for local validation, preserving an existing development copy."""
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).resolve()
identity = 'com.wavecontour.rolidrummer'
with (source / 'Contents/Info.plist').open('rb') as stream:
    if plistlib.load(stream).get('CFBundleIdentifier') != identity:
        raise SystemExit('Source is not the Roli Drummer AU')
subprocess.run(['codesign', '--verify', '--strict', str(source)], check=True)
destination = Path.home() / 'Library/Audio/Plug-Ins/Components/Roli Drummer.component'
destination.parent.mkdir(parents=True, exist_ok=True)
if destination.exists():
    with (destination / 'Contents/Info.plist').open('rb') as stream:
        if plistlib.load(stream).get('CFBundleIdentifier') != identity:
            raise SystemExit('A different plug-in occupies the destination')
staging = Path(tempfile.mkdtemp(prefix='rolidrummer-install-', dir=destination.parent))
prepared = staging / destination.name
backup = None
try:
    subprocess.run(['ditto', str(source), str(prepared)], check=True)
    subprocess.run(['codesign', '--verify', '--strict', str(prepared)], check=True)
    if destination.exists() or destination.is_symlink():
        backup = Path(tempfile.mkdtemp(prefix='rolidrummer-previous-au-')) / destination.name
        shutil.move(str(destination), str(backup))
    try:
        os.replace(prepared, destination)
    except Exception:
        if backup is not None:
            shutil.move(str(backup), str(destination))
        raise
finally:
    shutil.rmtree(staging)
print(f'Installed: {destination}')
if backup is not None:
    print(f'Previous development copy preserved: {backup}')
