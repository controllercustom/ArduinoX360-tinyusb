#!/bin/sh
# patch_renesas_core.macos.sh — apply the ArduinoX360-tinyusb compatibility
# patch to the locally installed Arduino Renesas core (arduino:renesas_uno).
#
# macOS version: uses only the default shell (/bin/sh) and stock utilities
# (/usr/bin/patch, grep). See patch_renesas_core.sh for the Linux version and
# patch_renesas_core.windows.ps1 for Windows.
#
# Restore by reinstalling the core:
#     arduino-cli core uninstall arduino:renesas_uno
#     arduino-cli core install arduino:renesas_uno
#
# Pinned core version: 1.6.0 (override with XR4_ALLOW_ANY_VERSION=1).

set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PATCH_FILE="$SCRIPT_DIR/renesas_core.patch"
PINNED_VERSION="1.6.0"

[ -f "$PATCH_FILE" ] || { echo "ERROR: $PATCH_FILE not found" >&2; exit 1; }

DATA_DIR=$(arduino-cli config get directories.data 2>/dev/null || true)
[ -n "$DATA_DIR" ] || DATA_DIR="$HOME/Library/Arduino15"

CORE_ROOT="$DATA_DIR/packages/arduino/hardware/renesas_uno"
if [ ! -d "$CORE_ROOT" ]; then
    echo "ERROR: no renesas_uno core installed." >&2
    echo "       Run: arduino-cli core install arduino:renesas_uno" >&2
    exit 1
fi

# Pick the highest-versioned install if several are present (glob sorts
# lexically; good enough for x.y.z versions).
CORE_DIR=""
for v in "$CORE_ROOT"/*; do
    [ -d "$v" ] && CORE_DIR="$v"
done
if [ -z "$CORE_DIR" ]; then
    echo "ERROR: no version directory under $CORE_ROOT" >&2
    exit 1
fi
VER=${CORE_DIR##*/}

if [ "$VER" != "$PINNED_VERSION" ] && [ "${XR4_ALLOW_ANY_VERSION:-0}" != "1" ]; then
    echo "ERROR: core version $VER found, but this patch is pinned to $PINNED_VERSION." >&2
    echo "       Re-run with XR4_ALLOW_ANY_VERSION=1 to try anyway." >&2
    exit 1
fi

echo "Core: $CORE_DIR"

USB_CPP="$CORE_DIR/cores/arduino/usb/USB.cpp"
NANO_CFG="$CORE_DIR/variants/NANOR4/tusb_config.h"
MINI_CFG="$CORE_DIR/variants/MINIMA/tusb_config.h"
VEND_C="$CORE_DIR/cores/arduino/tinyusb/class/vendor/vendor_device.c"

have_b_usb=0; grep -q '__USBGetCustomInterfaceDescriptor' "$USB_CPP" && have_b_usb=1
have_a_nano=0; grep -qF '#define CFG_TUD_VENDOR           1' "$NANO_CFG" && have_a_nano=1
have_a_mini=0; grep -qF '#define CFG_TUD_VENDOR           1' "$MINI_CFG" && have_a_mini=1
have_c_vend=0; grep -q 'XR4: use the transfer type' "$VEND_C" && have_c_vend=1

TOTAL=$((have_b_usb + have_a_nano + have_a_mini + have_c_vend))
if [ "$TOTAL" -eq 4 ]; then
    echo "Already patched - nothing to do."
    exit 0
fi
if [ "$TOTAL" -ne 0 ]; then
    echo "ERROR: core appears PARTIALLY patched (markers: usb=$have_b_usb nanor4=$have_a_nano minima=$have_a_mini vendord=$have_c_vend)." >&2
    echo "       Reinstall the core for a clean state:" >&2
    echo "         arduino-cli core uninstall arduino:renesas_uno && arduino-cli core install arduino:renesas_uno" >&2
    exit 1
fi

# Dry-run flag: GNU patch knows --dry-run, Apple/BSD patch uses -C.
DRYRUN="--dry-run"
if ! patch --help 2>&1 | grep -q -- '--dry-run'; then
    DRYRUN="-C"
fi

(cd "$CORE_DIR" && patch -p1 $DRYRUN -s < "$PATCH_FILE") || {
    echo "ERROR: patch does not apply cleanly to core $VER." >&2
    exit 1
}

(cd "$CORE_DIR" && patch -p1 -s < "$PATCH_FILE")
echo "Patch applied."

fail=0
grep -q '__USBGetCustomInterfaceDescriptor' "$USB_CPP" || { echo "POSTCHECK FAIL: USB.cpp hooks missing" >&2; fail=1; }
grep -qF '#define CFG_TUD_VENDOR           1' "$NANO_CFG" || { echo "POSTCHECK FAIL: NANOR4 CFG_TUD_VENDOR" >&2; fail=1; }
grep -qF '#define CFG_TUD_VENDOR           1' "$MINI_CFG" || { echo "POSTCHECK FAIL: MINIMA CFG_TUD_VENDOR" >&2; fail=1; }
grep -q 'XR4: use the transfer type' "$VEND_C" || { echo "POSTCHECK FAIL: vendord xfer-type fix" >&2; fail=1; }
[ "$fail" -eq 0 ] || exit 1

echo "Post-checks passed."
echo
echo "Build XInput sketches for Nano R4 / UNO R4 Minima with:"
echo "  arduino-cli compile --fqbn arduino:renesas_uno:nanor4 \\"
echo "    --build-property compiler.cpp.extra_flags=-DDISABLE_USB_SERIAL \\"
echo "    --library ~/ArduinoX360-tinyusb <sketch>"
