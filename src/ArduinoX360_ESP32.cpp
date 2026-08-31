// SPDX-License-Identifier: MIT
// ArduinoX360-tinyusb — ESP32-S2/S3 backend (arduino-esp32 + esp32-hal-tinyusb)

#if defined(ARDUINO_ARCH_ESP32)

#include "ArduinoX360.h"
#include "xinput_descriptor.h"
#include <Arduino.h>
#include "tusb.h"
#include "esp_timer.h"
#include "esp32-hal-tinyusb.h"

static uint8_t g_xinputReportBuffer[XINPUT_REPORT_SIZE] __attribute__((aligned(4)));
static uint8_t g_xinputOutBuffer[64];

static uint16_t g_xinputVid = XINPUT_VID_DEFAULT;
static uint16_t g_xinputPid = XINPUT_PID_DEFAULT;

static const uint8_t g_xinputDescDevice[] = {
    0x12, TUSB_DESC_DEVICE, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 64,
    uint8_t(XINPUT_VID_DEFAULT & 0xFF), uint8_t(XINPUT_VID_DEFAULT >> 8),
    uint8_t(XINPUT_PID_DEFAULT & 0xFF), uint8_t(XINPUT_PID_DEFAULT >> 8),
    0x14, 0x01, 1, 2, 3, 1
};
static const uint8_t g_xinputDescConfigHeader[] = { 0x09, TUSB_DESC_CONFIGURATION, XINPUT_CONFIG_DESC_LEN & 0xFF, (XINPUT_CONFIG_DESC_LEN >> 8) & 0xFF, 1, 1, 0, 0xA0, 250/2U };
uint16_t tusb_xinput_load_descriptor(uint8_t *dst, uint8_t *itf);

void buildDescriptors(uint16_t vid, uint16_t pid) {
    g_xinputVid = vid; g_xinputPid = pid;
    const_cast<uint8_t*>(g_xinputDescDevice)[8] = uint8_t(vid & 0xFF);
    const_cast<uint8_t*>(g_xinputDescDevice)[9] = uint8_t((vid >> 8) & 0xFF);
    const_cast<uint8_t*>(g_xinputDescDevice)[10] = uint8_t(pid & 0xFF);
    const_cast<uint8_t*>(g_xinputDescDevice)[11] = uint8_t((pid >> 8) & 0xFF);
    tinyusb_enable_interface(USB_INTERFACE_CUSTOM, 40, tusb_xinput_load_descriptor);
}

uint16_t tusb_xinput_load_descriptor(uint8_t *dst, uint8_t *itf) {
    uint8_t iface = *itf;
    uint8_t str_index = tinyusb_add_string_descriptor("XInput Controller");
    uint8_t desc[] = {
        9, TUSB_DESC_INTERFACE, iface, 0, 2, 0xFF, 0x5D, 0x01, str_index,
        0x11, 0x21, 0x00, 0x01, 0x01, 0x25, 0x81, 0x14, 0x00, 0x00, 0x00, 0x00, 0x13, 0x01, 0x08, 0x00, 0x00,
        7, TUSB_DESC_ENDPOINT, 0x81, TUSB_XFER_INTERRUPT, 32 & 0xFF, (32 >> 8) & 0xFF, 4,
        7, TUSB_DESC_ENDPOINT, 0x01, TUSB_XFER_INTERRUPT, 32 & 0xFF, (32 >> 8) & 0xFF, 8
    };
    *itf += 1; memcpy(dst, desc, sizeof(desc)); return sizeof(desc);
}

extern "C" bool tinyusb_vendor_control_request_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);
extern "C" bool tinyusb_vendor_control_request_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (request->bmRequestType_bit.recipient == TUSB_REQ_RCPT_INTERFACE &&
        request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
        request->bmRequestType_bit.direction == TUSB_DIR_IN &&
        request->bRequest == 0x01) {
        if (request->wValue == 0x0100) { static uint8_t buf[XINPUT_REPORT_SIZE]; memcpy(buf, &ArduinoX360.getReport(), sizeof(buf)); return tud_control_xfer(rhport, request, buf, sizeof(buf)); }
        if (request->wValue == 0x0000) { static uint8_t caps[8] = {0x00,0x08,0x00,0xFF,0xFF,0x00,0x00,0x00}; return tud_control_xfer(rhport, request, caps, sizeof(caps)); }
    }
    return false;
}

