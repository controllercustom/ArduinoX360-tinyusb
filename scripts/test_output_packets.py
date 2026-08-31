#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inject XInput OUT packets (rumble + LED) via libusb, verify device callback telemetry over UART.

Usage:
    sudo python3 scripts/test_output_packets.py --uart /dev/ttyACM0

Requires pyusb and pyserial. The device must be running a sketch that calls
setReceiveCallback() and prints CB_RUMBLE:<L>,<R> or CB_LED:<idx> telemetry on
the portable TELE channel (RP2040 Serial1 / ESP32 Serial0 / SAMD Serial).

CRITICAL: do NOT call dev.set_configuration(). It triggers a USB
re-enumeration/reset which unmounts TinyUSB and stops the receive pump; once
the 32B OUT FIFO fills, writes hang with Errno 110. Detach the kernel driver
(xpad) then claim_interface() directly.

The XInput vendor interface is discovered dynamically (bInterfaceClass == 0xFF)
so the script works whether the device is a pure XInput function (interface 0)
or a composite device (e.g. a debug CDC + XInput build where the vendor
interface is numbered higher and its endpoint is reallocated).
"""

import argparse
import select
import sys
import time

try:
    import usb.core
    import usb.util
except ImportError:
    print("ERROR: pyusb required. Install with: sudo apt install python3-usb")
    sys.exit(1)

try:
    import serial
except ImportError:
    print("ERROR: pyserial required. Install with: pip3 install --break-system-packages pyserial")
    sys.exit(1)


XINPUT_VID = 0x045E
XINPUT_PID = 0x028E


def find_device():
    dev = usb.core.find(idVendor=XINPUT_VID, idProduct=XINPUT_PID)
    if dev is None:
        print("ERROR: No XInput device found (VID=0x%04X PID=0x%04X)" % (XINPUT_VID, XINPUT_PID))
        sys.exit(1)
    return dev


def find_xinput_interface(dev):
    """Locate the XInput vendor interface (bInterfaceClass == 0xFF) and its
    bulk OUT endpoint. Returns (interface_number, out_endpoint_address)."""
    cfg = dev.get_active_configuration()
    intf = usb.util.find_descriptor(cfg, bInterfaceClass=0xFF)
    if intf is None:
        print("ERROR: No vendor (0xFF) interface found on XInput device")
        sys.exit(1)
    ep_out = usb.util.find_descriptor(
        intf,
        custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress)
        == usb.util.ENDPOINT_OUT,
    )
    if ep_out is None:
        print("ERROR: No OUT endpoint found on XInput interface")
        sys.exit(1)
    return intf.bInterfaceNumber, ep_out.bEndpointAddress


def detach_kernel_driver(dev, intf_num):
    try:
        if dev.is_kernel_driver_active(intf_num):
            print("  Detaching kernel driver from interface %d" % intf_num)
            dev.detach_kernel_driver(intf_num)
    except (usb.core.USBError, NotImplementedError):
        pass


def send_rumble(dev, out_ep, left_motor: int, right_motor: int) -> bool:
    """Send type-0x00 rumble packet."""
    pkt = bytes([0x00, 6, 0xFF, left_motor, right_motor])
    try:
        n = dev.write(out_ep, pkt)
        return n == len(pkt)
    except usb.core.USBError as e:
        print("  USB write error (rumble): %s" % e)
        return False


def send_led(dev, out_ep, led_index: int) -> bool:
    """Send type-0x01 LED animation packet."""
    pkt = bytes([0x01, 3, led_index])
    try:
        n = dev.write(out_ep, pkt)
        return n == len(pkt)
    except usb.core.USBError as e:
        print("  USB write error (led): %s" % e)
        return False


def read_uart_lines(ser, duration=1.0):
    """Read all available UART lines within a time window."""
    lines = []
    deadline = time.time() + duration
    while time.time() < deadline:
        remaining = max(0.05, min(0.2, deadline - time.time()))
        try:
            if ser.in_waiting > 0 or select.select([ser], [], [], remaining)[0]:
                line = ser.readline().decode('utf-8', errors='replace').strip()
                lines.append(line)
        except Exception:
            break
    return lines


def wait_for_keyword(ser, keyword, timeout=15.0):
    """Wait until a UART line contains the given keyword."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = max(0.1, min(0.5, deadline - time.time()))
        try:
            if ser.in_waiting > 0 or select.select([ser], [], [], remaining)[0]:
                line = ser.readline().decode('utf-8', errors='replace').strip()
                if keyword in line:
                    return True
        except Exception:
            break
    return False


def find_line_tag(lines, tag):
    for l in lines:
        if tag in l:
            return l
    return None


