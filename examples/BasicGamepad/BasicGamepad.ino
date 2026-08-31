// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com

// BasicGamepad — UART command parser for ArduinoX360.
// Accepts KEY=VALUE\n commands over Serial and sends XInput reports.
//
// Commands:
//   BTN_A=1, BTN_B=0, BTN_X=1, BTN_Y=0, etc.  (0 or 1)
//   BTN_START=1, BTN_BACK=1, BTN_LB=1, BTN_RB=1
//   BTN_LTHUMB=1, BTN_RTHUMB=1, BTN_GUIDE=1
//   DPAD_UP=1, DPAD_DOWN=0, DPAD_LEFT=0, DPAD_RIGHT=1
//   LX=0, LY=0, RX=0, RY=0                       (signed -32767..32767)
//   TRIG_L=200, TRIG_R=200                         (unsigned 0..32768)
//   RELEASE                                         (release all + send)

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


static int16_t lx = 0, ly = 0, rx = 0, ry = 0;

static void exec(const char *key, const char *val) {
  bool pressed = atoi(val) != 0;
  static bool dpadUp = false, dpadDown = false, dpadLeft = false, dpadRight = false;

  if (strcmp(key, "BTN_A") == 0)         { ArduinoX360.setButton(ArduinoX360Class::A, pressed); }
  else if (strcmp(key, "BTN_B") == 0)         { ArduinoX360.setButton(ArduinoX360Class::B, pressed); }
  else if (strcmp(key, "BTN_X") == 0)         { ArduinoX360.setButton(ArduinoX360Class::X, pressed); }
  else if (strcmp(key, "BTN_Y") == 0)         { ArduinoX360.setButton(ArduinoX360Class::Y, pressed); }
  else if (strcmp(key, "BTN_START") == 0)     { ArduinoX360.setButton(ArduinoX360Class::START, pressed); }
  else if (strcmp(key, "BTN_BACK") == 0)      { ArduinoX360.setButton(ArduinoX360Class::BACK, pressed); }
  else if (strcmp(key, "BTN_LB") == 0)        { ArduinoX360.setButton(ArduinoX360Class::LEFT_SHOULDER, pressed); }
  else if (strcmp(key, "BTN_RB") == 0)        { ArduinoX360.setButton(ArduinoX360Class::RIGHT_SHOULDER, pressed); }
  else if (strcmp(key, "BTN_LTHUMB") == 0)    { ArduinoX360.setButton(ArduinoX360Class::LEFT_THUMB, pressed); }
  else if (strcmp(key, "BTN_RTHUMB") == 0)    { ArduinoX360.setButton(ArduinoX360Class::RIGHT_THUMB, pressed); }
  else if (strcmp(key, "BTN_GUIDE") == 0)     { ArduinoX360.setButton(ArduinoX360Class::XBOX, pressed); }
  else if (strcmp(key, "DPAD_UP") == 0)       { /* handled below */ }
  else if (strcmp(key, "DPAD_DOWN") == 0)     { /* handled below */ }
  else if (strcmp(key, "DPAD_LEFT") == 0)     { /* handled below */ }
  else if (strcmp(key, "DPAD_RIGHT") == 0)    { /* handled below */ }
  else if (strcmp(key, "LX") == 0)  { lx = atoi(val); ArduinoX360.setStickLeft(lx, ly); }
  else if (strcmp(key, "LY") == 0)  { ly = atoi(val); ArduinoX360.setStickLeft(lx, ly); }
  else if (strcmp(key, "RX") == 0)  { rx = atoi(val); ArduinoX360.setStickRight(rx, ry); }
  else if (strcmp(key, "RY") == 0)  { ry = atoi(val); ArduinoX360.setStickRight(rx, ry); }
  else if (strcmp(key, "TRIG_L") == 0)  { ArduinoX360.setLeftTrigger((uint16_t)atoi(val)); }
  else if (strcmp(key, "TRIG_R") == 0)  { ArduinoX360.setRightTrigger((uint16_t)atoi(val)); }
  else if (strcmp(key, "RELEASE") == 0) {
    dpadUp = dpadDown = dpadLeft = dpadRight = false;
    ArduinoX360.releaseAll();
    TELE.println("SENT=1");
    return;
  }
  else {
    TELE.print("UNKNOWN_CMD=");
    TELE.println(key);
    return;
  }

  // Handle d-pad: read current state, update single direction, rebuild hat.
  if (strncmp(key, "DPAD_", 5) == 0) {
    if (strcmp(key, "DPAD_UP") == 0) dpadUp = pressed;
    else if (strcmp(key, "DPAD_DOWN") == 0) dpadDown = pressed;
    else if (strcmp(key, "DPAD_LEFT") == 0) dpadLeft = pressed;
    else if (strcmp(key, "DPAD_RIGHT") == 0) dpadRight = pressed;

    uint8_t hat = 8; // centered
    if (dpadUp && !dpadDown && !dpadLeft && !dpadRight) hat = 0;
    else if (dpadUp && !dpadDown && dpadRight && !dpadLeft) hat = 1;
    else if (!dpadUp && !dpadDown && dpadRight && !dpadLeft) hat = 2;
    else if (!dpadUp && dpadDown && dpadRight && !dpadLeft) hat = 3;
    else if (!dpadUp && dpadDown && !dpadLeft && !dpadRight) hat = 4;
    else if (!dpadUp && dpadDown && dpadLeft && !dpadRight) hat = 5;
    else if (!dpadUp && !dpadDown && dpadLeft && !dpadRight) hat = 6;
    else if (dpadUp && !dpadDown && dpadLeft && !dpadRight) hat = 7;
    ArduinoX360.setHat(hat);
  }

  ArduinoX360.send();
  TELE.println("SENT=1");
}

static char buf[32];
static size_t pos = 0;

void setup() {
  TELE.begin(115200);
  delay(200);
  ArduinoX360.begin();
  TELE.println("READY");
}

void loop() {
  ArduinoX360.pollRumble();
  while (TELE.available() > 0) {
    char c = TELE.read();
    if (c == '\n' || c == '\r') {
      if (pos > 0) {
        buf[pos] = '\0';
        char *eq = (char *)memchr(buf, '=', pos);
        if (eq) {
          *eq = '\0';
          exec(buf, eq + 1);
        } else {
          exec(buf, "0");
        }
        pos = 0;
      }
    } else if (pos < sizeof(buf) - 1) {
      buf[pos++] = c;
    }
  }
}
