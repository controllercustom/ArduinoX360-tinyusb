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
}

void loop() {
    static uint8_t phase = 0;
    
    switch (phase) {
        case 0: // Left stick sweep right
            for (int x = -32767; x <= 32767; x += 1000) {
                ArduinoX360.setStickLeft(x, 0);
                ArduinoX360.send();
                delay(5);
            }
            phase++;
            break;
            
        case 1: // Left stick sweep up
            for (int y = -32767; y <= 32767; y += 1000) {
                ArduinoX360.setStickLeft(0, y);
                ArduinoX360.send();
                delay(5);
            }
            phase++;
            break;
            
        case 2: // Right stick sweep right
            for (int x = -32767; x <= 32767; x += 1000) {
                ArduinoX360.setStickRight(x, 0);
                ArduinoX360.send();
                delay(5);
            }
            phase++;
            break;
            
        case 3: // Right stick sweep up
            for (int y = -32767; y <= 32767; y += 1000) {
                ArduinoX360.setStickRight(0, y);
                ArduinoX360.send();
                delay(5);
            }
            phase++;
            break;
            
        case 4: // Left trigger sweep
            for (uint16_t t = 0; t <= 32768; t += 1000) {
                ArduinoX360.setLeftTrigger(t);
                ArduinoX360.send();
                delay(5);
            }
            phase++;
            break;
            
        case 5: // Right trigger sweep
            for (uint16_t t = 0; t <= 32768; t += 1000) {
                ArduinoX360.setRightTrigger(t);
                ArduinoX360.send();
                delay(5);
            }
            phase++;
            break;
            
        case 6: // Reset to center/zero
            ArduinoX360.setStickLeft(0, 0);
            ArduinoX360.setStickRight(0, 0);
            ArduinoX360.setLeftTrigger(0);
            ArduinoX360.setRightTrigger(0);
            ArduinoX360.send();
            phase = 0;
            delay(500);
            break;
    }
}
