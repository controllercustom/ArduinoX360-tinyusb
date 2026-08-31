#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""End-to-end latency test harness for ArduinoX360-tinyusb.

Correlates device UART timestamps with host PC evdev gamepad events to measure
input-to-USB round-trip latency per event type.

Portable telemetry channel:
    RP2040 -> Serial1 (Debug Probe UART bridge, /dev/ttyACM0)
    ESP32  -> Serial0 (UART0 via the board's USB-UART bridge, /dev/ttyUSB0)

Usage:
    sudo python3 run_tests.py --uart /dev/ttyACM0 --evdev /dev/input/eventX
"""

import argparse
import collections
import json
import os
import queue
import statistics
import sys
import threading
import time

try:
    from evdev import InputDevice, ecodes
except ImportError:
    print("ERROR: evdev package required. Install with: pip3 install evdev")
    sys.exit(1)

try:
    import serial
except ImportError:
    print("ERROR: pyserial required. Install with: pip3 install pyserial")
    sys.exit(1)


UARTEvent = collections.namedtuple('UARTEvent', ['ts_ns', 'seq', 'type', 'value'])
EvdevEvent = collections.namedtuple('EvdevEvent', ['ts_ns', 'type', 'code', 'value'])

EVENT_NAMES = {
    0: "button_press",
    1: "button_release",
    2: "left_stick",
    3: "right_stick",
    4: "left_trigger",
    5: "right_trigger",
    6: "dpad",
    99: "test_marker",
}

# evdev event codes from xpad driver mapping.
EV_KEY = 1
EV_ABS = 3
INTERESTING_CODES = {
    EV_KEY: {304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318},
    EV_ABS: {0, 1, 3, 4, 5, 16, 17},  # ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_HAT0X, ABS_HAT0Y
}


def find_gamepad():
    from glob import glob
    for path in sorted(glob("/dev/input/event[0-9]*")):
        try:
            dev = InputDevice(path)
            caps = dev.capabilities()
            if EV_KEY in caps and 304 in caps.get(EV_KEY, []):
                return path, dev
        except (PermissionError, OSError):
            continue
    return None, None


def read_uart(ser, out_queue, stop_event):
    """Read TS: lines from the device UART."""
    try:
        while not stop_event.is_set():
            line = ser.readline()
            if not line:
                continue
            text = line.decode('utf-8', errors='replace').strip()

            if text.startswith("TS:"):
                parts = text[3:].split(":")
                if len(parts) == 4:
                    try:
                        out_queue.put(UARTEvent(
                            ts_ns=int(parts[0]) * 1000,
                            seq=int(parts[1]),
                            type=int(parts[2]),
                            value=int(parts[3]),
                        ))
                    except ValueError:
                        pass
            elif text.startswith("BTN:"):
                print(f"  Button: {text[4:]}", file=sys.stderr)
            elif text.startswith("RPT:"):
                print(f"  Report: {text[4:]}", file=sys.stderr)
            elif text == "READY":
                print("  device ready", file=sys.stderr)
    except Exception as e:
        print(f"UART error: {e}", file=sys.stderr)


def read_evdev(evdev_path, out_queue, stop_event):
    """Read gamepad events from evdev using non-blocking poll loop."""
    try:
        dev = InputDevice(evdev_path)
        print(f"  Device: {dev.name}", file=sys.stderr)

        import select
        while not stop_event.is_set():
            r, _, _ = select.select([dev], [], [], 0.5)
            if not r:
                continue
            for event in dev.read():
                if stop_event.is_set():
                    break
                if event.type in INTERESTING_CODES:
                    codes = INTERESTING_CODES[event.type]
                    if event.code in codes:
                        ts_ns = int(event.timestamp() * 1e9)
                        out_queue.put(EvdevEvent(ts_ns, event.type, event.code, event.value))
    except Exception as e:
        print(f"evdev error: {e}", file=sys.stderr)


def correlate(uart_records, evdev_events, offset_ns=0):
    """Match UART events to nearest evdev events by timestamp proximity."""
    results = {}
    for urec in uart_records:
        if urec.type == 99:
            continue
        name = EVENT_NAMES.get(urec.type, f"type_{urec.type}")
        host_ns = offset_ns + urec.ts_ns
        best_diff = float('inf')
        best_ev = None
        for ev in evdev_events:
            diff = abs(ev.ts_ns - host_ns)
            if diff < 100_000_000 and diff < best_diff:
                best_diff = diff
                best_ev = ev
        if best_ev is not None:
            results.setdefault(name, []).append(best_diff / 1000.0)
    return results


def print_results(results):
    if not results:
        print("No correlated events found.")
        return
    print("\n=== Latency Results (microseconds) ===")
    print(f"{'Event Type':<20} {'Count':>6} {'Min':>8} {'Max':>8} {'Median':>8} {'P95':>8} {'Avg':>8}")
    print("-" * 74)
    all_latencies = []
    for name in sorted(results.keys()):
        lats = results[name]
        if not lats:
            continue
        all_latencies.extend(lats)
        slat = sorted(lats)
        p95 = slat[int(len(slat) * 0.95)]
        print(f"{name:<20} {len(slat):>6} {min(slat):>8.1f} {max(slat):>8.1f} "
              f"{statistics.median(slat):>8.1f} {p95:>8.1f} {statistics.mean(slat):>8.1f}")
    if all_latencies:
        slat = sorted(all_latencies)
        p95_all = slat[int(len(slat) * 0.95)]
        print("-" * 74)
        print(f"{'ALL':<20} {len(slat):>6} {min(slat):>8.1f} {max(slat):>8.1f} "
              f"{statistics.median(slat):>8.1f} {p95_all:>8.1f} {statistics.mean(slat):>8.1f}")


def do_sync(ser):
    """SYNC handshake to compute device-timer to host-time offset (ns).

    Host time uses time.time_ns() (epoch-based) to match evdev timestamps.
    """
    ser.reset_input_buffer()
    host_before = time.time_ns()
    ser.write(b"SYNC\n")
    line = b""
    while True:
        c = ser.read(1)
        if not c:
            break
        line += c
        if c == b'\n':
            break
    host_after = time.time_ns()
    text = line.decode('utf-8', errors='replace').strip()
    if text.startswith("SYNC:"):
        dev_us = int(text[5:])
        host_estimate = (host_before + host_after) // 2
        return host_estimate - dev_us * 1000
    return None


def main():
    parser = argparse.ArgumentParser(description="ArduinoX360-tinyusb latency test")
    parser.add_argument("--uart", default="/dev/ttyACM0", help="UART port")
    parser.add_argument("--evdev", help="evdev device (auto-detect if omitted)")
    parser.add_argument("--duration", type=int, default=60, help="Max test duration (s)")
    parser.add_argument("--json", help="Output results as JSON to file")
    args = parser.parse_args()

    if not args.evdev:
        path, dev = find_gamepad()
        if path is None or dev is None:
            print("ERROR: No gamepad detected. Connect the XInput device and specify --evdev.")
            sys.exit(1)
        args.evdev = path
        print(f"Auto-detected gamepad: {dev.name} @ {path}", file=sys.stderr)

    print(f"Starting test (max {args.duration}s)...", file=sys.stderr)
    print(f"  UART:  {args.uart}", file=sys.stderr)
    print(f"  Evdev: {args.evdev}", file=sys.stderr)

    ser = serial.Serial(args.uart, 115200, timeout=2)
    time.sleep(1)
    ser.reset_input_buffer()
    offset_ns = do_sync(ser)
    if offset_ns is None:
        print("WARNING: Could not sync with device timer", file=sys.stderr)
        offset_ns = 0
    else:
        print(f"  Time offset: {offset_ns/1e6:.1f} ms", file=sys.stderr)

    event_queue = queue.Queue()
    stop_event = threading.Event()

    uart_thread = threading.Thread(target=read_uart, args=(ser, event_queue, stop_event), daemon=True)
    evdev_thread = threading.Thread(target=read_evdev, args=(args.evdev, event_queue, stop_event), daemon=True)

    uart_thread.start()
    evdev_thread.start()
    time.sleep(0.5)

    ser.write(b"START\n")
    print("  Sent START command", file=sys.stderr)

    time.sleep(args.duration)
    stop_event.set()

    uart_thread.join(timeout=3)
    evdev_thread.join(timeout=3)

    uart_records = []
    evdev_events = []
    while not event_queue.empty():
        ev = event_queue.get_nowait()
        if isinstance(ev, UARTEvent):
            uart_records.append(ev)
        else:
            evdev_events.append(ev)

    print(f"\nUART records: {len(uart_records)}, evdev events: {len(evdev_events)}", file=sys.stderr)

    results = correlate(uart_records, evdev_events, offset_ns)
    print_results(results)

    if args.json:
        out = {"latency_us": {}}
        for name, lats in results.items():
            out["latency_us"][name] = {
                "min": min(lats), "max": max(lats),
                "median": statistics.median(lats),
                "p95": sorted(lats)[int(len(lats) * 0.95)],
                "mean": statistics.mean(lats),
                "count": len(lats),
            }
        with open(args.json, "w") as f:
            json.dump(out, f, indent=2)
        print(f"Results written to {args.json}", file=sys.stderr)


if __name__ == "__main__":
    main()
