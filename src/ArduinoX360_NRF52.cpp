// SPDX-License-Identifier: MIT
// ArduinoX360-tinyusb — nRF52 backend

#if defined(ARDUINO_ARCH_NRF52) && defined(USE_TINYUSB)

#include "ArduinoX360.h"
#include "xinput_descriptor.h"
#include <Arduino.h>
#include "Adafruit_TinyUSB.h"
#include "tusb.h"
#include "device/usbd_pvt.h"

static uint8_t g_xinputReportBuffer[XINPUT_REPORT_SIZE] __attribute__((aligned(4)));
static uint8_t g_xinputOutBuffer[64];
static uint8_t _xinputEpIn = 0;
static uint8_t _xinputEpOut = 0;

class XInputInterface : public Adafruit_USBD_Interface {
public:
    bool begin(void){ return TinyUSBDevice.addInterface(*this); }
    uint16_t getInterfaceDescriptor(uint8_t itfnum_deprecated, uint8_t *buf, uint16_t bufsize) override{
        (void)itfnum_deprecated; uint8_t itfnum=0, ep_in=0, ep_out=0;
        if(buf){ itfnum=TinyUSBDevice.allocInterface(1); ep_in=TinyUSBDevice.allocEndpoint(TUSB_DIR_IN); ep_out=TinyUSBDevice.allocEndpoint(TUSB_DIR_OUT); }
        uint8_t desc[XINPUT_IFACE_DESC_LEN]; memcpy(desc,g_xinputIfaceDesc,sizeof(desc));
        desc[2]=itfnum; desc[8]=_strid; desc[28]=ep_in; desc[35]=ep_out;
        if(buf){ _xinputEpIn=ep_in; _xinputEpOut=ep_out; }
        uint16_t len=sizeof(desc);
        if(buf){ if(bufsize<len) return 0; memcpy(buf,desc,len); }
        return len;
    }
};
static XInputInterface _xinputInterface;
static uint8_t _xinputInBuf[XINPUT_REPORT_SIZE] __attribute__((aligned(4)));
static volatile bool _xinputTxPending=false;
static bool _xinputMounted=false;
static unsigned long _xinputMountedAt=0;
static SoftwareTimer _xinputTimer;
static bool _xinputTimerRunning=false;
static uint32_t _recvIntervalMsCache=8;

static bool _xinputArmIn(const uint8_t *src){
    if(_xinputEpIn==0) return false;
    if(!tud_mounted() || !tud_ready()) return false;
    if(usbd_edpt_busy(0,_xinputEpIn)) return false;
    memcpy(_xinputInBuf, src, XINPUT_REPORT_SIZE);
    return usbd_edpt_xfer(0,_xinputEpIn,_xinputInBuf,XINPUT_REPORT_SIZE,false);
}
static void _xinputPumpCallback(TimerHandle_t th){
    (void)th; ArduinoX360.pollRumble(); ArduinoX360.send();
    if(_xinputTxPending && tud_mounted() && tud_ready()){
        if(_xinputArmIn(g_xinputReportBuffer)) _xinputTxPending=false;
    }
}
static void _xinputTimerStart(uint32_t ms){
    if(_xinputTimerRunning) _xinputTimer.setPeriod(ms);
    else { _xinputTimer.begin(ms,_xinputPumpCallback,NULL,true); _xinputTimer.start(); _xinputTimerRunning=true; }
}
static void _xinputTimerStop(){ if(_xinputTimerRunning){ _xinputTimer.stop(); _xinputTimerRunning=false; } }
static void _xinputRestartTimer(){ _xinputTimerStop(); if(_recvIntervalMsCache>0) _xinputTimerStart(_recvIntervalMsCache); }

extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);
extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request){
    if(stage!=CONTROL_STAGE_SETUP) return true;
    if(request->bmRequestType_bit.recipient==TUSB_REQ_RCPT_INTERFACE && request->bmRequestType_bit.type==TUSB_REQ_TYPE_VENDOR && request->bmRequestType_bit.direction==TUSB_DIR_IN && request->bRequest==0x01){
        if(request->wValue==0x0100){ static uint8_t buf[XINPUT_REPORT_SIZE]; memcpy(buf,g_xinputReportBuffer,sizeof(buf)); return tud_control_xfer(rhport,request,buf,XINPUT_REPORT_SIZE); }
        if(request->wValue==0x0000){ static uint8_t caps[8]={0x00,0x08,0x00,0xFF,0xFF,0x00,0x00,0x00}; return tud_control_xfer(rhport,request,caps,sizeof(caps)); }
    }
    return false;
}

void ArduinoX360Class::begin(uint16_t vid, uint16_t pid){
    Serial.end();
    TinyUSBDevice.setID(vid,pid);
    TinyUSBDevice.setManufacturerDescriptor("Microsoft");
    TinyUSBDevice.setProductDescriptor("XInput Controller");
    TinyUSBDevice.setDeviceVersion(0x0114);
    TinyUSBDevice.setConfigurationAttribute(0xA0);
    TinyUSBDevice.setConfigurationMaxPower(250);
    _xinputInterface.begin();
    memset(g_xinputReportBuffer,0,sizeof(g_xinputReportBuffer));
    g_xinputReportBuffer[0]=0x00; g_xinputReportBuffer[1]=0x14;
    _connected=true; _report.bMessageType=0x00; _report.bMessageSize=0x14;
    _recvIntervalMsCache=_pollIntervalMs;
    if(_recvIntervalMsCache>0) _xinputTimerStart(_recvIntervalMsCache);
}
bool ArduinoX360Class::_canSend() const{
    if(!tud_mounted()) return false;
    if(!_xinputMounted){ _xinputMountedAt=millis(); _xinputMounted=true; }
    return ((unsigned long)(millis()-_xinputMountedAt))>=100UL;
}
bool ArduinoX360Class::isConnected() const{
    if(!_connected) return false;
    return tud_mounted();
}
uint32_t ArduinoX360Class::setPollInterval(uint32_t ms){
    uint32_t old=_pollIntervalMs;
    if(ms!=0 && ms<4) ms=4; _recvIntervalMsCache=ms; _pollIntervalMs=ms;
    _xinputRestartTimer();
    return old;
}
void ArduinoX360Class::_sendReport(){
    if(!_dirtyFlag.load()) return;
    if(!_canSend()) return;
    memcpy(g_xinputReportBuffer,&_report,sizeof(_report));
    if(!_xinputArmIn(g_xinputReportBuffer)) _xinputTxPending=true;
    else { _xinputTxPending=false; _dirtyFlag.store(false); }
}
void ArduinoX360Class::update(){ _sendReport(); }
void ArduinoX360Class::send(){ _sendReport(); }
bool ArduinoX360Class::ready(){ return isConnected(); }
void ArduinoX360Class::releaseAll(){ memset(&_report,0,sizeof(_report)); _report.bMessageType=0x00; _report.bMessageSize=0x14; _markDirty(); _sendReport(); }
void ArduinoX360Class::pollRumble(){
    if(!tud_mounted()) return;
    while(tud_vendor_n_available(0)){
        uint32_t len=tud_vendor_n_read(0,g_xinputOutBuffer,sizeof(g_xinputOutBuffer));
        if(len<1) continue;
        _receivedAnyOutput=true;
        if(len>=5 && g_xinputOutBuffer[0]==0x00 && _onRumbleCb){ _lastRumbleL=g_xinputOutBuffer[3]; _lastRumbleR=g_xinputOutBuffer[4]; _onRumbleCb(_lastRumbleL,_lastRumbleR); }
        else if(len>=3 && g_xinputOutBuffer[0]==0x01 && _onLedCb){ _lastLedIndex=g_xinputOutBuffer[2]; _onLedCb(_lastLedIndex); }
    }
}
#endif
