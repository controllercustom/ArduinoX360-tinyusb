// SPDX-License-Identifier: MIT
// ArduinoX360 — Output callback (rumble + LED) unit tests.
//
// Registers rumble/LED callbacks, calls pollRumble() each loop iteration,
// and prints telemetry to TELE for the pyusb test harness to verify.

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

#include <ArduinoX360.h>

static uint16_t pass = 0;
static uint16_t fail = 0;
static bool usbReady = false;

#define CHECK(expr, msg) do { \
    if (expr) { pass++; } \
    else { fail++; TELE.print("FAIL: "); TELE.println(msg); } \
} while(0)

// Callback invocation counters and last values.
static uint32_t rumbleCount = 0;
static uint8_t cbRumbleL = 0, cbRumbleR = 0;

static uint32_t ledCount = 0;
static uint8_t cbLedIndex = 0xFF;

// Test phase: 0=idle (waiting for USB), 1=running tests.
static uint8_t testPhase = 0;

void onRumble(uint8_t leftMotor, uint8_t rightMotor) {
    rumbleCount++;
    cbRumbleL = leftMotor;
    cbRumbleR = rightMotor;
    TELE_PRINTF("CB_RUMBLE:%u,%u\n", leftMotor, rightMotor);
}

void onLed(uint8_t ledIndex) {
    ledCount++;
    cbLedIndex = ledIndex;
    TELE_PRINTF("CB_LED:%u\n", ledIndex);
}

static void testInitialState() {
    TELE.println("--- Initial State ---");
    CHECK(ArduinoX360.getLastRumbleLeft() == 0, "rumbleL initial zero");
    CHECK(ArduinoX360.getLastRumbleRight() == 0, "rumbleR initial zero");
    CHECK(ArduinoX360.getLastLedIndex() == 0xFF, "led index initial unset (0xFF)");
    CHECK(!ArduinoX360.hasReceivedOutput(), "hasReceivedOutput false initially");
}

static void testCallbackRegistration() {
    TELE.println("--- Callback Registration ---");
    rumbleCount = 0;
    ledCount = 0;
    ArduinoX360.onRumble(onRumble);
    ArduinoX360.onLed(onLed);
    CHECK(true, "callbacks registered without crash");
}

static void testPollBeforeUsb() {
    TELE.println("--- Poll Before USB ---");
    uint32_t rc = rumbleCount;
    uint32_t lc = ledCount;
    ArduinoX360.pollRumble();
    CHECK(rumbleCount == rc, "rumble callback not called before USB mount");
    CHECK(ledCount == lc, "led callback not called before USB mount");
}

static void waitForUsb() {
    uint32_t t0 = millis();
    while (!ArduinoX360.ready() && (millis() - t0) < 5000UL) {
        delay(10);
    }
    if (ArduinoX360.ready()) {
        usbReady = true;
        TELE.println("USB ready");
    } else {
        TELE.println("WARNING: USB not ready after 5s — OUT packet tests will be skipped");
    }
}

static void testUsbOutput() {
    if (!usbReady) return;
    TELE.println("--- USB Output (awaiting pyusb packets) ---");
    rumbleCount = 0;
    ledCount = 0;
    ArduinoX360.releaseAll();
    delay(5);
}

static void checkRumbleRoundTrip(uint8_t expectL, uint8_t expectR) {
    CHECK(cbRumbleL == expectL, "rumble left motor value matches callback");
    CHECK(cbRumbleR == expectR, "rumble right motor value matches callback");
    CHECK(ArduinoX360.getLastRumbleLeft() == expectL, "getLastRumbleLeft matches");
    CHECK(ArduinoX360.getLastRumbleRight() == expectR, "getLastRumbleRight matches");
}

static void checkLedRoundTrip(uint8_t expectIdx) {
    CHECK(cbLedIndex == expectIdx, "led index value matches callback");
    CHECK(ArduinoX360.getLastLedIndex() == expectIdx, "getLastLedIndex matches");
    CHECK(ArduinoX360.hasReceivedOutput(), "hasReceivedOutput true after LED packet");
}

void setup() {
    TELE.begin(115200);
    while (!TELE) delay(10);

    ArduinoX360.begin();

    testInitialState();
    testCallbackRegistration();
    testPollBeforeUsb();
    waitForUsb();
    testUsbOutput();

    TELE_PRINTF("\n=== PRE-USB RESULTS: PASS=%u FAIL=%u ===\n", pass, fail);
}

void loop() {
    ArduinoX360.pollRumble();

    // Signal readiness to test harness until first packet arrives.
    static bool packetReceived = false;
    if (rumbleCount > 0 || ledCount > 0) packetReceived = true;
    static unsigned long lastReady = 0;
    if (ArduinoX360.ready() && !packetReceived && (millis() - lastReady) >= 500) {
        lastReady = millis();
        TELE.println("READY_FOR_PACKETS");
    }

    // After each callback invocation, verify state consistency.
    if (rumbleCount > 0) {
        checkRumbleRoundTrip(cbRumbleL, cbRumbleR);
    }
    if (ledCount > 0) {
        checkLedRoundTrip(cbLedIndex);
    }

    // Report summary every second while waiting for packets.
    static unsigned long lastReport = 0;
    if ((millis() - lastReport) >= 1000UL && usbReady) {
        lastReport = millis();
        TELE_PRINTF("SUMMARY: rumble=%u led=%u pass=%u fail=%u\n",
                       rumbleCount, ledCount, pass, fail);

        // Auto-exit after 30s of no new packets.
        static uint8_t idleSeconds = 0;
        if (rumbleCount == 0 && ledCount == 0) {
            idleSeconds++;
            if (idleSeconds >= 15) {
                TELE.println("\n=== TIMEOUT: No OUT packets received ===");
                TELE_PRINTF("FINAL RESULTS: PASS=%u FAIL=%u\n", pass, fail);
                while(1) delay(1000);
            }
        } else {
            idleSeconds = 0;
        }
    }

    // Check for DONE command from test harness.
    if (TELE.available()) {
        String cmd = TELE.readStringUntil('\n');
        cmd.trim();
        if (cmd == "DONE") {
            uint32_t finalRumble = rumbleCount;
            uint32_t finalLed = ledCount;
            TELE_PRINTF("\n=== FINAL RESULTS: PASS=%u FAIL=%u RUMBLE_PKTS=%u LED_PKTS=%u ===\n",
                           pass, fail, finalRumble, finalLed);
            if (fail == 0 && finalRumble > 0 && finalLed > 0) {
                TELE.println("ALL TESTS PASSED");
            } else if (finalRumble == 0 || finalLed == 0) {
                TELE.println("INCOMPLETE: Not all packet types received");
            } else {
                TELE.println("TESTS FAILED");
            }
            while(1) delay(1000);
        }
    }

    delay(2);
}
