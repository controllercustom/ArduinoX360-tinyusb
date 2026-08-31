// SPDX-License-Identifier: MIT
// ArduinoX360 — Latency benchmark emitter.
//
// Waits for "START" over Serial, then emits timestamped HID report events
// (buttons, sticks, triggers) at high speed. Attach a host-side python script
// that reads these timestamps and correlates them with Linux evdev events
// to measure end-to-end USB HID latency.
//
// Serial protocol:
//   TX lines:  TS:<us>:<seq>:<type>:<value>
//   TX lines:  BTN:<name>          (button enum -> evdev name mapping)
//   TX lines:  RPT:<payload_bytes>:<btn_count>
//   TX lines:  READY / DONE
//   RX line:   START               (begins measurement run)
//   RX line:   SYNC                (responds with SYNC:<us> for clock align)

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
#if defined(ARDUINO_ARCH_ESP32)
#include "esp_timer.h"
#endif

static volatile uint64_t g_seq = 0;
static volatile bool g_running = false;

#define BTN_ITERATIONS   4
#define ANALOG_ITERATIONS 50

enum : uint8_t {
    TYPE_BTN_PRESS   = 0,
    TYPE_BTN_RELEASE = 1,
    TYPE_LEFT_STICK  = 2,
    TYPE_RIGHT_STICK = 3,
    TYPE_L_TRIGGER   = 4,
    TYPE_R_TRIGGER   = 5,
    TYPE_DPAD        = 6,
    TYPE_MARKER      = 99,
};

static const char* buttonNames[] = {
    "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT",
    "START", "BACK", "LEFT_THUMB", "RIGHT_THUMB",
    "LEFT_SHOULDER", "RIGHT_SHOULDER", "XBOX",
    "(gap)",
    "A", "B", "X", "Y"
};

static void printULL(uint64_t v) {
    char buf[21];
    char *p = buf + sizeof(buf);
    *--p = '\0';
    if (v == 0) {
        *--p = '0';
    } else {
        while (v > 0) {
            *--p = char('0' + (v % 10));
            v /= 10;
        }
    }
    TELE.print(p);
}

static void emit(uint64_t ts_us, uint8_t type, uint16_t val) {
    g_seq++;
    TELE.print("TS:");
    printULL(ts_us);
    TELE.print(":");
    printULL(g_seq);
    TELE.print(":");
    TELE.print(type);
    TELE.print(":");
    TELE.println(val);
}

void setup() {
    TELE.begin(115200);

    ArduinoX360.begin();

    uint32_t waitStart = millis();
    while (!ArduinoX360.isConnected() && (millis() - waitStart) < 5000UL) {
        delay(10);
    }

    TELE.print("RPT:");
    TELE.print(sizeof(ArduinoX360Class::XInputReport));
    TELE.print(":");
    TELE.print(XINPUT_REPORT_SIZE);
    TELE.print(":");
    TELE.println(ArduinoX360Class::BUTTON_COUNT);

    for (uint8_t i = 0; i < ArduinoX360Class::BUTTON_COUNT; ++i) {
        TELE.print("BTN:");
        TELE.println(buttonNames[i]);
    }

    TELE.println("READY");
}

static void testButtonLatency() {
    for (uint8_t iter = 0; iter < BTN_ITERATIONS; ++iter) {
        for (uint8_t i = 0; i < ArduinoX360Class::BUTTON_COUNT; ++i) {
            auto btn = static_cast<ArduinoX360Class::Button>(i);

            ArduinoX360.press(btn);
            uint64_t t0 = micros();
            ArduinoX360.send();
            emit(t0, TYPE_BTN_PRESS, i);
            delayMicroseconds(10000);

            ArduinoX360.release(btn);
            uint64_t t1 = micros();
            ArduinoX360.send();
            emit(t1, TYPE_BTN_RELEASE, i);
            delayMicroseconds(10000);
        }
    }
}

