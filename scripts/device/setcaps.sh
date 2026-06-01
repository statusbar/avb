#!/usr/bin/env bash
# Grant raw-ethernet + realtime capabilities to statusbar AVB tools so they
# run without root. Manual equivalent of what the statusbar-avb .deb postinst
# does on install (packaging/deb/postinst.in) — useful for dev/standalone
# builds that weren't installed from the package.
#
# Usage:
#   ./scripts/device/setcaps.sh <binary> [<binary> ...]   # specific binaries
#   ./scripts/device/setcaps.sh                            # all statusbar-* in BINDIR
#
# Env:
#   BINDIR   directory scanned when no args are given (default: /usr/local/bin)
#   CAPS     capability set to apply (default: the full L2+RT set below)
#
# Requires sudo (setcap needs CAP_SETFCAP) and libcap2-bin (`setcap`).
set -euo pipefail

CAPS="${CAPS:-cap_net_raw,cap_net_admin,cap_sys_nice,cap_ipc_lock=ep}"
BINDIR="${BINDIR:-/usr/local/bin}"

if ! command -v setcap >/dev/null 2>&1; then
    echo "error: 'setcap' not found — install libcap2-bin" >&2
    exit 1
fi

SUDO=""
[ "$(id -u)" -eq 0 ] || SUDO="sudo"

targets=("$@")
if [ "${#targets[@]}" -eq 0 ]; then
    # Default: every statusbar-* ELF executable in BINDIR.
    while IFS= read -r f; do targets+=("$f"); done < <(ls "$BINDIR"/statusbar-* 2>/dev/null || true)
fi

if [ "${#targets[@]}" -eq 0 ]; then
    echo "nothing to do (no targets given and none found in $BINDIR)" >&2
    exit 0
fi

for f in "${targets[@]}"; do
    # Resolve a bare name against BINDIR / PATH.
    if [ ! -e "$f" ]; then
        f="$(command -v "$f" 2>/dev/null || echo "$BINDIR/$f")"
    fi
    [ -f "$f" ] || { echo "skip (not found): $f" >&2; continue; }
    # ELF only — setcap rejects scripts.
    magic=$(head -c 4 "$f" 2>/dev/null | od -An -tx1 2>/dev/null | tr -d ' \n')
    [ "$magic" = "7f454c46" ] || { echo "skip (not ELF): $f" >&2; continue; }
    if $SUDO setcap "$CAPS" "$f"; then
        echo "set [$CAPS] on $f"
    else
        echo "warning: setcap failed on $f" >&2
    fi
done
