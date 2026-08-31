// SPDX-License-Identifier: MIT
// ArduinoX360-tinyusb — compatibility shim: ESP32XInput.h -> ArduinoX360.h
#pragma once
#include "ArduinoX360.h"
using ESP32XInputClass = ArduinoX360Class;
inline ArduinoX360Class &ESP32XInput = ArduinoX360;
