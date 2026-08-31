# ArduinoX360-tinyusb — Agent Instructions

Xbox 360 emulation over TinyUSB for RP2040/RP2350 + ESP32-S2/S3 + SAMD21/SAMD51 + nRF52 + Renesas RA4M1.
Public API = ESP32XInput/PIPICXInput parity: `ArduinoX360` instance of `ArduinoX360Class` with Button enum, triggers, sticks, hat, onRumble/onLed, setPollInterval.

## Build

Toolchain: arduino-cli. Cores: rp2040:rp2040 6.0.0, esp32:esp32 3.3.11, adafruit:samd 1.7.17, adafruit:nrf52 1.7.0, Seeeduino:samd 1.8.6, Seeeduino:nrf52 1.1.13, arduino:renesas_uno 1.6.0. All SAMD/nRF52 need Adafruit TinyUSB Library 3.7.7 as user library.

### Isolated build

```bash
export AVENV_GOLDEN=/tmp/arduino_golden
scripts/prime_golden.sh
scripts/build.sh <board> <sketch> [extra args]
# board ∈ rp2040|esp32|esp32s2|samd21|samd51|grandcentral|nrf52|xiaom0|wioterminal|xiaonrf52|renesas
```

Board table and FQBNs live in `scripts/build.sh`.

## Source map

* `src/ArduinoX360.h`/.cpp — public API + state machine (shared)
* `src/ArduinoX360_RP2040.cpp` — Adafruit TinyUSB + repeating_timer
* `src/ArduinoX360_ESP32.cpp` — esp32-hal-tinyusb + esp_timer
* `src/ArduinoX360_SAMD.cpp` — TC4/TC0 MFRQ + dcd_edpt_xfer
* `src/ArduinoX360_NRF52.cpp` — SoftwareTimer + usbd_edpt_xfer
* `src/ArduinoX360_Renesas.cpp` — FspTimer + __USBGet* hooks
* `src/xinput_descriptor.h` — golden 40-byte iface block
* `src/ESP32XInput.h`, `PIPICXInput.h` — shims to ArduinoX360.h

## Pitfalls

* All `src/*.cpp` compile on every board → guard by `#if defined(ARDUINO_ARCH_*)`
* ESP32 magic message: weak `tinyusb_vendor_control_request_cb`, not `tud_vendor_control_xfer_cb`
* RP2040: do not use `picotool` upload (strong vendord cb conflict)
* SAMD: TC registers differ M0/M4; pump IRQ priority 3 (USB is 0)
* Renesas needs patched core + DISABLE_USB_SERIAL
