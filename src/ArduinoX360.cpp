// SPDX-License-Identifier: MIT
// ArduinoX360-tinyusb — board-agnostic state machine

#include "ArduinoX360.h"

ArduinoX360Class ArduinoX360;

void ArduinoX360Class::press(Button btn) {
    if (btn >= BUTTON_COUNT) return;
    _report.wButtons |= (1U << static_cast<uint8_t>(btn));
    _markDirty();
}

void ArduinoX360Class::release(Button btn) {
    if (btn >= BUTTON_COUNT) return;
    _report.wButtons &= ~(1U << static_cast<uint8_t>(btn));
    _markDirty();
}

void ArduinoX360Class::setButton(Button btn, bool pressed) {
    if (pressed) press(btn); else release(btn);
}

bool ArduinoX360Class::getButton(Button btn) const {
    if (btn >= BUTTON_COUNT) return false;
    return (_report.wButtons & (1U << static_cast<uint8_t>(btn))) != 0;
}

void ArduinoX360Class::setLeftTrigger(uint16_t value) {
    _report.bLeftTrigger = static_cast<uint8_t>((value > 32768U) ? 255U : (uint8_t)(value * 255U / 32768U));
    _markDirty();
}

void ArduinoX360Class::setRightTrigger(uint16_t value) {
    _report.bRightTrigger = static_cast<uint8_t>((value > 32768U) ? 255U : (uint8_t)(value * 255U / 32768U));
    _markDirty();
}

void ArduinoX360Class::setStickLeft(int16_t x, int16_t y) {
    _report.sThumbLX = x; _report.sThumbLY = y; _markDirty();
}

void ArduinoX360Class::setStickRight(int16_t x, int16_t y) {
    _report.sThumbRX = x; _report.sThumbRY = y; _markDirty();
}

void ArduinoX360Class::setHat(uint8_t hat) {
    _report.wButtons &= ~0x000FU;
    if (hat < 8) {
        static const uint8_t hatToButtons[8] = {0x01,0x09,0x08,0x0A,0x02,0x06,0x04,0x05};
        _report.wButtons |= hatToButtons[hat];
    }
    _markDirty();
}

void ArduinoX360Class::setDpad(uint8_t dir) { setHat(dir); }

uint8_t ArduinoX360Class::getHat() const {
    uint8_t btns = _report.wButtons & 0x000FU;
    bool up=btns&0x01, down=btns&0x02, left=btns&0x04, right=btns&0x08;
    if (!up && !down && !left && !right) return 8;
    if ( up && !down && !left && !right) return 0;
    if ( up && !down &&  right && !left) return 1;
    if (!up && !down &&  right && !left) return 2;
    if (!up &&  down &&  right && !left) return 3;
    if (!up &&  down && !left && !right) return 4;
    if (!up &&  down &&  left && !right) return 5;
    if (!up && !down &&  left && !right) return 6;
    if ( up && !down &&  left && !right) return 7;
    return 8;
}

void ArduinoX360Class::onRumble(RumbleCallback cb) { _onRumbleCb = std::move(cb); }
void ArduinoX360Class::onLed(LedCallback cb) { _onLedCb = std::move(cb); }

uint8_t ArduinoX360Class::getLastRumbleLeft() const { return _lastRumbleL; }
uint8_t ArduinoX360Class::getLastRumbleRight() const { return _lastRumbleR; }
uint8_t ArduinoX360Class::getLastLedIndex() const { return _lastLedIndex; }
bool ArduinoX360Class::hasReceivedOutput() const { return _receivedAnyOutput; }
const ArduinoX360Class::XInputReport& ArduinoX360Class::getReport() const { return _report; }

#if !defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_ESP32) && !defined(ARDUINO_ARCH_SAMD) && !defined(ARDUINO_ARCH_NRF52) && !defined(ARDUINO_ARCH_RENESAS)
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
void ArduinoX360Class::update() { _sendReport(); }
void ArduinoX360Class::send() { _sendReport(); }
bool ArduinoX360Class::ready() { return isConnected(); }
void ArduinoX360Class::releaseAll() {
    memset(&_report, 0, sizeof(_report));
    _report.bMessageType = 0x00; _report.bMessageSize = 0x14;
    _markDirty(); _sendReport();
}

void ArduinoX360Class::_sendReport() {
    if (!_dirtyFlag.load()) return;
    if (!_canSend()) return;
    // Backends that use direct endpoint arming (SAMD/NRF52/Renesas) override via their own _sendReport in backend file.
    // This generic path uses TinyUSB vendor write if available.
    extern bool _x360_generic_send(const uint8_t*, uint8_t);
    if (_x360_generic_send((const uint8_t*)&_report, XINPUT_REPORT_SIZE)) _dirtyFlag.store(false);
}
void ArduinoX360Class::pollRumble() {
    if (!tud_mounted()) return;
    extern int _x360_generic_available();
    extern int _x360_generic_recv(uint8_t*, uint8_t);
    while (_x360_generic_available() > 0) {
        uint8_t buf[64]; uint32_t len = _x360_generic_recv(buf, sizeof(buf));
        if (len < 1) continue;
        _receivedAnyOutput = true;
        if (buf[0]==0x00 && len>=5 && _onRumbleCb) { _lastRumbleL=buf[3]; _lastRumbleR=buf[4]; _onRumbleCb(_lastRumbleL,_lastRumbleR); }
        else if (buf[0]==0x01 && len>=3 && _onLedCb) { _lastLedIndex=buf[2]; _onLedCb(_lastLedIndex); }
    }
}
uint32_t ArduinoX360Class::setPollInterval(uint32_t ms) {
    uint32_t old=_pollIntervalMs;
    if (ms>0 && ms<4) ms=4;
    _pollIntervalMs=ms;
    return old;
}
#endif
