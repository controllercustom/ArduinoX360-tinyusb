#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Deep baseline validation for ArduinoX360-tinyusb AutoCycle capture.

Validates exact per-phase values across all reconstructed cycles:
- P0/P7c/P8 idle: strict all-zero reports
- P1 button table order (11 buttons, 3 passes)
- P2 d-pad ordered sweep 0..7 (ordered subsequence)
- P3/P4 stick linear sweep exact values, no cross-contamination
- P5/P6 trigger ramp exact uint8 values
- P7a all-input burst exact values (wButtons=0xF7F0, L(-16384,-16384),
  R(+16384,+16384), LT/RT=127)
- P7b rapid-fire A toggle transition count

Phase markers: all 11 buttons + DPAD_UP -> wButtons=0xF7F1 (LOGO=bit10, bit11
unused in base XInput mapping). P7a drops DPAD_UP -> 0xF7F0, so it never
collides with a marker. Strict marker also requires all analog values zero.

Cycle structure: each full cycle has 11 marker-delimited segments in order
P0,P1,P2,P3,P4,P5,P6,P7a,P7b,P7c,P8. The capture typically starts mid-P0, so
full cycles are reconstructed from the first complete P0 segment onward.

Usage:
    python3 validate_xinput_baseline.py [leftover_data.txt]
