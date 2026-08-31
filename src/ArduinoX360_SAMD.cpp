// SPDX-License-Identifier: MIT
// ArduinoX360-tinyusb — SAMD21/SAMD51 backend

#if defined(ARDUINO_ARCH_SAMD) && defined(USE_TINYUSB)

#include "ArduinoX360.h"
#include "xinput_descriptor.h"
#include <Arduino.h>
#include "Adafruit_TinyUSB.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "device/dcd.h"

static uint8_t g_xinputReportBuffer[XINPUT_REPORT_SIZE] __attribute__((aligned(4)));
static uint8_t g_xinputOutBuffer[64];
static uint8_t _xinputEpIn = 0;
static uint8_t _xinputEpOut = 0;

class XInputInterface : public Adafruit_USBD_Interface {
public:
    bool begin(void) { return TinyUSBDevice.addInterface(*this); }
    uint16_t getInterfaceDescriptor(uint8_t itfnum_deprecated, uint8_t *buf, uint16_t bufsize) override {
        (void)itfnum_deprecated;
        uint8_t itfnum=0, ep_in=0, ep_out=0;
        if (buf) { itfnum=TinyUSBDevice.allocInterface(1); ep_in=TinyUSBDevice.allocEndpoint(TUSB_DIR_IN); ep_out=TinyUSBDevice.allocEndpoint(TUSB_DIR_OUT); }
        uint8_t desc[XINPUT_IFACE_DESC_LEN];
        memcpy(desc, g_xinputIfaceDesc, sizeof(desc));
        desc[2]=itfnum; desc[8]=_strid; desc[28]=ep_in; desc[35]=ep_out;
        if (buf) { _xinputEpIn=ep_in; _xinputEpOut=ep_out; }
        uint16_t len=sizeof(desc);
        if (buf) { if(bufsize<len) return 0; memcpy(buf,desc,len); }
        return len;
    }
};
static XInputInterface _xinputInterface;
static uint8_t _xinputInBuf[XINPUT_REPORT_SIZE] __attribute__((aligned(4)));
static volatile bool _xinputTxPending=false;
static volatile bool _xinputTxBusy=false;
static bool _xinputMounted=false;
static unsigned long _xinputMountedAt=0;
static bool _xinputTimerRunning=false;
static uint32_t _recvIntervalMsCache=8;

static inline bool _xinputHwBankReady(){ return USB->DEVICE.DeviceEndpoint[_xinputEpIn & 0x0F].EPSTATUS.bit.BK1RDY != 0; }
static bool _xinputTrySend(const uint8_t *src){
    if(_xinputEpIn==0) return false;
    if(!tud_mounted() || !tud_ready()) return false;
    bool ok=false; uint32_t prim=__get_PRIMASK(); __disable_irq();
    if(!_xinputTxBusy && !_xinputHwBankReady()){
        memcpy(_xinputInBuf, src, XINPUT_REPORT_SIZE);
        ok=dcd_edpt_xfer(0,_xinputEpIn,_xinputInBuf,XINPUT_REPORT_SIZE,false);
        _xinputTxBusy=ok;
    }
    if(!prim) __enable_irq();
    return ok;
}

extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);
extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request){
    if(stage!=CONTROL_STAGE_SETUP) return true;
    if(request->bmRequestType_bit.recipient==TUSB_REQ_RCPT_INTERFACE &&
       request->bmRequestType_bit.type==TUSB_REQ_TYPE_VENDOR &&
       request->bmRequestType_bit.direction==TUSB_DIR_IN && request->bRequest==0x01){
        if(request->wValue==0x0100){ static uint8_t buf[XINPUT_REPORT_SIZE]; memcpy(buf,g_xinputReportBuffer,sizeof(buf)); return tud_control_xfer(rhport,request,buf,XINPUT_REPORT_SIZE); }
        if(request->wValue==0x0000){ static uint8_t caps[8]={0x00,0x08,0x00,0xFF,0xFF,0x00,0x00,0x00}; return tud_control_xfer(rhport,request,caps,sizeof(caps)); }
    }
    return false;
}

static void _xinputPumpCallback(){
    ArduinoX360.pollRumble();
    if(_xinputTxBusy && !_xinputHwBankReady()) _xinputTxBusy=false;
    ArduinoX360.send();
    if(_xinputTxPending && tud_mounted() && tud_ready()){
        if(_xinputTrySend(g_xinputReportBuffer)) _xinputTxPending=false;
    }
}

#if defined(ARDUINO_SAMD_ZERO)
#define XINPUT_TC TC4
#define XINPUT_TC_IRQ TC4_IRQn
void TC4_Handler(void){ XINPUT_TC->COUNT16.INTFLAG.bit.MC0=1; _xinputPumpCallback(); }
#elif defined(__SAMD51__)
#define XINPUT_TC TC0
#define XINPUT_TC_IRQ TC0_IRQn
void TC0_Handler(void){ XINPUT_TC->COUNT16.INTFLAG.bit.MC0=1; _xinputPumpCallback(); }
#endif

