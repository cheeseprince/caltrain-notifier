#!/usr/bin/env bash
# Build and flash the Caltrain Notifier from a Mac.
#
#   ./tools/mac_flash.sh              # flash the bring-up smoke test (start here)
#   ./tools/mac_flash.sh bringup      # same, explicitly
#   ./tools/mac_flash.sh firmware     # flash the real firmware
#   ./tools/mac_flash.sh bringup v20  # force the other board revision
#   ./tools/mac_flash.sh monitor      # just open the serial monitor
#
# PlatformIO is installed into a virtualenv under .venv-pio rather than into the
# system Python or via Homebrew. That keeps this repo from perturbing anything
# else on the machine, and makes "delete the folder" a complete uninstall.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TARGET="${1:-bringup}"
REV="${2:-v22}"          # this unit is v2.2; v20 swaps TFT_MISO and TOUCH_CS
VENV="$ROOT/.venv-pio"
PIO="$VENV/bin/pio"

say()  { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\n\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

[[ "$(uname -s)" == "Darwin" ]] || say "Note: this script is written for macOS; continuing anyway."

# ---------------------------------------------------------------------------
# 1. PlatformIO
# ---------------------------------------------------------------------------
if [[ ! -x "$PIO" ]]; then
    say "Installing PlatformIO into $VENV (one time, a few minutes)"
    command -v python3 >/dev/null || die "python3 not found. Install Xcode command line tools: xcode-select --install"
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install --quiet --upgrade pip
    "$VENV/bin/pip" install --quiet platformio
fi

# esptool 4.11 imports intelhex, but a pip-installed PlatformIO does not pull it
# in — the toolchain package declares it and the bare venv never gets it. The
# failure surfaces late and cryptically, as a ModuleNotFoundError while
# generating bootloader.bin, long after the source has compiled cleanly.
#
# Checked on every run, not just at creation, so a venv made before this fix
# repairs itself rather than needing to be deleted.
if ! "$VENV/bin/python" -c "import intelhex" >/dev/null 2>&1; then
    say "Adding esptool's missing intelhex dependency"
    "$VENV/bin/pip" install --quiet intelhex
fi

say "PlatformIO $("$PIO" --version | awk '{print $NF}')"

# ---------------------------------------------------------------------------
# 2. Find the board
# ---------------------------------------------------------------------------
# This board uses a CP210x or CH340 USB-UART bridge, not native USB, so macOS
# needs a driver for it. Recent macOS ships a CP210x driver; CH340 usually does
# not and needs the WCH one. Always prefer the cu.* device: opening tty.* blocks
# waiting for carrier detect and simply hangs.
find_port() {
    local p
    for p in /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial*; do
        [[ -e "$p" ]] && { echo "$p"; return 0; }
    done
    return 1
}

if ! PORT="$(find_port)"; then
    die "No USB serial device found.

Checked /dev/cu.usbserial-*, /dev/cu.SLAB_USBtoUART*, /dev/cu.wchusbserial*

  - Is the board plugged in, with a DATA cable rather than a charge-only one?
  - CH340 boards need the WCH driver: https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html
  - List what the Mac does see with:  ls /dev/cu.*"
fi
say "Board on $PORT"

# ---------------------------------------------------------------------------
# 3. Build and upload
# ---------------------------------------------------------------------------
case "$TARGET" in
    bringup)
        PROJECT_DIR="$ROOT/bringup"
        ENV_NAME=$([[ "$REV" == "v20" ]] && echo "bringup" || echo "bringup_v22")
        ;;
    firmware)
        # Revision-specific again: tap-to-wake reads the touch controller, which
        # needs the two pins that v2.0 and v2.2 swap.
        PROJECT_DIR="$ROOT"
        ENV_NAME=$([[ "$REV" == "v20" ]] && echo "caltrain_v20" || echo "caltrain")
        ;;
    monitor)
        say "Opening serial monitor on $PORT — Ctrl-C to exit"
        exec "$PIO" device monitor --port "$PORT" --baud 115200
        ;;
    *)
        die "Unknown target '$TARGET'. Use: bringup | firmware | monitor"
        ;;
esac

[[ -f "$PROJECT_DIR/platformio.ini" ]] || die "No platformio.ini in $PROJECT_DIR"

say "Building $TARGET (env: $ENV_NAME)"
"$PIO" run --project-dir "$PROJECT_DIR" --environment "$ENV_NAME"

say "Uploading to $PORT"
# If this fails to sync, hold the BOOT/IO0 button, tap EN/RST, release BOOT,
# and re-run — that forces the ROM download mode by hand.
"$PIO" run --project-dir "$PROJECT_DIR" --environment "$ENV_NAME" \
           --target upload --upload-port "$PORT"

say "Flashed. Opening the serial monitor — Ctrl-C to exit."
if [[ "$TARGET" == "bringup" ]]; then
    cat <<'EOF'

What you should see:
  * a banner naming the chip, flash size and PSRAM
  * the screen cycling RED / GREEN / BLUE / WHITE / BLACK / CYAN / AMBER,
    each with a label and a frame touching all four edges
  * [TOUCH] lines and a dot under your finger when you press the screen

If the colors cycle but touch is dead, this board is the OTHER revision:
    ./tools/mac_flash.sh bringup v20

EOF
fi
exec "$PIO" device monitor --project-dir "$PROJECT_DIR" --port "$PORT" --baud 115200
