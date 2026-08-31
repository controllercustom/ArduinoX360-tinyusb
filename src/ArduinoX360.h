// SPDX-License-Identifier: MIT
// ArduinoX360-tinyusb — unified TinyUSB XInput library
// Public API matches ESP32XInput / PIPICXInput (Button enum, triggers, sticks, hat, rumble/LED callbacks)

#pragma once

#include <Arduino.h>
#include <atomic>
#include <functional>

#define XINPUT_REPORT_SIZE 20U
#define XINPUT_VID_DEFAULT 0x045EU
#define XINPUT_PID_DEFAULT 0x028EU

#if defined(ARDUINO_ARCH_ESP32)
#include "esp_timer.h"
#elif defined(ARDUINO_ARCH_RP2040)
#include <pico/time.h>
#endif

class ArduinoX360Class {
public:
    struct __attribute__((packed)) XInputReport {
        uint8_t  bMessageType;      // 0x00
        uint8_t  bMessageSize;      // 0x14
        uint16_t wButtons;
        uint8_t  bLeftTrigger;
        uint8_t  bRightTrigger;
        int16_t  sThumbLX;
        int16_t  sThumbLY;
        int16_t  sThumbRX;
        int16_t  sThumbRY;
        uint32_t dwReserved0;
        uint16_t wReserved1;
    };

    enum Button : uint8_t {
        DPAD_UP = 0,
        DPAD_DOWN,
        DPAD_LEFT,
        DPAD_RIGHT,
        START,
        BACK,
        LEFT_THUMB,
        RIGHT_THUMB,
        LEFT_SHOULDER,
        RIGHT_SHOULDER,
        XBOX,
        A = 12,
        B,
        X,
        Y,
        BUTTON_COUNT = 16U
    };

    void begin(uint16_t vid = XINPUT_VID_DEFAULT, uint16_t pid = XINPUT_PID_DEFAULT);
    bool isConnected() const;

    void press(Button btn);
    void release(Button btn);
    void setButton(Button btn, bool pressed);
    bool getButton(Button btn) const;

    void setLeftTrigger(uint16_t value);
    void setRightTrigger(uint16_t value);
    void setStickLeft(int16_t x, int16_t y);
    void setStickRight(int16_t x, int16_t y);

    void setHat(uint8_t hat);
    void setDpad(uint8_t dir);
    uint8_t getHat() const;

    using RumbleCallback = std::function<void(uint8_t leftMotor, uint8_t rightMotor)>;
    using LedCallback    = std::function<void(uint8_t ledIndex)>;
    void onRumble(RumbleCallback cb);
    void onLed(LedCallback cb);

    uint32_t setPollInterval(uint32_t ms);
    void update();
    void send();
    bool ready();
    void releaseAll();
    void pollRumble();
    uint8_t getLastRumbleLeft() const;
    uint8_t getLastRumbleRight() const;
    uint8_t getLastLedIndex() const;
    bool hasReceivedOutput() const;
    const XInputReport& getReport() const;

private:
    void _sendReport();
    bool _isDirty() const { return _dirtyFlag.load(); }
    void _markDirty() { _dirtyFlag.store(true); }
    bool _canSend() const;

    XInputReport _report{0x00, 0x14};
    volatile std::atomic<bool> _dirtyFlag{false};

    uint32_t _pollIntervalMs = 8U;
#if defined(ARDUINO_ARCH_ESP32)
    esp_timer_handle_t _timerHandle = nullptr;
#elif defined(ARDUINO_ARCH_RP2040)
    repeating_timer_t _timer{};
    bool _timerStarted = false;
#endif
    bool _connected = false;
    mutable volatile bool _usbReady = false;
    mutable unsigned long _mountedAt = 0;

    RumbleCallback _onRumbleCb = nullptr;
    LedCallback    _onLedCb   = nullptr;

    uint8_t _lastRumbleL  = 0;
    uint8_t _lastRumbleR  = 0;
    uint8_t _lastLedIndex = 0xFF;

    bool _receivedAnyOutput = false;

#if defined(ARDUINO_ARCH_ESP32)
    static void IRAM_ATTR timerCallback(void* arg);
#elif defined(ARDUINO_ARCH_RP2040)
    static bool timerCallback(struct repeating_timer *t);
#endif
};

static_assert(sizeof(ArduinoX360Class::XInputReport) == XINPUT_REPORT_SIZE,
    "XInputReport must be exactly 20 bytes");

extern ArduinoX360Class ArduinoX360;