static void _xinputTimerStart(uint32_t ms){
    uint32_t cc=(F_CPU/1024UL*ms)/1000UL;
    if(cc>0xFFFFUL) cc=0xFFFFUL; if(cc==0) cc=1; uint16_t top=(uint16_t)(cc-1);
#if defined(ARDUINO_SAMD_ZERO)
    PM->APBCMASK.reg|=PM_APBCMASK_TC4;
    GCLK->CLKCTRL.reg=GCLK_CLKCTRL_ID_TC4_TC5 | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_CLKEN;
    while(GCLK->STATUS.bit.SYNCBUSY){}
    XINPUT_TC->COUNT16.CTRLA.reg=TC_CTRLA_SWRST; while(XINPUT_TC->COUNT16.STATUS.bit.SYNCBUSY){}
    XINPUT_TC->COUNT16.CTRLA.reg=TC_CTRLA_MODE_COUNT16 | TC_CTRLA_WAVEGEN_MFRQ | TC_CTRLA_PRESCALER_DIV1024;
    XINPUT_TC->COUNT16.CC[0].reg=top; XINPUT_TC->COUNT16.INTENSET.reg=TC_INTENSET_MC0;
#elif defined(__SAMD51__)
    MCLK->APBAMASK.reg|=MCLK_APBAMASK_TC0;
    GCLK->PCHCTRL[TC0_GCLK_ID].reg=GCLK_PCHCTRL_GEN_GCLK0_Val | (1<<GCLK_PCHCTRL_CHEN_Pos);
    while(GCLK->SYNCBUSY.reg){}
    XINPUT_TC->COUNT16.CTRLA.reg=TC_CTRLA_SWRST; while(XINPUT_TC->COUNT16.SYNCBUSY.bit.SWRST){}
    XINPUT_TC->COUNT16.CTRLA.reg=TC_CTRLA_MODE_COUNT16 | TC_CTRLA_PRESCALER_DIV1024;
    XINPUT_TC->COUNT16.WAVE.reg=TC_WAVE_WAVEGEN_MFRQ;
    XINPUT_TC->COUNT16.CC[0].reg=top; XINPUT_TC->COUNT16.INTENSET.reg=TC_INTENSET_MC0;
#endif
    NVIC_ClearPendingIRQ(XINPUT_TC_IRQ); NVIC_SetPriority(XINPUT_TC_IRQ,3); NVIC_EnableIRQ(XINPUT_TC_IRQ);
    XINPUT_TC->COUNT16.CTRLA.reg|=TC_CTRLA_ENABLE;
#if defined(ARDUINO_SAMD_ZERO)
    while(XINPUT_TC->COUNT16.STATUS.bit.SYNCBUSY){}
#elif defined(__SAMD51__)
    while(XINPUT_TC->COUNT16.SYNCBUSY.bit.ENABLE){}
#endif
}
static void _xinputTimerStop(){
#if defined(ARDUINO_SAMD_ZERO) || defined(__SAMD51__)
    NVIC_DisableIRQ(XINPUT_TC_IRQ);
    XINPUT_TC->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE;
#if defined(ARDUINO_SAMD_ZERO)
    while(XINPUT_TC->COUNT16.STATUS.bit.SYNCBUSY){}
#elif defined(__SAMD51__)
    while(XINPUT_TC->COUNT16.SYNCBUSY.bit.ENABLE){}
#endif
#endif
    _xinputTimerRunning=false;
}
static void _xinputRestartTimer(){
    _xinputTimerStop();
    if(_recvIntervalMsCache>0){ _xinputTimerStart(_recvIntervalMsCache); _xinputTimerRunning=true; }
}

// ArduinoX360Class methods for SAMD
void ArduinoX360Class::begin(uint16_t vid, uint16_t pid){
    Serial.end();
#ifdef XINPUT_DEBUG_CDC
    Serial.begin(115200);
#endif
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
    if(!_xinputTimerRunning && _recvIntervalMsCache>0){ _xinputTimerStart(_recvIntervalMsCache); _xinputTimerRunning=true; }
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
    if(ms!=0 && ms<4) ms=4;
    _pollIntervalMs=ms; _recvIntervalMsCache=ms;
    _xinputRestartTimer();
    return old;
}
void ArduinoX360Class::_sendReport(){
    if(!_dirtyFlag.load()) return;
    if(!_canSend()) return;
    memcpy(g_xinputReportBuffer, &_report, sizeof(_report));
    if(!_xinputTrySend(g_xinputReportBuffer)) _xinputTxPending=true;
    else { _xinputTxPending=false; _dirtyFlag.store(false); }
}
void ArduinoX360Class::update(){ _sendReport(); }
void ArduinoX360Class::send(){ _sendReport(); }
bool ArduinoX360Class::ready(){ return isConnected(); }
void ArduinoX360Class::releaseAll(){
    memset(&_report,0,sizeof(_report)); _report.bMessageType=0x00; _report.bMessageSize=0x14; _markDirty(); _sendReport();
}
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
