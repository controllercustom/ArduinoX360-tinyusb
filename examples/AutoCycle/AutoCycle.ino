// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com

// AutoCycle — phased, deterministic test for pcap analysis and verification.
// 9 phases P0-P8 with phase markers between transitions, callback telemetry,
// disabled auto-poll timer, explicit send() per iteration only.

#include <ArduinoX360.h>
#if defined(ARDUINO_ARCH_RP2040)
  #define TELE Serial1
#elif defined(ARDUINO_ARCH_ESP32)
  #define TELE Serial0
#elif defined(ARDUINO_ARCH_SAMD)
  #ifdef XINPUT_DEBUG_CDC
    #define TELE Serial
  #else
    #define TELE Serial1
  #endif
#elif defined(ARDUINO_ARCH_NRF52)
  #ifdef XINPUT_DEBUG_CDC
    #define TELE Serial
  #else
    #define TELE Serial1
  #endif
#elif defined(ARDUINO_ARCH_RENESAS)
  #define TELE Serial1
#else
  #define TELE Serial
#endif

#include <cstdio>
#define TELE_PRINTF(fmt, ...) do { char _b[160]; snprintf(_b, sizeof(_b), fmt, __VA_ARGS__); TELE.print(_b); } while(0)


static const uint16_t BTN_NAMES[] = {
    ArduinoX360Class::DPAD_UP,   // 0 (also setHat handles this)
};

#define PHASE_MARKER_DELAY_MS 8U
#define ITERATION_DELAY_MS    10U

// --- Phase marker: unmistakable all-buttons frame for pcap boundary detection.
static void sendPhaseMarker() {
    ArduinoX360.releaseAll();
    delay(ITERATION_DELAY_MS);

    // Press every button (excluding d-pad bits which are set by setHat and gap 11).
    for (uint8_t i = 4; i < ArduinoX360Class::BUTTON_COUNT; ++i) {
        if (i == 11) continue; // gap
        ArduinoX360.press(static_cast<ArduinoX360Class::Button>(i));
    }
    // Also press d-pad buttons via setHat(0) — UP.
    ArduinoX360.setHat(0);

    TELE.println("MARKER:ALL_BUTTONS");
    ArduinoX360.send();
    delay(PHASE_MARKER_DELAY_MS);

    ArduinoX360.releaseAll();
    ArduinoX360.send();
}

// --- Button table for P1 (all 15 buttons, excluding d-pad which is in wButtons bits 0-3).
static const struct { uint8_t idx; const char* name; } buttonTable[] = {
    // D-pad buttons handled via setHat() — not individual press/release.
    {ArduinoX360Class::START,       "START"},
    {ArduinoX360Class::BACK,        "BACK"},
    {ArduinoX360Class::LEFT_THUMB,  "LTHUMB"},
    {ArduinoX360Class::RIGHT_THUMB, "RTHUMB"},
    {ArduinoX360Class::LEFT_SHOULDER,  "LB"},
    {ArduinoX360Class::RIGHT_SHOULDER, "RB"},
    {ArduinoX360Class::XBOX,        "XBOX"},
    {ArduinoX360Class::A,           "A"},
    {ArduinoX360Class::B,           "B"},
    {ArduinoX360Class::X,           "X"},
    {ArduinoX360Class::Y,           "Y"},
};

#define BTN_TABLE_SIZE (sizeof(buttonTable) / sizeof(buttonTable[0]))

// --- Stick sweep tables for P3/P4.
static const struct { int16_t x; int16_t y; } stickLinearSweep[] = {
    {-29491,  0     }, // ~-90% X
    {-19660,  0     }, // ~-60% X
    {-9830 ,  0     }, // ~-30% X
    {      0,  0     }, // center
    {+9830 ,  0     }, // +30% X
    {+19660,  0     }, // +60% X
    {+29491,  0     }, // +90% X

    {      0,-29491}, // ~-90% Y
    {      0,-19660}, // ~-60% Y
    {      0,-9830 }, // ~-30% Y
    {      0,     0}, // center
    {      0,+9830 }, // +30% Y
    {      0,+19660}, // +60% Y
    {      0,+29491}, // +90% Y

    {     -32768,   -32768}, // bottom-left extreme (note: XInput uses signed range)
};

