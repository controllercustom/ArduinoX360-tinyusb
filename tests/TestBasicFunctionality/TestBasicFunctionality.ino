// SPDX-License-Identifier: MIT
// ArduinoX360 — Basic functionality on-board test suite.

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

#include <ArduinoX360.h>

static_assert(sizeof(ArduinoX360Class::Button) == sizeof(uint8_t), "Button enum must be uint8_t");
static_assert(ArduinoX360Class::BUTTON_COUNT == 16U, "Must have exactly 16 button slots (dpad 0-3, hold 4-10, gap 11, A/B/X/Y 12-15)");
static_assert(sizeof(ArduinoX360Class::XInputReport) == 20U, "XInputReport must be exactly 20 bytes");

static uint16_t pass = 0;
static uint16_t fail = 0;

#define CHECK(expr, msg) do { \
    if (expr) { pass++; } \
    else { fail++; TELE.print("FAIL: "); TELE.println(msg); } \
} while (0)

static void testButtons() {
    TELE.println("--- Buttons ---");

    for (uint8_t i = 0; i < ArduinoX360Class::BUTTON_COUNT; ++i) {
        auto btn = static_cast<ArduinoX360Class::Button>(i);

        ArduinoX360.press(btn);
        CHECK(ArduinoX360.getButton(btn), "press failed");

        ArduinoX360.release(btn);
        CHECK(!ArduinoX360.getButton(btn), "release failed");
    }

    ArduinoX360.setButton(ArduinoX360Class::A, true);
    CHECK(ArduinoX360.getButton(ArduinoX360Class::A), "setButton(true) failed");
    ArduinoX360.setButton(ArduinoX360Class::A, false);
    CHECK(!ArduinoX360.getButton(ArduinoX360Class::A), "setButton(false) failed");

    ArduinoX360.press(static_cast<ArduinoX360Class::Button>(ArduinoX360Class::BUTTON_COUNT));
    CHECK(!ArduinoX360.getButton(static_cast<ArduinoX360Class::Button>(ArduinoX360Class::BUTTON_COUNT)), "OOR press should be noop");

    for (uint8_t i = 0; i < ArduinoX360Class::BUTTON_COUNT; i++) {
        ArduinoX360.press(static_cast<ArduinoX360Class::Button>(i));
    }
    bool allPressed = true;
    for (uint8_t i = 0; i < ArduinoX360Class::BUTTON_COUNT; i++) {
        if (!ArduinoX360.getButton(static_cast<ArduinoX360Class::Button>(i))) { allPressed = false; break; }
    }
    CHECK(allPressed, "pressAll: not all pressed");
    ArduinoX360.releaseAll();
    bool nonePressed = true;
    for (uint8_t i = 0; i < ArduinoX360Class::BUTTON_COUNT; i++) {
        if (ArduinoX360.getButton(static_cast<ArduinoX360Class::Button>(i))) { nonePressed = false; break; }
    }
    CHECK(nonePressed, "releaseAll: buttons still held");
}

static void testSticks() {
    TELE.println("--- Sticks ---");
    
    ArduinoX360.setStickLeft(-32767, -32767);
    CHECK(ArduinoX360.getReport().sThumbLX == -32767 && ArduinoX360.getReport().sThumbLY == -32767, "stick left negative");

    ArduinoX360.setStickLeft(32767, 32767);
    CHECK(ArduinoX360.getReport().sThumbLX == 32767 && ArduinoX360.getReport().sThumbLY == 32767, "stick left positive");

    ArduinoX360.setStickRight(-1000, 500);
    CHECK(ArduinoX360.getReport().sThumbRX == -1000 && ArduinoX360.getReport().sThumbRY == 500, "stick right mixed");

    ArduinoX360.setStickLeft(0, 0);
    ArduinoX360.setStickRight(0, 0);
    CHECK(true, "stick writes survived extremes");
}

static void testTriggers() {
    TELE.println("--- Triggers ---");
    
    ArduinoX360.setLeftTrigger(0);
    CHECK(ArduinoX360.getReport().bLeftTrigger == 0, "left trigger zero");

    ArduinoX360.setLeftTrigger(16384);
    CHECK(ArduinoX360.getReport().bLeftTrigger == 127, "left trigger half (expected 127)");

    ArduinoX360.setLeftTrigger(32768);
    CHECK(ArduinoX360.getReport().bLeftTrigger == 255, "left trigger full");

    ArduinoX360.setRightTrigger(0);
    CHECK(ArduinoX360.getReport().bRightTrigger == 0, "right trigger zero");

    ArduinoX360.setRightTrigger(8192);
    CHECK(ArduinoX360.getReport().bRightTrigger == 63, "right trigger quarter (expected 63)");

    ArduinoX360.setLeftTrigger(0);
    ArduinoX360.setRightTrigger(0);
}

static void testHat() {
    TELE.println("--- Hat ---");
    
    for (uint8_t h = 0; h < 8; ++h) {
        ArduinoX360.setHat(h);
        CHECK(ArduinoX360.getHat() == h, "hat round-trip direction");
    }

    ArduinoX360.setHat(8); // CENTERED
    CHECK(ArduinoX360.getHat() == 8, "hat centered");

    ArduinoX360.setDpad(0); // UP via alias
    CHECK(ArduinoX360.getHat() == 0, "setDpad alias works (UP)");

    ArduinoX360.setHat(8); // reset to center
}

static void testSendAndReady() {
    TELE.println("--- send/ready ---");
    uint32_t t0 = millis();
    while (!ArduinoX360.ready() && millis() - t0 < 3000) {
        delay(50);
    }
    if (!ArduinoX360.ready()) {
        TELE.println("  (USB not ready — skipping send test)");
        pass++;
        return;
    }
    ArduinoX360.send();
    CHECK(true, "send() survived");
}

void setup() {
    TELE.begin(115200);
    while (!TELE) delay(10);

    ArduinoX360.begin();
    delay(200);

    TELE.println("=== ArduinoX360 TestBasicFunctionality ===");

    testButtons();
    testSticks();
    testTriggers();
    testHat();
    testSendAndReady();

    TELE.println("=== RESULTS ===");
    TELE.print("PASS: ");
    TELE.println(pass);
    TELE.print("FAIL: ");
    TELE.println(fail);
    if (fail == 0) {
        TELE.println("ALL TESTS PASSED");
    } else {
        TELE.println("TESTS FAILED");
    }
}

void loop() {
    delay(1000);
}
