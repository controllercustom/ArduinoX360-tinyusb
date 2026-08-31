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


void onRumble(uint8_t leftMotor, uint8_t rightMotor) {
    TELE.print("Rumble received - Left motor: ");
    TELE.print(leftMotor);
    TELE.print(", Right motor: ");
    TELE.println(rightMotor);
}

void onLedChange(uint8_t ledIndex) {
    TELE.print("LED change requested, index: ");
    TELE.println(ledIndex);
}

void setup() {
    TELE.begin(115200);
    
    ArduinoX360.onRumble(onRumble);
    ArduinoX360.onLed(onLedChange);
    
    ArduinoX360.begin();
}

void loop() {
    ArduinoX360.pollRumble();

    static uint8_t buttonIdx = 0;
    const char* names[] = {"A", "B", "X", "Y"};
    
    TELE.print("Pressing ");
    TELE.println(names[buttonIdx]);
    
    switch (buttonIdx) {
        case 0: ArduinoX360.press(ArduinoX360.Button::A); break;
        case 1: ArduinoX360.press(ArduinoX360.Button::B); break;
        case 2: ArduinoX360.press(ArduinoX360.Button::X); break;
        case 3: ArduinoX360.press(ArduinoX360.Button::Y); break;
    }
    
    delay(1000);
    
    switch (buttonIdx) {
        case 0: ArduinoX360.release(ArduinoX360.Button::A); break;
        case 1: ArduinoX360.release(ArduinoX360.Button::B); break;
        case 2: ArduinoX360.release(ArduinoX360.Button::X); break;
        case 3: ArduinoX360.release(ArduinoX360.Button::Y); break;
    }
    
    buttonIdx = (buttonIdx + 1) % 4;
}
