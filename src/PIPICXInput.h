// SPDX-License-Identifier: MIT
// ArduinoX360-tinyusb — compatibility shim: PIPICXInput.h -> ArduinoX360.h
#pragma once
#include "ArduinoX360.h"
using PIPICXInputClass = ArduinoX360Class;
inline ArduinoX360Class &PIPICXInput = ArduinoX360;
