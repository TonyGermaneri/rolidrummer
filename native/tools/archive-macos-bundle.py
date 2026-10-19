#!/usr/bin/env python3
"""Create a release archive with the same stable basename as its bundle."""
import argparse
from pathlib import Path
import plistlib
import re
import subprocess
import tempfile


def archive(bundle, directory):
    bundle = bundle.resolve()
    if bundle.name not in {'Roli Drummer.component', 'Roli Drummer.vst3', 'Roli Drummer.app'}:
        raise ValueError(f'Unexpected product: {bundle.name}')
    metadata = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
    version = metadata['CFBundleShortVersionString']
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError(f'Invalid release version: {version}')
    name = 'RoliDrummer' + bundle.suffix
    directory = directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    destination = directory / f'{name}-v{version}.zip'
    with tempfile.TemporaryDirectory(prefix='rolidrummer-distribution-') as temporary:
        staged = Path(temporary) / name
        # Rename only the distribution directory. The executable, AU component
        # codes, bundle identifier, signature and stapled ticket stay intact.
        subprocess.run(['ditto', str(bundle), str(staged)], check=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(staged)], check=True)
        subprocess.run(['ditto', '-c', '-k', '--keepParent', '--sequesterRsrc',
                        str(staged), str(destination)], check=True)
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    print(archive(args.bundle, args.directory))
