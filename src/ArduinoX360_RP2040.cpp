// SPDX-License-Identifier: MIT
// ArduinoX360-tinyusb — RP2040/RP2350 backend

#if defined(ARDUINO_ARCH_RP2040)

#include "ArduinoX360.h"
#include "xinput_descriptor.h"
#include <Arduino.h>
#include "Adafruit_TinyUSB.h"
#include "tusb.h"
#include <pico/time.h>

static uint8_t g_xinputReportBuffer[XINPUT_REPORT_SIZE] __attribute__((aligned(4)));
static uint8_t g_xinputOutBuffer[64];

class XInputInterface : public Adafruit_USBD_Interface {
public:
    bool begin(void) { return TinyUSBDevice.addInterface(*this); }
    uint16_t getInterfaceDescriptor(uint8_t itfnum_deprecated, uint8_t *buf, uint16_t bufsize) override {
        (void)itfnum_deprecated;
        uint8_t itfnum = 0; uint8_t ep_in = 0; uint8_t ep_out = 0;
        if (buf) { itfnum = TinyUSBDevice.allocInterface(1); ep_in = TinyUSBDevice.allocEndpoint(TUSB_DIR_IN); ep_out = TinyUSBDevice.allocEndpoint(TUSB_DIR_OUT); }
        uint8_t desc[XINPUT_IFACE_DESC_LEN];
        memcpy(desc, g_xinputIfaceDesc, sizeof(desc));
        desc[2] = itfnum; desc[8] = _strid; desc[28] = ep_in; desc[35] = ep_out;
        uint16_t const len = sizeof(desc);
        if (buf) { if (bufsize < len) return 0; memcpy(buf, desc, len); }
        return len;
    }
};

static XInputInterface _xinputInterface;

extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);
extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (request->bmRequestType_bit.recipient == TUSB_REQ_RCPT_INTERFACE &&
        request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
        request->bmRequestType_bit.direction == TUSB_DIR_IN &&
        request->bRequest == 0x01) {
        if (request->wValue == 0x0100) {
            static uint8_t buf[XINPUT_REPORT_SIZE];
            memcpy(buf, &ArduinoX360.getReport(), sizeof(buf));
            return tud_control_xfer(rhport, request, buf, sizeof(buf));
        }
        if (request->wValue == 0x0000) {
            static uint8_t caps[8] = {0x00, 0x08, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00};
            return tud_control_xfer(rhport, request, caps, sizeof(caps));
        }
    }
    return false;
}

void ArduinoX360Class::begin(uint16_t vid, uint16_t pid) {
    Serial.end();
    TinyUSBDevice.setID(vid, pid);
    TinyUSBDevice.setManufacturerDescriptor("Microsoft");
    TinyUSBDevice.setProductDescriptor("XInput Controller");
    TinyUSBDevice.setDeviceVersion(0x0114);
    TinyUSBDevice.setConfigurationAttribute(0xA0);
    TinyUSBDevice.setConfigurationMaxPower(250);
    _xinputInterface.begin();
    _connected = true;
    _report.bMessageType = 0x00; _report.bMessageSize = 0x14;
}

bool ArduinoX360Class::_canSend() const {
    if (!tud_mounted()) return false;
    if (!_usbReady) { _mountedAt = millis(); _usbReady = true; }
    return ((unsigned long)(millis() - _mountedAt)) >= 100UL;
}

bool ArduinoX360Class::isConnected() const {
    if (!_connected) return false;
    bool mounted = tud_mounted();
    if (!mounted && _usbReady) _usbReady = false;
    if (mounted && !_usbReady) { _mountedAt = millis(); _usbReady = true; }
    if (_usbReady && ((unsigned long)(millis() - _mountedAt)) < 100UL) return false;
    return mounted;
}

uint32_t ArduinoX360Class::setPollInterval(uint32_t ms) {
    uint32_t oldMs = _pollIntervalMs;
    if (_timerStarted) { cancel_repeating_timer(&_timer); _timerStarted = false; }
    if (ms > 0U && ms < 4U) _pollIntervalMs = 4U; else _pollIntervalMs = ms;
    if (_pollIntervalMs > 0U) {
        _timerStarted = add_repeating_timer_ms(static_cast<int32_t>(_pollIntervalMs), &ArduinoX360Class::timerCallback, this, &_timer);
    }
    return oldMs;
}

bool ArduinoX360Class::timerCallback(struct repeating_timer *t) {
    if (t == nullptr || t->user_data == nullptr) return true;
    auto self = reinterpret_cast<ArduinoX360Class*>(t->user_data);
    if (!self->_dirtyFlag.load()) return true;
    if (!self->_canSend()) return true;
    memcpy(g_xinputReportBuffer, &self->_report, sizeof(self->_report));
    tud_vendor_n_write(0, g_xinputReportBuffer, XINPUT_REPORT_SIZE);
    tud_vendor_n_write_flush(0);
    self->_dirtyFlag.store(false);
    return true;
}

void ArduinoX360Class::_sendReport() {
    if (!_dirtyFlag.load()) return;
    if (!_canSend()) return;
    memcpy(g_xinputReportBuffer, &_report, sizeof(_report));
    tud_vendor_n_write(0, g_xinputReportBuffer, XINPUT_REPORT_SIZE);
    tud_vendor_n_write_flush(0);
    _dirtyFlag.store(false);
}

void ArduinoX360Class::update() { _sendReport(); }
void ArduinoX360Class::send() { _sendReport(); }
bool ArduinoX360Class::ready() { return isConnected(); }

void ArduinoX360Class::releaseAll() {
    memset(&_report, 0, sizeof(_report));
    _report.bMessageType = 0x00; _report.bMessageSize = 0x14;
    _markDirty(); _sendReport();
}

void ArduinoX360Class::pollRumble() {
    if (!tud_mounted()) return;
    while (tud_vendor_n_available(0)) {
        uint32_t len = tud_vendor_n_read(0, g_xinputOutBuffer, sizeof(g_xinputOutBuffer));
        if (len < 1) continue;
        _receivedAnyOutput = true;
        if (len >= 5 && g_xinputOutBuffer[0]==0x00 && _onRumbleCb) { _lastRumbleL=g_xinputOutBuffer[3]; _lastRumbleR=g_xinputOutBuffer[4]; _onRumbleCb(_lastRumbleL,_lastRumbleR); }
        else if (len >= 3 && g_xinputOutBuffer[0]==0x01 && _onLedCb) { _lastLedIndex=g_xinputOutBuffer[2]; _onLedCb(_lastLedIndex); }
    }
}

#endif // ARDUINO_ARCH_RP2040
