#!/usr/bin/env bash
#
# Notarise the signed bundles, staple the tickets, and package what comes out.
#
# Notarisation authenticates one of two ways, and which one you use depends on what Apple gave
# you rather than on preference:
#
#   App Store Connect API key   NOTARY_KEY_P8 / NOTARY_KEY_ID / NOTARY_ISSUER_ID
#     A .p8 downloaded once from App Store Connect. Preferred in CI: scoped to notarisation,
#     revocable on its own, and not tied to a person's Apple ID.
#
#   Apple ID + app-specific password   NOTARY_APPLE_ID / NOTARY_PASSWORD / NOTARY_TEAM_ID
#     The fallback, and what you already have if you have a developer account at all. The
#     password is an app-specific one from appleid.apple.com — never the account password.
#
# The API key wins if both are set. Signing (the .p12) and notarising are separate credentials
# for separate steps; having one does not give you the other.
#
#   native/tools/macos-notarize.sh <plugin-build-dir> [output-dir] [configuration]
#
# Written for bash 3.2 — see the note in macos-sign.sh.

set -euo pipefail

plugins="${1:-}"
outdir="${2:-dist}"
config="${3:-Release}"

if [ -z "$plugins" ] || [ ! -d "$plugins" ]; then
    echo "usage: $0 <plugin-build-dir> [output-dir] [configuration]" >&2
    exit 2
fi

work=""
key_file=""
cleanup() {
    [ -n "$key_file" ] && rm -f "$key_file"
    [ -n "$work" ] && rm -rf "$work"
    return 0
}
trap cleanup EXIT

instrument="$plugins/RoliDrummer_artefacts/$config"

bundles=()
for candidate in "$instrument/AU/Roli Drummer.component" \
                 "$instrument/VST3/Roli Drummer.vst3" \
                 "$instrument/Standalone/Roli Drummer.app"; do
    [ -e "$candidate" ] && bundles+=("$candidate")
done
if [ "${#bundles[@]}" -eq 0 ]; then
    echo "error: nothing to notarise in $plugins" >&2
    exit 1
fi

# --------------------------------------------------------------------------------- credentials
notary_args=()
if [ -n "${NOTARY_KEY_PATH:-}" ] && [ -n "${NOTARY_KEY_ID:-}" ] && [ -n "${NOTARY_ISSUER_ID:-}" ]; then
    # A .p8 already on disk — the natural shape for a local run, where the file is simply there.
    [ -r "$NOTARY_KEY_PATH" ] || { echo "error: cannot read $NOTARY_KEY_PATH" >&2; exit 1; }
    notary_args=(--key "$NOTARY_KEY_PATH" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER_ID")
    echo "authenticating with an App Store Connect API key (from a file)"

elif [ -n "${NOTARY_KEY_P8:-}" ] && [ -n "${NOTARY_KEY_ID:-}" ] && [ -n "${NOTARY_ISSUER_ID:-}" ]; then
    # The same key, base64 in an environment variable — the shape a secret store can hold. It is
    # written with a restrictive umask and removed by the trap above however this exits.
    key_file="$(mktemp "${TMPDIR:-/tmp}/notary-XXXXXX.p8")"
    chmod 600 "$key_file"
    printf '%s' "$NOTARY_KEY_P8" | base64 --decode > "$key_file"
    notary_args=(--key "$key_file" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER_ID")
    echo "authenticating with an App Store Connect API key"

elif [ -n "${NOTARY_APPLE_ID:-}" ] && [ -n "${NOTARY_PASSWORD:-}" ] && [ -n "${NOTARY_TEAM_ID:-}" ]; then
    notary_args=(--apple-id "$NOTARY_APPLE_ID" --password "$NOTARY_PASSWORD" --team-id "$NOTARY_TEAM_ID")
    echo "authenticating as $NOTARY_APPLE_ID"

else
    echo "error: no notarisation credentials." >&2
    echo "       Either NOTARY_KEY_P8 + NOTARY_KEY_ID + NOTARY_ISSUER_ID," >&2
    echo "       or NOTARY_APPLE_ID + NOTARY_PASSWORD + NOTARY_TEAM_ID." >&2
    exit 1
fi

# --------------------------------------------------------------------------------- submit
#
# Everything goes up in one archive. Notarisation is a round trip to Apple measured in minutes,
# and three of them in series is three times the wait for the same answer.
work="$(mktemp -d "${TMPDIR:-/tmp}/rolidrummer-notarize-XXXXXX")"
archive="$work/rolidrummer.zip"

echo
echo "submitting:"
printf '  %s\n' "${bundles[@]##*/}"

# `ditto -c -k --keepParent` is the archiver notarytool expects; zip(1) does not preserve the
# symlinks and extended attributes a signed bundle is made of.
ditto -c -k --keepParent --sequesterRsrc "${bundles[@]}" "$archive" 2>/dev/null || {
    # ditto takes one source; several bundles go via a staging directory.
    staging="$work/staging"
    mkdir -p "$staging"
    for bundle in "${bundles[@]}"; do
        ditto "$bundle" "$staging/$(basename "$bundle")"
    done
    ditto -c -k --keepParent --sequesterRsrc "$staging" "$archive"
}

echo
xcrun notarytool submit "$archive" ${notary_args[@]+"${notary_args[@]}"} --wait --timeout 30m

# --------------------------------------------------------------------------------- staple
#
# The ticket is attached to each bundle so it verifies with no network at all. Without stapling,
# a machine that is offline the first time it sees the plugin cannot check it and refuses.
# Apple says "Accepted" before every server stapler asks has the ticket: a staple tried at once can
# fail with the ticket simply not there yet (v0.6.1's did, where v0.6.0's same build had not). So
# each is tried again over a few minutes, and if it still fails, stapler's own words are shown.
staple() {
    local bundle="$1" out="" attempt
    for attempt in 1 2 3 4 5 6; do
        out="$(xcrun stapler staple "$bundle" 2>&1)" && return 0
        [ "$attempt" -lt 6 ] && sleep $((attempt < 4 ? attempt * 15 : 60))
    done
    echo "FAILED"
    printf '%s\n' "$out" | sed 's/^/    /' >&2
    return 1
}
echo
for bundle in "${bundles[@]}"; do
    printf '  staple %-26s ' "$(basename "$bundle")"
    staple "$bundle" || exit 1
    echo "ok"
done

echo
echo "Gatekeeper assessment (post-notarisation — 'accepted' is the answer now):"
for bundle in "${bundles[@]}"; do
    printf '  %-26s ' "$(basename "$bundle")"
    spctl --assess --type install --verbose=2 "$bundle" 2>&1 | tail -1 | sed 's/^[[:space:]]*//' || true
done

# --------------------------------------------------------------------------------- package
mkdir -p "$outdir"
version="${VERSION:-dev}"

for bundle in "${bundles[@]}"; do
    name="$(basename "$bundle")"
    ditto -c -k --keepParent --sequesterRsrc "$bundle" "$outdir/${name}-${version}.zip"
done

echo
echo "packaged into $outdir:"
ls -1 "$outdir"
