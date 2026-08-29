#!/usr/bin/env bash
# Package the project for transfer to the Mac that has the board plugged in.
#
# Build and flash happen on the Mac because that is where the USB port is; this
# machine only ever writes source. The tarball deliberately excludes anything
# machine-specific or secret:
#
#   .pio/          build output — must be regenerated for the host toolchain
#   .511-token     the API token, which belongs on neither the wire nor a disk
#                  you are about to copy around
#   .git/          the whole object store. It can hold objects that are no
#                  longer reachable from any branch — a dropped stash, an
#                  amended commit — which a plain file copy carries along
#                  even though a push never would.
#   tools/captures raw API responses, which embed the token in their URLs
#   test/test_*    compiled host-test binaries (Linux ELF, useless on macOS)
#
# Usage:
#   tools/package.sh                 -> ./dist/caltrain-notifier-<stamp>.tar.gz
#   tools/package.sh /some/where     -> writes there instead
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-$ROOT/dist}"
STAMP="$(date +%Y%m%d-%H%M%S)"
NAME="caltrain-notifier-$STAMP"
TARBALL="$OUT_DIR/$NAME.tar.gz"

mkdir -p "$OUT_DIR"

# Remove compiled host-test binaries rather than naming each one in an exclude
# list. An enumerated list silently goes stale the moment a test is added, and
# the failure mode is shipping a Linux ELF to a Mac.
make -C "$ROOT/test" clean >/dev/null 2>&1 || true

BASE="$(basename "$ROOT")"

# Build directories are excluded WITHOUT a path anchor, so the pattern catches
# them at any depth. An anchored --exclude=".../.pio" misses bringup/.pio, and
# shipping one host's object files to another architecture is at best confusing
# and at worst a build that links stale Linux artifacts.
tar czf "$TARBALL" \
    -C "$(dirname "$ROOT")" \
    --transform "s,^$BASE,$NAME," \
    --exclude=".git" \
    --exclude=".pio" \
    --exclude=".venv-pio" \
    --exclude="__pycache__" \
    --exclude="*.pyc" \
    --exclude="captures" \
    --exclude="$BASE/dist" \
    --exclude="$BASE/.511-token" \
    "$BASE"

echo "Wrote $TARBALL"
echo

# Fail loudly if the token somehow made it in. A silent leak into a file you are
# about to copy to another machine is exactly the kind of thing that only gets
# noticed after it has been shared.
if tar tzf "$TARBALL" | grep -q '511-token'; then
    echo "ERROR: the token file is inside the archive — refusing to vouch for it." >&2
    exit 1
fi
if tar tzf "$TARBALL" | grep -qE '(^|/)\.git/'; then
    echo "ERROR: the git object store leaked into the archive." >&2
    exit 1
fi
if tar tzf "$TARBALL" | grep -qE '(^|/)\.pio/'; then
    echo "ERROR: build output leaked into the archive." >&2
    exit 1
fi
# Any executable under test/ is a compiled binary; sources end in .cpp.
if tar tzf "$TARBALL" | grep -E '/test/test_[a-z_]+$' | grep -qv '\.cpp$'; then
    echo "ERROR: a compiled test binary leaked into the archive." >&2
    tar tzf "$TARBALL" | grep -E '/test/test_[a-z_]+$' >&2
    exit 1
fi
echo "Verified: no token, no build output, no test binaries."
echo "Contents:"
tar tzf "$TARBALL" | sed "s,^$NAME/,  ," | grep -v '/$' | sort
echo
echo "Copy it to the Mac, then:"
echo "    tar xzf $NAME.tar.gz"
echo "    cd $NAME"
echo "    ./tools/mac_flash.sh bringup     # prove the panel first"
