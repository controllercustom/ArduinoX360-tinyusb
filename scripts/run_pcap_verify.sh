#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Flash AutoCycle, capture USB traffic via usbmon, and validate the resulting
# pcap against the deterministic phase baseline.
#
# Hardware-in-the-loop path: a board must be attached with the AutoCycle
# sketch flashed. RP2040 uses the Debug Probe (or a native Pico upload);
# ESP32 uses the board's normal upload path (UART bridge allowed HERE — this
# is the validation path, not the README user path).
#
# Usage:
#     sudo ./run_pcap_verify.sh --board rp2040 --sketch_dir /path/to/AutoCycle
#     sudo ./run_pcap_verify.sh --board esp32 --sketch_dir /path/to/AutoCycle
#
# Requires: arduino-cli, jstest (jstest-gtk), tcpdump, tshark, python3.

set -euo pipefail

BOARD="rp2040"
SKETCH_DIR=""
PORT=""
CAPTURE_SECONDS=35
BUS=""
OUT=/tmp/xinput_capture.pcap
LEFT=/tmp/leftover_data.txt
LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 --board {rp2040|esp32} --sketch_dir DIR [--port PORT] [--seconds N]"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --board) BOARD="$2"; shift 2;;
        --sketch_dir) SKETCH_DIR="$2"; shift 2;;
        --port) PORT="$2"; shift 2;;
        --seconds) CAPTURE_SECONDS="$2"; shift 2;;
        --bus) BUS="$2"; shift 2;;
        -h|--help) usage;;
        *) usage;;
    esac
done

[[ -n "$SKETCH_DIR" ]] || usage
[[ -d "$SKETCH_DIR" ]] || { echo "Sketch dir not found: $SKETCH_DIR"; exit 1; }

case "$BOARD" in
    rp2040)
        FQBN="rp2040:rp2040:rpipico:usbstack=tinyusb,freq=200,flash=2097152_0,opt=Small"
        ;;
    esp32)
        FQBN="esp32:esp32:esp32s3:USBMode=default"
        ;;
    *)
        echo "Unknown board: $BOARD"; exit 1;;
esac

echo "=== Flashing $SKETCH_DIR for $BOARD ==="
if [[ -n "$PORT" ]]; then
    arduino-cli compile --upload -p "$PORT" --fqbn "$FQBN" --library "$LIB_DIR" "$SKETCH_DIR"
else
    arduino-cli compile --fqbn "$FQBN" --library "$LIB_DIR" "$SKETCH_DIR"
    echo "No --port given; compiled only. Flash manually, then re-run with --port."
    exit 0
fi

sleep 3

# Auto-detect USB bus if not given.
if [[ -z "$BUS" ]]; then
    BUS=$(lsusb | grep -i "045e:028e" | sed -E 's/Bus ([0-9]+).*/\1/' | head -1)
fi
[[ -n "$BUS" ]] || { echo "Could not find 045e:028e device on USB (lsusb)."; exit 1; }
MON=usbmon$BUS
echo "Using $MON for capture"

# jstest keeps the IN endpoint polled so reports actually flow on the bus.
JSDEV=$(ls /dev/input/js* 2>/dev/null | head -1)
if [[ -n "$JSDEV" ]]; then
    echo "Starting jstest on $JSDEV"
    jstest "$JSDEV" >/tmp/jstest_output.txt 2>&1 &
    JS_PID=$!
else
    JS_PID=""
    echo "WARNING: no joystick device — reports may not be captured"
fi

echo "Capturing $CAPTURE_SECONDS s on $MON"
sudo timeout "$CAPTURE_SECONDS" tcpdump -i "$MON" -w "$OUT" || true
[[ -n "$JS_PID" ]] && kill "$JS_PID" 2>/dev/null || true

echo "=== Extracting payloads ==="
tshark -r "$OUT" -V | grep "Leftover Capture Data" > "$LEFT"
wc -l "$LEFT"

echo "=== Analysis ==="
python3 "$LIB_DIR/scripts/analyze_xinput_pcap.py" "$LEFT"
echo
echo "=== Baseline validation ==="
python3 "$LIB_DIR/scripts/validate_xinput_baseline.py" "$LEFT"
