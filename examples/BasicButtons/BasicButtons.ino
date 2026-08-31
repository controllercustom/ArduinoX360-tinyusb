// SPDX-License-Identifier: MIT

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


void setup() {
    TELE.begin(115200);
    
    ArduinoX360.begin();
    
    while (!TELE) delay(10);
}

void loop() {
    static bool pressed = false;
    pressed = !pressed;
    
    if (pressed) {
        ArduinoX360.press(ArduinoX360.Button::A);
        TELE.println("Pressing A");
    } else {
        ArduinoX360.release(ArduinoX360.Button::A);
        TELE.println("Releasing A");
    }
    ArduinoX360.send();
    
    delay(500);
}
