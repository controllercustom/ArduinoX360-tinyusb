// SPDX-License-Identifier: MIT
// ArduinoX360-tinyusb — shared XInput descriptor definitions
#pragma once
#include "tusb.h"

#define XINPUT_IFACE_DESC_LEN  40U
#define XINPUT_CONFIG_DESC_LEN 49U

static const uint8_t g_xinputIfaceDesc[] = {
    9, TUSB_DESC_INTERFACE, 0, 0, 2, 0xFF, 0x5D, 0x01, 0,
    0x11, 0x21, 0x00, 0x01, 0x01, 0x25,
    0x81, 0x14, 0x00, 0x00, 0x00, 0x00, 0x13,
    0x01, 0x08, 0x00, 0x00,
    7, TUSB_DESC_ENDPOINT, 0x81, TUSB_XFER_INTERRUPT, 32 & 0xFF, (32 >> 8) & 0xFF, 4,
    7, TUSB_DESC_ENDPOINT, 0x01, TUSB_XFER_INTERRUPT, 32 & 0xFF, (32 >> 8) & 0xFF, 8
};