#define STICK_LINEAR_SIZE (sizeof(stickLinearSweep) / sizeof(stickLinearSweep[0]))

static const struct { int16_t x; int16_t y; } stickCircle[] = {
    {-9830, -9830}, // ~50% radius: bottom-left quadrant → actually top-left in XInput coords (Y inverted)
    {+9830, -9830}, // top-right
    {+9830, +9830}, // bottom-right
    {-9830, +9830}, // bottom-left

    {-14745,-14745}, // ~75% radius: same quadrants
    {+14745,-14745},
    {+14745,+14745},
    {-14745,+14745},

    {0, 0}, // return to center
};

#define STICK_CIRCLE_SIZE (sizeof(stickCircle) / sizeof(stickCircle[0]))

// --- Trigger ramp table for P5/P6.
static const uint16_t triggerRamp[] = {
    0, 4096, 8192, 16384, 24576, 32768, 16384, 0
};

#define TRIGGER_RAMP_SIZE (sizeof(triggerRamp) / sizeof(triggerRamp[0]))

// --- D-pad sweep table for P2.
static const uint8_t dpadSweep[] = {
    0, // UP
    1, // UP_RIGHT
    2, // RIGHT
    3, // DOWN_RIGHT
    4, // DOWN
    5, // DOWN_LEFT
    6, // LEFT
    7, // UP_LEFT
    8, // CENTERED (hold)
    8, // CENTERED (confirm clean state)
};

#define DPAD_SWEEP_SIZE (sizeof(dpadSweep) / sizeof(dpadSweep[0]))

// --- Phase iteration counts.
static const uint16_t PHASE_IDLE_ITERS = 50;   // P0 and P8: ~5s each at 10ms/iter
#define BTN_PASSES     3                        // P1: full passes over button table (press×2, release×1 per btn)

void setup() {
    TELE.begin(115200);
    delay(200);

    ArduinoX360.setPollInterval(0);

    ArduinoX360.onRumble([](uint8_t lMotor, uint8_t rMotor) {
        TELE_PRINTF("CB_RUMBLE:left=%u,right=%u\n", lMotor, rMotor);
    });
    ArduinoX360.onLed([](uint8_t idx) {
        TELE_PRINTF("CB_LED:%u\n", idx);
    });

    ArduinoX360.begin();

    unsigned long start = millis();
    while (!ArduinoX360.isConnected() && (millis() - start) < 5000UL) {
        delay(10);
    }
    TELE.println("READY");
}

