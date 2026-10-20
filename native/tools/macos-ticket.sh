#!/usr/bin/env bash
# Apple's ticket service is used by both staple and validate. Retry delivery
# failures without accepting missing, invalid or revoked tickets.
set -euo pipefail

action="${1:-}"
bundle="${2:-}"
if [ "$#" -ne 2 ] || { [ "$action" != staple ] && [ "$action" != validate ]; }; then
    echo "usage: $0 <staple|validate> <bundle>" >&2
    exit 64
fi

for attempt in 1 2 3 4 5 6 7 8 9 10 11 12; do
    if output="$(xcrun stapler "$action" "$bundle" 2>&1)"; then
        printf '%s\n' "$output"
        exit 0
    else
        status=$?
    fi
    printf '%s\n' "$output" >&2
    # sysexits: invalid data/input, unwritable output and revoked tickets are
    # permanent. EX_NOHOST is also used for delayed tickets/CloudKit error 68.
    case "$status" in
        64|65|66|73|77) exit "$status" ;;
        68|69|75) ;;
        *)
            case "$output" in
                *NSURLErrorDomain*|*kCFErrorDomainCFNetwork*) ;;
                *) exit "$status" ;;
            esac
            ;;
    esac
    if [ "$attempt" -eq 12 ]; then
        echo "Apple ticket $action failed after $attempt attempts: $bundle" >&2
        exit "$status"
    fi
    delay=$((attempt < 4 ? attempt * 15 : 60))
    echo "Apple ticket $action attempt $attempt failed ($status); retrying in ${delay}s: $bundle" >&2
    sleep "$delay"
done