static void testStickLatency() {
    for (uint8_t iter = 0; iter < ANALOG_ITERATIONS; ++iter) {
        uint64_t t;

        ArduinoX360.setStickLeft(32767, 0);
        t = micros();
        ArduinoX360.send();
        emit(t, TYPE_LEFT_STICK, 0);
        delayMicroseconds(200);

        ArduinoX360.setStickLeft(0, 0);
        t = micros();
        ArduinoX360.send();
        emit(t, TYPE_LEFT_STICK, 1);
        delayMicroseconds(200);

        ArduinoX360.setStickLeft(0, 32767);
        t = micros();
        ArduinoX360.send();
        emit(t, TYPE_LEFT_STICK, 2);
        delayMicroseconds(200);

        ArduinoX360.setStickLeft(0, 0);
        t = micros();
        ArduinoX360.send();
        emit(t, TYPE_LEFT_STICK, 3);
        delayMicroseconds(200);

        ArduinoX360.setStickRight(32767, 0);
        t = micros();
        ArduinoX360.send();
        emit(t, TYPE_RIGHT_STICK, 0);
        delayMicroseconds(200);

        ArduinoX360.setStickRight(0, 0);
        t = micros();
        ArduinoX360.send();
        emit(t, TYPE_RIGHT_STICK, 1);
        delayMicroseconds(200);
    }
}

static void testTriggerLatency() {
    for (uint8_t iter = 0; iter < ANALOG_ITERATIONS; ++iter) {
        uint64_t t;

        ArduinoX360.setLeftTrigger(32768U);
        t = micros();
        ArduinoX360.send();
        emit(t, TYPE_L_TRIGGER, 0);
        delayMicroseconds(200);

        ArduinoX360.setLeftTrigger(0);
        t = micros();
        ArduinoX360.send();
        emit(t, TYPE_L_TRIGGER, 1);
        delayMicroseconds(200);

        ArduinoX360.setRightTrigger(32768U);
        t = micros();
        ArduinoX360.send();
        emit(t, TYPE_R_TRIGGER, 0);
        delayMicroseconds(200);

        ArduinoX360.setRightTrigger(0);
        t = micros();
        ArduinoX360.send();
        emit(t, TYPE_R_TRIGGER, 1);
        delayMicroseconds(200);
    }
}

static void testDpadLatency() {
    for (uint8_t iter = 0; iter < BTN_ITERATIONS; ++iter) {
        uint64_t t;
        for (uint8_t h = 0; h <= 7; ++h) {
            ArduinoX360.setHat(h);
            t = micros();
            ArduinoX360.send();
            emit(t, TYPE_DPAD, h);
            delayMicroseconds(200);
        }

        ArduinoX360.setHat(8);
        t = micros();
        ArduinoX360.send();
        emit(t, TYPE_DPAD, 8);
        delayMicroseconds(200);
    }
}

static char serialBuf[64];
static size_t serialPos = 0;

void loop() {
    if (!g_running) {
        while (TELE.available()) {
            char c = TELE.read();
            if (c == '\n' || c == '\r') {
                if (serialPos > 0) {
                    serialBuf[serialPos] = '\0';

                    const char* p = serialBuf;
                    while (*p && (*p == ' ' || *p == '\t')) p++;
                    size_t len = strlen(p);
                    if (len > 0) {
                        while (len > 0 && (p[len-1] == ' ' || p[len-1] == '\t' || p[len-1] == '\r' || p[len-1] == '\n')) len--;

                        if (strncmp(p, "START", len) == 0 && strlen("START") <= len) {
                            g_seq = 0;
                            g_running = true;
                            emit(micros(), TYPE_MARKER, 0);
                        } else if (strncmp(p, "SYNC", len) == 0 && strlen("SYNC") <= len) {
                            TELE.print("SYNC:");
                            TELE.println(micros());
                        }
                    }
                }
                serialPos = 0;
            } else if (serialPos < sizeof(serialBuf) - 1) {
                serialBuf[serialPos++] = c;
            }
        }
        return;
    }

    testButtonLatency();
    testStickLatency();
    testTriggerLatency();
    testDpadLatency();

    emit(micros(), TYPE_MARKER, 1);
    g_running = false;
}