void loop() {
    static uint8_t phase = 0;
    static uint16_t seqInPhase = 0;
    static uint32_t globalSeq = 0;

    // --- P0: Idle baseline (~5s). Clean zero-state capture window.
    if (phase == 0) {
        ArduinoX360.releaseAll();
        TELE_PRINTF("PHASE=0 SEQ=%u TS:%lld\n", seqInPhase, (int64_t)micros());

        if (++seqInPhase >= PHASE_IDLE_ITERS) {
            sendPhaseMarker();
            phase = 1;
            seqInPhase = 0;
        }
    }

    // --- P1: Individual button cycle. Each btn pressed for 2 iters, released for 1. Multiple passes.
    else if (phase == 1) {
        ArduinoX360.releaseAll();

        uint8_t pass = seqInPhase / BTN_TABLE_SIZE;
        uint8_t btnIdx = seqInPhase % BTN_TABLE_SIZE;
        bool pressHold = (seqInPhase % 3 != 0); // Press on iter N, hold on N+1, release on N+2.

        if (pressHold) {
            ArduinoX360.press(static_cast<ArduinoX360Class::Button>(buttonTable[btnIdx].idx));
            TELE_PRINTF("PHASE=1 SEQ=%u PASS=%d BTN=%s TS:%lld\n", seqInPhase, pass + 1, buttonTable[btnIdx].name, (int64_t)micros());
        } else {
            ArduinoX360.release(static_cast<ArduinoX360Class::Button>(buttonTable[btnIdx].idx));
            TELE_PRINTF("PHASE=1 SEQ=%u PASS=%d BTN=%s(REL) TS:%lld\n", seqInPhase, pass + 1, buttonTable[btnIdx].name, (int64_t)micros());
        }

        if (++seqInPhase >= BTN_TABLE_SIZE * BTN_PASSES) {
            sendPhaseMarker();
            phase = 2;
            seqInPhase = 0;
        }
    }

    // --- P2: D-pad cardinal + diagonal sweep.
    else if (phase == 2) {
        ArduinoX360.releaseAll();
        ArduinoX360.setStickLeft(0, 0);
        ArduinoX360.setStickRight(0, 0);

        uint8_t dpadDir = dpadSweep[seqInPhase];
        ArduinoX360.setHat(dpadDir);

        TELE_PRINTF("PHASE=2 SEQ=%u DPAD=%d TS:%lld\n", seqInPhase, dpadDir, (int64_t)micros());

        if (++seqInPhase >= DPAD_SWEEP_SIZE) {
            sendPhaseMarker();
            phase = 3;
            seqInPhase = 0;
        }
    }

    // --- P3: Left stick linear sweep + circle. Right stick centered throughout.
    else if (phase == 3) {
        ArduinoX360.releaseAll();

        int16_t sx, sy;
        const char* label = "LINEAR";
        uint8_t stepIdx = seqInPhase % STICK_LINEAR_SIZE;

        if (seqInPhase < STICK_LINEAR_SIZE) {
            // Linear sweep.
            sx = stickLinearSweep[stepIdx].x;
            sy = stickLinearSweep[stepIdx].y;
        } else {
            // Circle sub-sequence.
            label = "CIRCLE";
            stepIdx = seqInPhase - STICK_LINEAR_SIZE;
            if (stepIdx < STICK_CIRCLE_SIZE) {
                sx = stickCircle[stepIdx].x;
                sy = stickCircle[stepIdx].y;
            } else {
                // Past circle — return to center.
                sx = 0;
                sy = 0;
            }
        }

        ArduinoX360.setStickLeft(sx, sy);
        ArduinoX360.setStickRight(0, 0);

        TELE_PRINTF("PHASE=3 SEQ=%u %s LX=(%d,%d) TS:%lld\n", seqInPhase, label, sx, sy, (int64_t)micros());

        uint16_t totalIters = STICK_LINEAR_SIZE + STICK_CIRCLE_SIZE;
        if (++seqInPhase >= totalIters) {
            sendPhaseMarker();
            phase = 4;
            seqInPhase = 0;
        }
    }

    // --- P4: Right stick sweep (mirrors P3). Left stick centered throughout.
    else if (phase == 4) {
        ArduinoX360.releaseAll();

        int16_t sx, sy;
        const char* label = "LINEAR";
        uint8_t stepIdx = seqInPhase % STICK_LINEAR_SIZE;

        if (seqInPhase < STICK_LINEAR_SIZE) {
            sx = stickLinearSweep[stepIdx].x;
            sy = stickLinearSweep[stepIdx].y;
        } else {
            label = "CIRCLE";
            stepIdx = seqInPhase - STICK_LINEAR_SIZE;
            if (stepIdx < STICK_CIRCLE_SIZE) {
                sx = stickCircle[stepIdx].x;
                sy = stickCircle[stepIdx].y;
            } else {
                sx = 0;
                sy = 0;
            }
        }

        ArduinoX360.setStickLeft(0, 0);
        ArduinoX360.setStickRight(sx, sy);

        TELE_PRINTF("PHASE=4 SEQ=%u %s RX=(%d,%d) TS:%lld\n", seqInPhase, label, sx, sy, (int64_t)micros());

        uint16_t totalIters = STICK_LINEAR_SIZE + STICK_CIRCLE_SIZE;
        if (++seqInPhase >= totalIters) {
            sendPhaseMarker();
            phase = 5;
            seqInPhase = 0;
        }
    }

    // --- P5: L-trigger smooth ramp. R-trigger held at 0, sticks centered, no buttons.
    else if (phase == 5) {
        ArduinoX360.releaseAll();
        ArduinoX360.setStickLeft(0, 0);
        ArduinoX360.setStickRight(0, 0);

        uint16_t ltVal = triggerRamp[seqInPhase];
        ArduinoX360.setLeftTrigger(ltVal);
        ArduinoX360.setRightTrigger(0);

        TELE_PRINTF("PHASE=5 SEQ=%u LT=%d TS:%lld\n", seqInPhase, ltVal, (int64_t)micros());

        if (++seqInPhase >= TRIGGER_RAMP_SIZE) {
            sendPhaseMarker();
            phase = 6;
            seqInPhase = 0;
        }
    }

    // --- P6: R-trigger smooth ramp. L-trigger held at 0.
    else if (phase == 6) {
        ArduinoX360.releaseAll();
        ArduinoX360.setStickLeft(0, 0);
        ArduinoX360.setStickRight(0, 0);

        uint16_t rtVal = triggerRamp[seqInPhase];
        ArduinoX360.setLeftTrigger(0);
        ArduinoX360.setRightTrigger(rtVal);

        TELE_PRINTF("PHASE=6 SEQ=%u RT=%d TS:%lld\n", seqInPhase, rtVal, (int64_t)micros());

        if (++seqInPhase >= TRIGGER_RAMP_SIZE) {
            sendPhaseMarker();
            phase = 7;
            seqInPhase = 0;
        }
    }

    // --- P7a: All inputs simultaneously — max complexity report encoding.
    else if (phase == 7) {
        ArduinoX360.releaseAll();

        for (uint8_t i = 0; i < BTN_TABLE_SIZE; ++i) {
            ArduinoX360.press(static_cast<ArduinoX360Class::Button>(buttonTable[i].idx));
        }
        ArduinoX360.setHat(0); // UP.
        ArduinoX360.setStickLeft(-16384, -16384);
        ArduinoX360.setStickRight(+16384, +16384);
        ArduinoX360.setLeftTrigger(16384);
        ArduinoX360.setRightTrigger(16384);

        TELE_PRINTF("PHASE=7a SEQ=%u ALL_INPUTS TS:%lld\n", seqInPhase, (int64_t)micros());

        if (++seqInPhase >= 5) {
            sendPhaseMarker();
            phase = 8;
            seqInPhase = 0;
        }
    }

    // --- P7b: Rapid-fire toggle of button A. Everything else zeroed.
    else if (phase == 8) {
        ArduinoX360.releaseAll();

        bool pressA = ((seqInPhase % 2) == 1);
        ArduinoX360.setButton(ArduinoX360Class::A, pressA);

        TELE_PRINTF("PHASE=7b SEQ=%u RAPID_FIRE(A=%d) TS:%lld\n", seqInPhase, pressA ? 1 : 0, (int64_t)micros());

        if (++seqInPhase >= 25) {
            sendPhaseMarker();
            phase = 9;
            seqInPhase = 0;
        }
    }

    // --- P7c: Trailing idle — clean zero reports to verify recovery after stress.
    else if (phase == 9) {
        ArduinoX360.releaseAll();

        TELE_PRINTF("PHASE=7c SEQ=%u STRESS_IDLE TS:%lld\n", seqInPhase, (int64_t)micros());

        if (++seqInPhase >= PHASE_IDLE_ITERS / 4) { // ~1.25s idle after stress
            sendPhaseMarker();
            phase = 10;
            seqInPhase = 0;
        }
    }

    // --- P8: Sustained idle (~5s). Stable capture window at end of cycle.
    else {
        ArduinoX360.releaseAll();
        TELE_PRINTF("PHASE=8 SEQ=%u TS:%lld\n", seqInPhase, (int64_t)micros());

        if (++seqInPhase >= PHASE_IDLE_ITERS) {
            // Full cycle complete — restart from P0.
            sendPhaseMarker();
            phase = 0;
            seqInPhase = 0;
            globalSeq = 0;
        }
    }

    ArduinoX360.send();
    ArduinoX360.pollRumble();
    delay(ITERATION_DELAY_MS);
}

