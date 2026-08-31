#!/usr/bin/env bash
# patch_renesas_core.sh — apply the ArduinoX360-tinyusb compatibility patch
# to the locally installed Arduino Renesas core (arduino:renesas_uno).
#
# The stock arduino:renesas_uno core is closed to custom USB interfaces:
#   * variants/{NANOR4,MINIMA}/tusb_config.h hardcode CFG_TUD_VENDOR 0
#   * cores/arduino/usb/USB.cpp has strong descriptor callbacks and no
#     extension point for VID/PID or custom interfaces
#   * TinyUSB's vendord_open() rejects the INTERRUPT endpoints of the Xbox 360
#     interface (it forces TUSB_XFER_BULK), which makes SET_CONFIGURATION stall
#
# Sibling scripts for other operating systems live alongside this file
# (.macos.sh, .windows.ps1/.bat); all apply the same renesas_core.patch.
# It is idempotent: re-running on a patched core is a no-op. Restore by
# reinstalling the core:
#     arduino-cli core uninstall arduino:renesas_uno
#     arduino-cli core install arduino:renesas_uno
#
# Pinned core version: 1.6.0 (override with XR4_ALLOW_ANY_VERSION=1).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCH_FILE="$SCRIPT_DIR/renesas_core.patch"
PINNED_VERSION="1.6.0"

DATA_DIR="$(arduino-cli config get directories.data 2>/dev/null || echo "$HOME/.arduino15")"
CORE_ROOT="$DATA_DIR/packages/arduino/hardware/renesas_uno"

[[ -f "$PATCH_FILE" ]] || { echo "ERROR: $PATCH_FILE not found" >&2; exit 1; }
[[ -d "$CORE_ROOT" ]] || {
    echo "ERROR: no renesas_uno core installed." >&2
    echo "       Run: arduino-cli core install arduino:renesas_uno" >&2
    exit 1
}

# Pick the highest-versioned install if several are present.
VERSIONS=("$CORE_ROOT"/*)
CORE_DIR=""
for v in "${VERSIONS[@]}"; do
    [[ -d "$v" ]] && CORE_DIR="$v"
done
CORE_DIR="${CORE_DIR%/}"
VER="$(basename "$CORE_DIR")"

if [[ "$VER" != "$PINNED_VERSION" && "${XR4_ALLOW_ANY_VERSION:-0}" != "1" ]]; then
    echo "ERROR: core version $VER found, but this patch is pinned to $PINNED_VERSION." >&2
    echo "       Re-run with XR4_ALLOW_ANY_VERSION=1 to try anyway." >&2
    exit 1
fi

echo "Core: $CORE_DIR"

# ---- Idempotency / consistency checks -------------------------------------
MARK_B_USB='__USBGetCustomInterfaceDescriptor'
MARK_A_NANO='#define CFG_TUD_VENDOR           1'
MARK_A_MINI='#define CFG_TUD_VENDOR           1'
MARK_C_VEND='XR4: use the transfer type'

have_b_usb=0; grep -q "$MARK_B_USB" "$CORE_DIR/cores/arduino/usb/USB.cpp" && have_b_usb=1
have_a_nano=0; grep -qF "$MARK_A_NANO" "$CORE_DIR/variants/NANOR4/tusb_config.h" && have_a_nano=1
have_a_mini=0; grep -qF "$MARK_A_MINI" "$CORE_DIR/variants/MINIMA/tusb_config.h" && have_a_mini=1
have_c_vend=0; grep -q "$MARK_C_VEND" "$CORE_DIR/cores/arduino/tinyusb/class/vendor/vendor_device.c" && have_c_vend=1

TOTAL=$((have_b_usb + have_a_nano + have_a_mini + have_c_vend))
if [[ $TOTAL -eq 4 ]]; then
    echo "Already patched - nothing to do."
    exit 0
fi
if [[ $TOTAL -ne 0 ]]; then
    echo "ERROR: core appears PARTIALLY patched (markers: usb=$have_b_usb nanor4=$have_a_nano minima=$have_a_mini vendord=$have_c_vend)." >&2
    echo "       Reinstall the core for a clean state:" >&2
    echo "         arduino-cli core uninstall arduino:renesas_uno && arduino-cli core install arduino:renesas_uno" >&2
    exit 1
fi

# ---- Dry run, then apply ---------------------------------------------------
if ! (cd "$CORE_DIR" && patch -p1 --dry-run --silent < "$PATCH_FILE" >/dev/null); then
    echo "ERROR: patch does not apply cleanly to core $VER." >&2
    echo "       The core layout may differ from the pinned $PINNED_VERSION." >&2
    exit 1
fi

(cd "$CORE_DIR" && patch -p1 --silent < "$PATCH_FILE")
echo "Patch applied."

# ---- Post-conditions --------------------------------------------------------
fail=0
grep -q "$MARK_B_USB" "$CORE_DIR/cores/arduino/usb/USB.cpp" || { echo "POSTCHECK FAIL: USB.cpp hooks missing" >&2; fail=1; }
grep -qF "$MARK_A_NANO" "$CORE_DIR/variants/NANOR4/tusb_config.h" || { echo "POSTCHECK FAIL: NANOR4 CFG_TUD_VENDOR" >&2; fail=1; }
grep -qF "$MARK_A_MINI" "$CORE_DIR/variants/MINIMA/tusb_config.h" || { echo "POSTCHECK FAIL: MINIMA CFG_TUD_VENDOR" >&2; fail=1; }
grep -q "$MARK_C_VEND" "$CORE_DIR/cores/arduino/tinyusb/class/vendor/vendor_device.c" || { echo "POSTCHECK FAIL: vendord xfer-type fix" >&2; fail=1; }
[[ $fail -eq 0 ]] || exit 1

echo "Post-checks passed."
echo
echo "Build XInput sketches for Nano R4 / UNO R4 Minima with:"
echo "  arduino-cli compile --fqbn arduino:renesas_uno:nanor4 \\"
echo "    --build-property compiler.cpp.extra_flags=-DDISABLE_USB_SERIAL \\"
echo "    --library ~/ArduinoX360-tinyusb <sketch>"