def test_rumble(dev, out_ep, ser) -> bool:
    print("\n--- RUMBLE TEST ---")
    passed = True

    tests = [
        (128, 0),
        (0, 255),
        (64, 192),
        (255, 255),
        (0, 0),
    ]

    for left, right in tests:
        ok = send_rumble(dev, out_ep, left, right)
        if not ok:
            print("  FAIL: could not write rumble packet L=%d R=%d" % (left, right))
            passed = False
            continue

        lines = read_uart_lines(ser, duration=1.5)
        expected_tag = "CB_RUMBLE:%d,%d" % (left, right)
        matched_line = find_line_tag(lines, expected_tag)
        if matched_line:
            print("  PASS: L=%3d R=%3d -> got '%s'" % (left, right, matched_line))
        else:
            summary = "; ".join(lines[:5]) if lines else "(no UART output)"
            print("  FAIL: L=%3d R=%3d -> expected '%s', got: %s" % (left, right, expected_tag, summary))
            passed = False

    return passed


def test_led(dev, out_ep, ser) -> bool:
    print("\n--- LED TEST ---")
    passed = True

    for led_idx in range(5):
        ok = send_led(dev, out_ep, led_idx)
        if not ok:
            print("  FAIL: could not write LED packet idx=%d" % led_idx)
            passed = False
            continue

        lines = read_uart_lines(ser, duration=1.5)
        expected_tag = "CB_LED:%d" % led_idx
        matched_line = find_line_tag(lines, expected_tag)
        if matched_line:
            print("  PASS: LED index %d -> got '%s'" % (led_idx, matched_line))
        else:
            summary = "; ".join(lines[:5]) if lines else "(no UART output)"
            print("  FAIL: LED index %d -> expected '%s', got: %s" % (led_idx, expected_tag, summary))
            passed = False

    return passed


def test_mixed(dev, out_ep, ser) -> bool:
    """Interleave rumble and LED packets to verify correct type routing."""
    print("\n--- MIXED PACKET TEST ---")
    passed = True

    sequence = [
        ("rumble", 100, 200),
        ("led", 3, None),
        ("rumble", 50, 75),
        ("led", 1, None),
    ]

    for typ, a, b in sequence:
        if typ == "rumble":
            ok = send_rumble(dev, out_ep, a, b)
            expected_tag = "CB_RUMBLE:%d,%d" % (a, b)
        else:
            ok = send_led(dev, out_ep, a)
            expected_tag = "CB_LED:%d" % a

        if not ok:
            print("  FAIL: could not write packet")
            passed = False
            continue

        lines = read_uart_lines(ser, duration=1.5)
        matched_line = find_line_tag(lines, expected_tag)
        if matched_line:
            print("  PASS: -> got '%s'" % matched_line)
        else:
            summary = "; ".join(lines[:5]) if lines else "(no UART output)"
            print("  FAIL: expected '%s', got: %s" % (expected_tag, summary))
            passed = False

    return passed


def main():
    parser = argparse.ArgumentParser(description="ArduinoX360-tinyusb OUT packet test")
    parser.add_argument("--uart", default="/dev/ttyACM0", help="UART port for telemetry")
    args = parser.parse_args()

    # Open the telemetry (UART/CDC) port FIRST. On debug-CDC builds the sketch
    # gates XInput.begin() behind `while(!TELE)`, so the XInput gamepad only
    # enumerates once the host opens this port. We must open it before looking
    # the device up via pyusb.
    try:
        ser = serial.Serial(args.uart, 115200, timeout=3, rtscts=False, dsrdtr=False)
        time.sleep(1.5)

        while ser.in_waiting > 0:
            ser.readline()

        print("UART opened on %s" % args.uart)

        print("Waiting for device READY_FOR_PACKETS...")
        found_ready = wait_for_keyword(ser, "READY_FOR_PACKETS", timeout=15)

        if not found_ready:
            print("WARNING: Did not receive READY_FOR_PACKETS — proceeding anyway (USB may not be mounted)")
    except Exception as e:
        print("ERROR opening UART: %s" % e)
        sys.exit(1)

    print("Finding XInput device...")
    dev = find_device()
    print("  Found: %s" % dev)

    intf_num, out_ep = find_xinput_interface(dev)
    print("  XInput vendor interface: %d, OUT endpoint: 0x%02X" % (intf_num, out_ep))

    try:
        detach_kernel_driver(dev, intf_num)
        usb.util.claim_interface(dev, intf_num)
        print("  Interface %d claimed" % intf_num)
    except Exception as e:
        print("ERROR claiming interface: %s" % e)
        ser.close()
        sys.exit(1)

    time.sleep(0.5)

    results = []
    try:
        results.append(("Rumble", test_rumble(dev, out_ep, ser)))
        time.sleep(0.2)
        results.append(("LED", test_led(dev, out_ep, ser)))
        time.sleep(0.2)
        results.append(("Mixed", test_mixed(dev, out_ep, ser)))

        time.sleep(0.5)
        ser.write(b"DONE\n")
        time.sleep(1)
    finally:
        usb.util.release_interface(dev, intf_num)
        usb.util.dispose_resources(dev)
        ser.close()

    print("\n=== RESULTS ===")
    all_pass = True
    for name, ok in results:
        status = "PASS" if ok else "FAIL"
        print("  %s: %s" % (name, status))
        if not ok:
            all_pass = False

    if all_pass:
        print("\nALL TESTS PASSED")
    else:
        print("\nSOME TESTS FAILED")
        sys.exit(1)


if __name__ == "__main__":
    main()
