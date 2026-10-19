#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/native/build-universal}"
out="${2:-$root/release}"
notarized="${3:-}"
artifacts="$build/plugin/RoliDrummer_artefacts/Release"
python3 "$root/native/tools/verify-neural-bundles.py" "$artifacts"
version="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$artifacts/AU/Roli Drummer.component/Contents/Info.plist")"
mkdir -p "$out"
if [ "${NOTARIZED:-false}" = true ] && [ ! -d "$notarized" ]; then
 echo 'Notarized releases must publish the archives produced by macos-notarize.sh' >&2
 exit 1
fi
for item in 'AU/Roli Drummer.component' 'VST3/Roli Drummer.vst3' 'Standalone/Roli Drummer.app'; do
 bundle="$artifacts/$item"
 codesign --verify --strict "$bundle"
 neural="$bundle/Contents/Resources/neural"
 test -s "$neural/manifest.json"
 test -s "$neural/minilm/model.onnx"
 test -s "$neural/minilm/vocab.txt"
 runtime="$neural/onnxruntime/lib/libonnxruntime.1.23.1.dylib"
 codesign --verify --strict "$runtime"
 for executable in "$bundle/Contents/MacOS/Roli Drummer" "$runtime"; do
 architectures="$(/usr/bin/lipo -archs "$executable")"
 for architecture in arm64 x86_64; do
  case " $architectures " in
   *" $architecture "*) ;;
   *) echo "Missing $architecture slice in $executable" >&2; exit 1;;
  esac
 done
 done
 name="${bundle##*/}"
 archive_name="${name// /}-v$version.zip"
 if [ -n "$notarized" ]; then
  # Match SSB: publish the original notarized ZIP, without another archive pass.
  cp "$notarized/$archive_name" "$out/$archive_name"
  cmp "$notarized/$archive_name" "$out/$archive_name"
 else
  python3 "$root/native/tools/archive-macos-bundle.py" "$bundle" "$out"
 fi
done
cp "$root/native/packaging/release-notes.md" "$out/README.md"
cp "$root/LICENSE" "$root/THIRD_PARTY.md" "$root/native/packaging/SIGNING.md" "$out/"
(cd "$out" && shasum -a 256 ./*.zip > SHA256SUMS)

# Validate a fresh extraction, not only the build directory.
if [ "${NOTARIZED:-false}" = true ]; then
 python3 "$root/native/tools/verify-macos-archives.py" "$out" --require-notarized
else
 python3 "$root/native/tools/verify-macos-archives.py" "$out"
fi
