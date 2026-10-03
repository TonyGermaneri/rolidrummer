#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/native/build-universal}"
out="${2:-$root/release}"
artifacts="$build/plugin/RoliDrummer_artefacts/Release"
version="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$artifacts/AU/Roli Drummer.component/Contents/Info.plist")"
mkdir -p "$out"
for item in 'AU/Roli Drummer.component' 'VST3/Roli Drummer.vst3' 'Standalone/Roli Drummer.app'; do
 bundle="$artifacts/$item"
 codesign --verify --strict "$bundle"
 architectures="$(/usr/bin/lipo -archs "$bundle/Contents/MacOS/Roli Drummer")"
 for architecture in arm64 x86_64; do
  case " $architectures " in
   *" $architecture "*) ;;
   *) echo "Missing $architecture slice in $bundle" >&2; exit 1;;
  esac
 done
 name="${bundle##*/}"
 ditto -c -k --keepParent --sequesterRsrc "$bundle" "$out/$name-$version-macOS-universal.zip"
done
cp "$root/native/packaging/release-notes.md" "$out/README.md"
cp "$root/LICENSE" "$root/THIRD_PARTY.md" "$root/native/packaging/SIGNING.md" "$out/"
(cd "$out" && shasum -a 256 ./*.zip > SHA256SUMS)
