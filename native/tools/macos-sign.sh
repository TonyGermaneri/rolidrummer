#!/usr/bin/env bash
#
# Sign the macOS bundles with a Developer ID Application certificate.
#
# Used unchanged by CI and by hand. CI imports a .p12 into a throwaway keychain first; locally
# your login keychain already holds the identity, so there is nothing to import and the same
# script does the same thing. That symmetry is the point — a signing path that only exists inside
# a workflow is a signing path you cannot debug.
#
#   native/tools/macos-sign.sh native/build/plugin
#
# The identity is discovered from the keychain unless MACOS_SIGN_IDENTITY names one; with more
# than one Developer ID Application certificate installed, discovery refuses to guess.
#
# Written for bash 3.2, which is the bash macOS ships and therefore the bash this will actually
# run under. No mapfile, no associative arrays, and every possibly-empty array expanded through
# the ${arr[@]+"${arr[@]}"} idiom, because `set -u` treats an empty one as unset in 3.2.

set -euo pipefail

plugins="${1:-}"
config="${2:-Release}"
if [ -z "$plugins" ] || [ ! -d "$plugins" ]; then
    echo "usage: $0 <plugin-build-dir> [configuration]" >&2
    echo "  e.g. native/build/plugin" >&2
    exit 2
fi

keychain_args=()
if [ -n "${KEYCHAIN_PATH:-}" ]; then
    keychain_args=(--keychain "$KEYCHAIN_PATH")
fi

# ---------------------------------------------------------------------------------- identity
identity="${MACOS_SIGN_IDENTITY:-}"
if [ -z "$identity" ]; then
    found="$(security find-identity -v -p codesigning ${KEYCHAIN_PATH:+"$KEYCHAIN_PATH"} 2>/dev/null \
        | grep "Developer ID Application" \
        | sed -E 's/.*"(.*)".*/\1/' || true)"
    count=0
    [ -n "$found" ] && count="$(printf '%s\n' "$found" | wc -l | tr -d ' ')"
    if [ "$count" -eq 0 ]; then
        echo "error: no Developer ID Application identity in the keychain." >&2
        echo "       Set MACOS_SIGN_IDENTITY, or import the certificate first." >&2
        exit 1
    elif [ "$count" -gt 1 ]; then
        echo "error: $count Developer ID Application identities found. Name one in" >&2
        echo "       MACOS_SIGN_IDENTITY:" >&2
        printf '         %s\n' "$found" >&2
        exit 1
    fi
    identity="$found"
fi
echo "signing as: $identity"

# ---------------------------------------------------------------------------------- signing
#
# Two flags are not optional and are easy to leave off:
#
#   --options runtime  the hardened runtime. Notarisation rejects anything without it, and the
#                      rejection arrives twenty minutes later inside a JSON log.
#   --timestamp        a secure timestamp from Apple. Without one the signature stops verifying
#                      the day the certificate expires, rather than staying valid for what was
#                      signed while it was live. It needs network access, which is why signing
#                      offline appears to work and then does not.
#
# `entitlements` is optional and nothing passes one: the page's JavaScript runs in WebKit's own
# processes, not in ours, so the hardened runtime has nothing in Roli Drummer to object to. What the bundles
# do need is the hardened runtime itself, which is a codesign flag rather than an entitlement.
sign_bundle() {
    bundle="$1"
    entitlements="${2:-}"
    ent_args=()
    [ -n "$entitlements" ] && ent_args=(--entitlements "$entitlements")
    if [ ! -e "$bundle" ]; then
        echo "  skip  $(basename "$bundle") (not built)"
        return 0
    fi

    # Innermost first: a bundle's signature seals everything inside it, so anything nested that
    # gets signed afterwards breaks that seal.
    if [ -d "$bundle/Contents" ]; then
        find "$bundle/Contents" \
             \( -name '*.dylib' -o -name '*.framework' -o -name '*.bundle' \) -print0 2>/dev/null \
        | while IFS= read -r -d '' nested; do
            echo "  inner $(basename "$nested")"
            codesign --force --options runtime --timestamp \
                     ${keychain_args[@]+"${keychain_args[@]}"} --sign "$identity" "$nested"
        done
    fi

    echo "  sign  $(basename "$bundle")"
    codesign --force --options runtime --timestamp \
             ${ent_args[@]+"${ent_args[@]}"} \
             ${keychain_args[@]+"${keychain_args[@]}"} --sign "$identity" "$bundle"

    codesign --verify --strict --verbose=2 "$bundle" 2>&1 | sed 's/^/        /'
}

# The page goes into these bundles after CMake has built them, which breaks the signature CMake
# left behind — so this is the signing that counts, and it happens last. `codesign --verify
# --strict` above is what would catch the order being wrong: it names the added files.
instrument="$plugins/RoliDrummer_artefacts/$config"

sign_bundle "$instrument/AU/Roli Drummer.component"
sign_bundle "$instrument/VST3/Roli Drummer.vst3"
sign_bundle "$instrument/Standalone/Roli Drummer.app"

# ---------------------------------------------------------------------------------- verify
#
# `spctl` is Gatekeeper's own answer rather than codesign's. Before notarisation it says
# "rejected — not notarized", which is correct and expected: the signature is good, the ticket
# does not exist yet. Reported rather than acted on, so the difference stays visible.
echo
echo "Gatekeeper assessment (pre-notarisation — 'not notarized' is the expected answer here):"
for bundle in "$instrument/AU/Roli Drummer.component" "$instrument/VST3/Roli Drummer.vst3" \
              "$instrument/Standalone/Roli Drummer.app"; do
    [ -e "$bundle" ] || continue
    printf '  %-26s ' "$(basename "$bundle")"
    # spctl exits non-zero for anything not yet notarised, which is the expected state here —
    # and `set -o pipefail` would otherwise turn that expected answer into a failed script.
    spctl --assess --type install --verbose=2 "$bundle" 2>&1 | tail -1 | sed 's/^[[:space:]]*//' || true
done
