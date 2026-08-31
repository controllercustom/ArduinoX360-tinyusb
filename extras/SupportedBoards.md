# Supported Boards — ArduinoX360-tinyusb

TinyUSB vendor-class backends live in this repository
(`src/ArduinoX360_RP2040.cpp`, `ArduinoX360_ESP32.cpp`,
`ArduinoX360_SAMD.cpp`, `ArduinoX360_NRF52.cpp`,
`ArduinoX360_Renesas.cpp`) with a shared board-agnostic state machine
(`src/ArduinoX360.cpp` + `src/ArduinoX360.h`).

## Raspberry Pi RP2040 / RP2350 (arduino-pico core)

* Raspberry Pi Pico / Pico W / Pico 2 / Pico 2 W
* Any `rp2040`/`rp2350` board supported by earlephilhower/arduino-pico

Requires **Adafruit TinyUSB** USB stack (`usbstack=tinyusb`). Upload via UF2/BOOTSEL default. Do not use `picotool` upload — library defines `tud_vendor_control_xfer_cb` and will refuse to compile with `ENABLE_PICOTOOL_USB`.

## Espressif ESP32-S2 / S3 (arduino-esp32 core)

* ESP32-S2, ESP32-S3

Any board with native USB-OTG. Requires **USB-OTG (TinyUSB)** mode (`USBMode=default` on S3; S2 has no USB-Mode menu). Classic ESP32, C3, C6 have no USB peripheral and are unsupported.

## Atmel SAMD21 / SAMD51 (Adafruit SAMD core)

* SAMD21 (M0+) — Feather M0 Express, Metro M0, Circuit Playground Express
* SAMD51 (M4) — Metro M4, Grand Central M4 (distinct FQBN)

Requires **TinyUSB** USB stack (`usbstack=tinyusb`). Library drops CDC (`Serial.end()`) so device enumerates as pure vendor-class; re-enter bootloader via double-tap reset.

## Nordic nRF52840 / nRF52833 (Adafruit nRF52 core)

* Any `adafruit:nrf52` board with nRF52840/33 — Feather nRF52840 Express, ItsyBitsy nRF52840, dongle

USB is always TinyUSB. Library drops CDC; re-enter bootloader via double-tap → UF2.

## Seeed (Seeeduino cores)

* Seeeduino XIAO M0, XIAO M0 Plus, Femto M0, Seeeduino Zero, Wio Terminal — `Seeeduino:samd` `usbstack=tinyusb`
* Seeed XIAO nRF52840, Sense, Plus — `Seeeduino:nrf52` (always TinyUSB)

Add `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json`.
These cores bundle old Adafruit TinyUSB; install **Adafruit TinyUSB Library ≥ 3.0** from Library Manager — it shadows the bundled version.

## Renesas RA4M1 — Nano R4 / UNO R4 Minima (arduino:renesas_uno)

Requires one-time patch of installed core 1.6.0:

```bash
scripts/patch_renesas_core.sh            # Linux
scripts/patch_renesas_core.macos.sh      # macOS
scripts\patch_renesas_core.windows.ps1   # Windows
```

Build with `DISABLE_USB_SERIAL` so XInput is interface 0:

```bash
arduino-cli compile --fqbn arduino:renesas_uno:nanor4 \
  --build-property compiler.cpp.extra_flags=-DDISABLE_USB_SERIAL \
  --library ~/ArduinoX360-tinyusb <sketch>
```

FQBNs: `arduino:renesas_uno:nanor4` or `:minima`. Upload via double-tap reset DFU or J-Link SWD.

## Not supported

* Classic ESP32 (no USB-OTG), nRF52832, AVR/Teensy — no USB or intentionally not supported.