"""
import argparse
import re
import sys
from collections import Counter

# Import shared decoding/constants from sibling analyzer.
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from analyze_xinput_pcap import (  # noqa: E402
    is_marker, parse_report, MARKER_BUTTONS, HAT_TO_BUTTONS,
    STICK_LINEAR_X, TRIGGER_RAMP_U8,
)

# P1 button table order (bit index -> name), matching AutoCycle's buttonTable[].
P1_ORDER = [
    (4, "START"), (5, "BACK"), (6, "LTHUMB"), (7, "RTHUMB"),
    (8, "LB"), (9, "RB"), (10, "XBOX"), (12, "A"), (13, "B"), (14, "X"), (15, "Y"),
]
P1_TABLE_SIZE = len(P1_ORDER)

# P2 d-pad ordered sweep (AutoCycle dpadSweep = 0..8; the 8 = centered/neutral
# produces 0x0000 which is indistinguishable from releaseAll() reset frames, so
# only the 8 directions 0..7 are validated as an ordered subsequence).
DPAD_ORDER = [HAT_TO_BUTTONS[i] for i in range(8)]

P7A_BUTTONS = 0xF7F0  # all 11 main buttons, no DPAD_UP


def classify_segment(seg):
    """Classify a marker-delimited segment into a phase name."""
    sticks_zero = all(r["sLX"] == 0 and r["sLY"] == 0 and r["sRX"] == 0 and r["sRY"] == 0 for r in seg)
    trig_zero = all(r["lt"] == 0 and r["rt"] == 0 for r in seg)
    btn_masks = set(r["wButtons"] & 0xFFF0 for r in seg)
    has_p7a = any(r["wButtons"] == P7A_BUTTONS and r["lt"] == 127 and r["rt"] == 127 for r in seg)

    # P7a: exact all-input burst frame present
    if has_p7a:
        return "P7a"

    # P7b: only A (bit 12) toggling, nothing else
    single_btn_masks = [m for m in btn_masks if m and (m & (m - 1)) == 0]
    multi_btn_masks = [m for m in btn_masks if m and (m & (m - 1)) != 0]
    if single_btn_masks == [0x1000] and not multi_btn_masks and trig_zero and sticks_zero:
        return "P7b"

    # P1: single buttons cycling (0x0010..0x8000 individual bits)
    if single_btn_masks and not multi_btn_masks and trig_zero and sticks_zero:
        return "P1"

    # P2: d-pad (bits 0-3 only)
    btn_bits = set()
    for r in seg:
        for b in range(16):
            if r["wButtons"] & (1 << b):
                btn_bits.add(b)
    if btn_bits and btn_bits <= {0, 1, 2, 3} and sticks_zero and trig_zero:
        hats = set()
        for r in seg:
            for h, btns in HAT_TO_BUTTONS.items():
                if (r["wButtons"] & 0xF) == btns:
                    hats.add(h)
        if hats:
            return "P2"

    # Stick sweeps (P3/P4)
    if not btn_bits and trig_zero:
        lx_set = set(r["sLX"] for r in seg)
        rx_set = set(r["sRX"] for r in seg)
        if lx_set >= set(STICK_LINEAR_X):
            return "P3"
        if rx_set >= set(STICK_LINEAR_X):
            return "P4"

    # Trigger ramps (P5/P6)
    if not btn_bits and sticks_zero and (any(r["lt"] > 0 for r in seg) or any(r["rt"] > 0 for r in seg)):
        if all(r["rt"] == 0 for r in seg):
            return "P5"
        if all(r["lt"] == 0 for r in seg):
            return "P6"

    # Idle (P0/P7c/P8): strict zero
    if sticks_zero and trig_zero and not btn_bits:
        return "IDLE"

    return "OTHER"


PHASE_ORDER = ["P0", "P1", "P2", "P3", "P4", "P5", "P6", "P7a", "P7b", "P7c", "P8"]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", nargs="?", default="/tmp/leftover_data.txt",
                    help="file of `Leftover Capture Data` lines from tshark -V")
    args = ap.parse_args()

    frames = []
    with open(args.path) as f:
        for line in f:
            m = re.search(r"([0-9a-fA-F]{40})", line)
            if m:
                r = parse_report(m.group(1))
                if r:
                    frames.append(r)

    if not frames:
        print("No frames parsed.")
        return 1

    # Split at markers (each marker frame is a phase boundary). Drop the
    # marker frames themselves and keep the data segments in order.
    segments = []
    cur = []
    for r in frames:
        if is_marker(r):
            if cur:
                segments.append(cur)
                cur = []
        else:
            cur.append(r)
    if cur:
        segments.append(cur)

    print(f"Frames: {len(frames)}, marker-delimited data segments: {len(segments)}")

    # Classify each segment, then reconstruct cycles: a full cycle is a run of
    # 11 segments in order P0,P1,P2,P3,P4,P5,P6,P7a,P7b,P7c,P8. Capture often
    # starts mid-cycle, so find the first full P0 and slice every 11 segments.
    classified = [classify_segment(s) for s in segments]
    print("Segment classification:")
    for i, c in enumerate(classified):
        print(f"  Seg{i:2d}: {c}")

    # Reconstruct cycles. P0/P7c/P8 are content-identical strict-idle segments,
    # so anchor on the distinctive 8-segment run P1,P2,P3,P4,P5,P6,P7a,P7b:
    # the IDLE before it is P0, and the two IDLEs after it are P7c and P8.
    cycles = []
    i = 0
    while i + 9 < len(classified):
        if classified[i:i + 8] == ["P1", "P2", "P3", "P4", "P5", "P6", "P7a", "P7b"]:
            if i >= 1 and classified[i - 1] == "IDLE" and classified[i + 8] == "IDLE" and classified[i + 9] == "IDLE":
                cycles.append(segments[i - 1:i + 10])
                i += 10
                continue
        i += 1

    print(f"\nFull cycles reconstructed (exact P0..P8 pattern): {len(cycles)}")

    errors = []
    results = {ph: [] for ph in PHASE_ORDER}

    for ci, cyc in enumerate(cycles):
        for si, seg in enumerate(cyc):
            results[PHASE_ORDER[si]].append((ci, seg))

    def subseq(needle, hay):
        it = iter(hay)
        return all(any(x == n for x in it) for n in needle)

    # P0/P7c/P8: all frames strictly zero
    for ph in ["P0", "P7c", "P8"]:
        counts = Counter()
        for ci, seg in results[ph]:
            for r in seg:
                counts[(r["wButtons"], r["lt"], r["rt"], r["sLX"], r["sLY"], r["sRX"], r["sRY"])] += 1
        non_zero_kinds = {k for k, c in counts.items() if c and k != (0, 0, 0, 0, 0, 0, 0)}
        if non_zero_kinds:
            errors.append(f"{ph}: {len(non_zero_kinds)} non-zero frame kinds across {len(results[ph])} segments: {list(non_zero_kinds)[:3]}")
        else:
            print(f"  {ph}: strict zero state PASS ({len(results[ph])} segments, {sum(len(s) for _, s in results[ph])} frames)")

    # P1: all 11 buttons present (btnIdx advances every iteration, release
    # every 3rd, so the wire order is a rotation of buttonTable[] — validate
    # presence + no contamination, not the table order).
    for ci, seg in results["P1"]:
        seen = set()
        for r in seg:
            for bit, _ in P1_ORDER:
                if r["wButtons"] == (1 << bit):
                    seen.add(bit)
        missing = [b for b, _ in P1_ORDER if b not in seen]
        if missing:
            errors.append(f"P1 cycle {ci}: missing buttons {missing}")
        else:
            print(f"  P1 cycle {ci}: all {len(seen)} buttons present")
        # Contamination check
        if any(r["wButtons"] != 0 and (r["lt"] != 0 or r["rt"] != 0) for r in seg):
            errors.append(f"P1 cycle {ci}: trigger contamination")
        if any(r["wButtons"] != 0 and (r["sLX"] or r["sLY"] or r["sRX"] or r["sRY"]) for r in seg):
            errors.append(f"P1 cycle {ci}: stick contamination")

    # P2: d-pad ordered sweep 0..7 as subsequence
    for ci, seg in results["P2"]:
        it = iter(r["wButtons"] & 0xF for r in seg)
        ok = True
        for want in DPAD_ORDER:
            if not any((b & 0xF) == want for b in it):
                ok = False
                break
        if not ok:
            errors.append(f"P2 cycle {ci}: d-pad sweep order broken")
        else:
            print(f"  P2 cycle {ci}: d-pad ordered sweep PASS")

    # P3/P4: stick linear values exact, no contamination
    for ph, field in [("P3", "sLX"), ("P4", "sRX")]:
        for ci, seg in results[ph]:
            vals = [r[field] for r in seg]
            if not subseq(STICK_LINEAR_X, vals):
                errors.append(f"{ph} cycle {ci}: linear X sweep missing values: {sorted(set(vals))}")
            other = "sRX" if field == "sLX" else "sLX"
            other_y = "sRY" if field == "sLX" else "sLY"
            if any(r[other] != 0 or r[other_y] != 0 for r in seg):
                errors.append(f"{ph} cycle {ci}: cross-contamination on {other}")
            if any(r["lt"] != 0 or r["rt"] != 0 for r in seg):
                errors.append(f"{ph} cycle {ci}: trigger contamination")
        print(f"  {ph}: {len(results[ph])} segments validated (linear sweep + no cross-contamination)")

    # P5/P6: trigger ramp exact values
    for ph, field in [("P5", "lt"), ("P6", "rt")]:
        for ci, seg in results[ph]:
            vals = [r[field] for r in seg]
            if not subseq(TRIGGER_RAMP_U8, vals):
                errors.append(f"{ph} cycle {ci}: trigger ramp {vals} missing values")
            present = set(vals)
            missing = set(TRIGGER_RAMP_U8) - present
            if missing:
                errors.append(f"{ph} cycle {ci}: missing ramp values {sorted(missing)}")
            other = "rt" if field == "lt" else "lt"
            if any(r[other] != 0 for r in seg):
                errors.append(f"{ph} cycle {ci}: {other} trigger contamination")
        print(f"  {ph}: {len(results[ph])} segments, exact values present")

    # P7a: exact all-input burst
    for ci, seg in results["P7a"]:
        burst_frames = [r for r in seg if r["lt"] == 127 and r["rt"] == 127]
        if not burst_frames:
            errors.append(f"P7a cycle {ci}: no burst frame with LT=RT=127 found")
            continue
        for r in burst_frames:
            if r["wButtons"] != P7A_BUTTONS:
                errors.append(f"P7a cycle {ci}: wButtons=0x{r['wButtons']:04X} != 0x{P7A_BUTTONS:04X}")
            if (r["sLX"], r["sLY"]) != (-16384, -16384):
                errors.append(f"P7a cycle {ci}: left stick ({r['sLX']},{r['sLY']}) != (-16384,-16384)")
            if (r["sRX"], r["sRY"]) != (16384, 16384):
                errors.append(f"P7a cycle {ci}: right stick ({r['sRX']},{r['sRY']}) != (16384,16384)")
        print(f"  P7a cycle {ci}: burst frame count={len(burst_frames)} values exact")

    # P7b: rapid-fire A toggle transitions
    for ci, seg in results["P7b"]:
        transitions = 0
        prev_a = None
        for r in seg:
            a_state = bool(r["wButtons"] & (1 << 12))
            if prev_a is not None and a_state != prev_a:
                transitions += 1
            prev_a = a_state
        print(f"  P7b cycle {ci}: {transitions} A press/release transitions")

    print(f"\n=== Total frames: {len(frames)} ===")
    print(f"=== Phase segment counts ===")
    for ph in PHASE_ORDER:
        print(f"  {ph}: {len(results[ph])}")

    print(f"\n=== Errors: {len(errors)} ===")
    for e in errors[:20]:
        print(f"  !! {e}")
    if not errors:
        print("  None — exact-value baseline validation PASS")
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
