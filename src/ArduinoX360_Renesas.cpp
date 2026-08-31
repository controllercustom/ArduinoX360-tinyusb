// SPDX-License-Identifier: MIT
// ArduinoX360-tinyusb — Renesas RA4M1 backend

#if defined(ARDUINO_ARCH_RENESAS)

#include "ArduinoX360.h"
#include "xinput_descriptor.h"
#include <Arduino.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "FspTimer.h"

#define XINPUT_EP_IN 0x85
#define XINPUT_EP_OUT 0x05
static uint16_t _xinputVid=XINPUT_VID_DEFAULT;
static uint16_t _xinputPid=XINPUT_PID_DEFAULT;
static uint8_t g_xinputReportBuffer[XINPUT_REPORT_SIZE] __attribute__((aligned(4)));
static uint8_t g_xinputOutBuffer[64];
static volatile bool _xinputTxBusy=false;
static volatile bool _xinputTxPending=false;
static bool _xinputMounted=false;
static unsigned long _xinputMountedAt=0;
static FspTimer _xinputPumpTimer;
static bool _xinputTimerRunning=false;
static uint32_t _recvIntervalMsCache=8;

static void _xinputTryArm(){
    if(_xinputTxBusy || !tud_mounted()) return;
    if(usbd_edpt_xfer(0,XINPUT_EP_IN,g_xinputReportBuffer,XINPUT_REPORT_SIZE)) _xinputTxBusy=true;
}
extern "C" void tud_vendor_tx_cb(uint8_t itf, uint32_t xferred_bytes);
extern "C" void tud_vendor_tx_cb(uint8_t itf, uint32_t xferred_bytes){ (void)itf;(void)xferred_bytes; _xinputTxBusy=false; if(_xinputTxPending){ _xinputTxPending=false; _xinputTryArm(); } }

extern "C" bool __USBGetVidPid(uint16_t *vid, uint16_t *pid){ *vid=_xinputVid; *pid=_xinputPid; return true; }
extern "C" uint8_t const *__USBGetCustomInterfaceDescriptor(uint8_t itfnum, size_t *len, uint8_t *num_interfaces){
    static uint8_t desc[XINPUT_IFACE_DESC_LEN];
    memcpy(desc,g_xinputIfaceDesc,sizeof(desc));
    desc[2]=itfnum; desc[8]=0; desc[28]=XINPUT_EP_IN; desc[35]=XINPUT_EP_OUT; desc[15]=XINPUT_EP_IN; desc[22]=XINPUT_EP_OUT;
    *len=XINPUT_IFACE_DESC_LEN; *num_interfaces=1; return desc;
}
extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);
extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request){
    if(stage!=CONTROL_STAGE_SETUP) return true;
    if(request->bmRequestType_bit.recipient==TUSB_REQ_RCPT_INTERFACE && request->bmRequestType_bit.type==TUSB_REQ_TYPE_VENDOR && request->bmRequestType_bit.direction==TUSB_DIR_IN && request->bRequest==0x01){
        if(request->wValue==0x0100){ static uint8_t buf[XINPUT_REPORT_SIZE]; memcpy(buf,g_xinputReportBuffer,sizeof(buf)); return tud_control_xfer(rhport,request,buf,XINPUT_REPORT_SIZE); }
        if(request->wValue==0x0000){ static uint8_t caps[8]={0x00,0x08,0x00,0xFF,0xFF,0x00,0x00,0x00}; return tud_control_xfer(rhport,request,caps,sizeof(caps)); }
    }
    return false;
}

static void _xinputPumpCallback(timer_callback_args_t *arg){
    (void)arg; ArduinoX360.pollRumble(); ArduinoX360.send();
    if(_xinputTxPending && !_xinputTxBusy){ _xinputTxPending=false; _xinputTryArm(); }
}
static bool _xinputStartTimer(uint32_t interval_ms){
    uint32_t hz=(interval_ms==0)?0:(1000UL/interval_ms); if(hz==0||hz>250) hz=250;
    uint8_t type=GPT_TIMER; int8_t ch=FspTimer::get_available_timer(type);
    if(ch<0){ FspTimer::force_use_of_pwm_reserved_timer(); ch=FspTimer::get_available_timer(type,true); if(ch<0) return false; }
    if(!_xinputPumpTimer.begin(TIMER_MODE_PERIODIC,type,(uint8_t)ch,(float)hz,50.0f,_xinputPumpCallback)) return false;
    if(!_xinputPumpTimer.setup_overflow_irq(14)){ _xinputPumpTimer.end(); return false; }
    if(!_xinputPumpTimer.open()){ _xinputPumpTimer.end(); return false; }
    return _xinputPumpTimer.start();
}
static void _xinputRestartTimer(){
    if(_xinputTimerRunning){ _xinputPumpTimer.end(); _xinputTimerRunning=false; }
    if(_recvIntervalMsCache>0) _xinputTimerRunning=_xinputStartTimer(_recvIntervalMsCache);
}

void ArduinoX360Class::begin(uint16_t vid, uint16_t pid){
    _xinputVid=vid; _xinputPid=pid;
    memset(g_xinputReportBuffer,0,sizeof(g_xinputReportBuffer));
    g_xinputReportBuffer[0]=0x00; g_xinputReportBuffer[1]=0x14;
    _connected=true; _report.bMessageType=0x00; _report.bMessageSize=0x14;
    _recvIntervalMsCache=_pollIntervalMs;
    if(!_xinputTimerRunning && _recvIntervalMsCache>0) _xinputTimerRunning=_xinputStartTimer(_recvIntervalMsCache);
}
bool ArduinoX360Class::_canSend() const{
    if(!tud_mounted()) return false;
    if(!_xinputMounted){ _xinputMountedAt=millis(); _xinputMounted=true; }
    return ((unsigned long)(millis()-_xinputMountedAt))>=100UL;
}
bool ArduinoX360Class::isConnected() const{ if(!_connected) return false; return tud_mounted(); }
uint32_t ArduinoX360Class::setPollInterval(uint32_t ms){
    uint32_t old=_pollIntervalMs;
    if(ms!=0 && ms<4) ms=4; _pollIntervalMs=ms; _recvIntervalMsCache=ms;
    _xinputRestartTimer(); return old;
}
void ArduinoX360Class::_sendReport(){
    if(!_dirtyFlag.load()) return;
    if(!_canSend()) return;
    memcpy(g_xinputReportBuffer, &_report, sizeof(_report));
    if(_xinputTxBusy) _xinputTxPending=true;
    else _xinputTryArm();
    _dirtyFlag.store(false);
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