void ArduinoX360Class::begin(uint16_t vid, uint16_t pid) {
    buildDescriptors(vid, pid);
    tinyusb_device_config_t cfg{}; cfg.vid=vid; cfg.pid=pid;
    cfg.product_name="XInput Controller"; cfg.manufacturer_name="Microsoft";
    cfg.serial_number=NULL; cfg.fw_version=0x0100; cfg.usb_version=0x0200;
    cfg.usb_class=0xFF; cfg.usb_subclass=0xFF; cfg.usb_protocol=0xFF;
    cfg.usb_attributes=TUSB_DESC_CONFIG_ATT_SELF_POWERED; cfg.usb_power_ma=500;
    tinyusb_init(&cfg);
    _connected=true; _report.bMessageType=0x00; _report.bMessageSize=0x14;
}

bool ArduinoX360Class::_canSend() const {
    if (!tud_mounted()) return false;
    if (!_usbReady) { _mountedAt=millis(); _usbReady=true; }
    return ((unsigned long)(millis()-_mountedAt))>=100UL;
}
bool ArduinoX360Class::isConnected() const {
    if (!_connected) return false;
    bool m=tud_mounted();
    if (!m && _usbReady) _usbReady=false;
    if (m && !_usbReady) { _mountedAt=millis(); _usbReady=true; }
    if (_usbReady && ((unsigned long)(millis()-_mountedAt))<100UL) return false;
    return m;
}
uint32_t ArduinoX360Class::setPollInterval(uint32_t ms) {
    uint32_t old=_pollIntervalMs;
    if (_timerHandle) { esp_timer_stop(_timerHandle); esp_timer_delete(_timerHandle); _timerHandle=nullptr; }
    if (ms>0 && ms<4) _pollIntervalMs=4; else _pollIntervalMs=ms;
    esp_timer_create_args_t args{}; args.callback=&ArduinoX360Class::timerCallback; args.arg=this; args.dispatch_method=ESP_TIMER_TASK; args.name="x360_poll";
    if (esp_timer_create(&args,&_timerHandle)==ESP_OK && _pollIntervalMs>0) esp_timer_start_periodic(_timerHandle, (int64_t)_pollIntervalMs*1000LL);
    return old;
}
void IRAM_ATTR ArduinoX360Class::timerCallback(void* arg) {
    if (!arg) return; auto self=reinterpret_cast<ArduinoX360Class*>(arg);
    if (!self->_dirtyFlag.load()) return;
    if (!self->_canSend()) return;
    memcpy(g_xinputReportBuffer, &self->_report, sizeof(self->_report));
    tud_vendor_n_write(0,g_xinputReportBuffer,XINPUT_REPORT_SIZE); tud_vendor_n_write_flush(0);
    self->_dirtyFlag.store(false);
}
void ArduinoX360Class::_sendReport() {
    if (!_dirtyFlag.load()) return;
    if (!_canSend()) return;
    memcpy(g_xinputReportBuffer, &_report, sizeof(_report));
    tud_vendor_n_write(0,g_xinputReportBuffer,XINPUT_REPORT_SIZE); tud_vendor_n_write_flush(0);
    _dirtyFlag.store(false);
}
void ArduinoX360Class::update(){_sendReport();}
void ArduinoX360Class::send(){_sendReport();}
bool ArduinoX360Class::ready(){return isConnected();}
void ArduinoX360Class::releaseAll(){ memset(&_report,0,sizeof(_report)); _report.bMessageType=0x00; _report.bMessageSize=0x14; _markDirty(); _sendReport();}
void ArduinoX360Class::pollRumble(){
    if (!tud_mounted()) return;
    while(tud_vendor_n_available(0)){ uint32_t len=tud_vendor_n_read(0,g_xinputOutBuffer,sizeof(g_xinputOutBuffer)); if(len<1) continue; _receivedAnyOutput=true;
        if(g_xinputOutBuffer[0]==0x00 && len>=5 && _onRumbleCb){ _lastRumbleL=g_xinputOutBuffer[3]; _lastRumbleR=g_xinputOutBuffer[4]; _onRumbleCb(_lastRumbleL,_lastRumbleR);}
        else if(g_xinputOutBuffer[0]==0x01 && len>=3 && _onLedCb){ _lastLedIndex=g_xinputOutBuffer[2]; _onLedCb(_lastLedIndex);}
    }
}
#endif
