# ArduinoX360-tinyusb

Xbox 360 controller emulation over USB for Raspberry Pi RP2040/RP2350,
ESP32-S2/S3, SAMD21/SAMD51, nRF52840/nRF52833, Seeed XIAO / Wio Terminal,
and Arduino Nano R4 / UNO R4 Minima (Renesas RA4M1), using TinyUSB vendor-class backends inside the library.

Public API matches `ESP32XInput`:

```cpp
#include <ArduinoX360.h>

void setup() {
  ArduinoX360.begin(); // optionally begin(vid, pid)
  ArduinoX360.onRumble([](uint8_t l, uint8_t r){});
  ArduinoX360.onLed([](uint8_t idx){});
}

void loop() {
  ArduinoX360.press(ArduinoX360Class::A);
  ArduinoX360.setStickLeft(0, 32767);
  ArduinoX360.setLeftTrigger(32768);
  ArduinoX360.send();
  delay(8);
  ArduinoX360.pollRumble();
}
```

Compatibility shim is included: `#include <ESP32XInput.h>` aliases `ArduinoX360`.

> Console note: No security handshake is implemented, so the device will not bind to an Xbox console. This can be used with Xbox consoles using adapters/converters from Brook and Mayflash.

## Installation

### Arduino IDE — Add .ZIP Library (recommended for most users)

1. **Download the ZIP** — on GitHub open `https://github.com/controllercustom/ArduinoX360-tinyusb` and click the green **Code** button → **Download ZIP** (or go to **Releases** and download the latest `ArduinoX360-tinyusb-*.zip`).
2. **Install the ZIP in the IDE** — open the Arduino IDE (1.x or 2.x) and choose **Sketch → Include Library → Add .ZIP Library…**, then select the ZIP you just downloaded. The IDE extracts it to your sketchbook `libraries/` folder (e.g. `~/Arduino/libraries/ArduinoX360-tinyusb` on Linux, `~/Documents/Arduino/libraries/ArduinoX360-tinyusb` on macOS/Windows).
   - **Manual alternative:** unzip the file and copy/rename the folder to `ArduinoX360-tinyusb` inside the same `libraries/` folder.
3. **Restart the IDE** if it was open during install.
4. **Open an example** from **File → Examples → ArduinoX360-tinyusb** (e.g. `BasicButtons`).
5. **Install board support and dependencies** as listed per board family below (board cores via the **Boards Manager** icon, libraries via the **Library Manager** icon).

Alternatively for `arduino-cli` or fully manual installs, copy/clone this folder to `~/Arduino/libraries/ArduinoX360-tinyusb` (or your sketchbook `libraries/` path).

### Board-specific setup

Each section below is a numbered flow for the current IDE (Arduino IDE 2.x); only the URLs, core name/version, board, and Tools options change between families. Common conventions:

* **Boards Manager** — the bottom-left board icon, or **Tools → Boards Manager…**; the Boards tab lists searchable cores with an **Install** button on each. Always installs the *latest* version of a core — the exact version in each section below is what this project builds and tests against (see the build harness); if you need that exact version, install it with `arduino-cli core install` instead.
* **Library Manager** — the bottom-left book icon, or **Tools → Library Manager…**; used only where a section calls for it (the Adafruit TinyUSB Library on SAMD/nRF52/Seeed).
* **Additional Boards Manager URLs** — **File → Preferences → Board Manager** tab. If the field already holds other URLs, add a comma and a space before pasting; then click **OK**.
* The `FQBN` string at the end of each section is the exact equivalent `arduino-cli` flag (see the build script).

#### RP2040/RP2350 — Earle Philhower `rp2040:rp2040` 6.0.0

1. **Add the Boards Manager URL:** at **File → Preferences → Board Manager** tab → **Additional Boards Manager URLs**, paste
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
   and click **OK**.
2. **Install the core:** click the **Boards Manager** icon (bottom-left) or open **Tools → Boards Manager…**; on the **Boards** tab search `Raspberry Pi Pico` and hit **Install** on **Raspberry Pi Pico / RP2040** — the build harness pins `6.0.0`, which is what the library is tested against.
3. **Select your board:** under **Tools → Raspberry Pi Pico / RP2040 →** pick your board — e.g. **Raspberry Pi Pico**, **Raspberry Pi Pico W**, or a Waveshare / Adafruit RP2040 (the same name appears in the top-toolbar board picker).
4. **Set the Tools options** (each appears on its own row under the **Tools** menu):
   * **Tools → USB Stack → Adafruit TinyUSB** (`usbstack=tinyusb`)
   * **Tools → Flash Size → 2MB (no FS)** (`flash=2097152_0`)
   * **Tools → CPU Speed → 200 MHz** (`freq=200`)
   * **Tools → Optimize → Small** (`opt=Small`)
   
   → FQBN `rp2040:rp2040:rpipico:usbstack=tinyusb,freq=200,flash=2097152_0,opt=Small`.
5. **Upload:** use the USB serial port in **Tools → Port** — do not select a `pico-tool:` port (the core offers it as an alternative flash path, but the library's TinyUSB vendor hooks conflict with `ENABLE_PICOTOOL_USB` and would fail to build).

#### ESP32-S2 / ESP32-S3 — Espressif `esp32:esp32` 3.3.11

1. **Add the Boards Manager URL:** at **File → Preferences → Board Manager** tab → **Additional Boards Manager URLs**, paste
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   and click **OK**.
2. **Install the core:** click the **Boards Manager** icon (bottom-left) or open **Tools → Boards Manager…**; on the **Boards** tab search `esp32` and hit **Install** on **esp32** — the build harness pins `3.3.11`.
3. **Select your board:** under **Tools → esp32 →** pick the module.
   
   - **ESP32-S3 (dev module or an S3-based board):** choose e.g. **ESP32S3 Dev Module** (or whichever S3 entry best matches your hardware — Flash Size / PSRAM can be adjusted via **Tools → Flash Size**, **Tools → PSRAM** if needed). Then set **Tools → USB Mode → USB-OTG (TinyUSB)** — this is required; the alternative option (*Hardware CDC*) builds without the USB-OTG stack the library needs. Leave other options at defaults.
     
     → FQBN `esp32:esp32:esp32s3:USBMode=default`.
   - **ESP32-S2:** choose **ESP32S2 Dev Module** (or a matching S2 board). The S2 has no **USB Mode** menu — TinyUSB is the only stack — so no option changes are needed.
     
     → FQBN `esp32:esp32:esp32s2`.
4. **Upload:** use the normal USB upload — plug the board in, pick the port that appears in **Tools → Port**, and build/upload. No Library Manager step is needed for this family (it ships its own USB-OTG TinyUSB stack).

#### SAMD21 / SAMD51 — Adafruit `adafruit:samd` 1.7.17

1. **Add the Boards Manager URL:** at **File → Preferences → Board Manager** tab → **Additional Boards Manager URLs**, paste
   `https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`
   and click **OK** (this index covers the SAMD and the nRF52 cores — add once).
2. **Install the core:** click the **Boards Manager** icon (bottom-left) or open **Tools → Boards Manager…**; on the **Boards** tab search `Adafruit SAMD` and hit **Install** on **Adafruit SAMD Boards** — the build harness pins `1.7.17`.
3. **Select your board & set the Tools option:** under **Tools → Adafruit SAMD →** pick your board (e.g. **Adafruit Feather M0 Express**, **Adafruit Metro M4**, **Adafruit Grand Central M4**), then set **Tools → USB Stack → TinyUSB** (the core's *USB Stack* menu default is **Arduino**, which builds the non-TinyUSB CDC path — you must switch it):
   
   → FQBN `adafruit:samd:adafruit_feather_m0_express:usbstack=tinyusb` (or `adafruit:samd:adafruit_metro_m4:usbstack=tinyusb` / `adafruit:samd:adafruit_grandcentral_m4:usbstack=tinyusb`).
4. **Install the library dependency:** the Adafruit core bundles an old copy of the Adafruit TinyUSB. Open the **Library Manager** icon (bottom-left) or **Tools → Library Manager…**, search `Adafruit TinyUSB Library`, and install version **3.7.7** (or newer). This user-library install automatically shadows the older copy bundled with the core — no extra steps are needed.
5. **Upload:** normal USB upload via the serial port the IDE lists for the board. The library uses the full-CDC slot, so after flashing a sketch, use the board's reset (or the double-tap-reset bootloader entry) to re-enumerate before re-uploading if the IDE reports a failure.

#### nRF52840 / nRF52833 — Adafruit `adafruit:nrf52` 1.7.0

1. **Add the Boards Manager URL:** same Adafruit index as the SAMD section (`https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`) — if you already added it for SAMD, this step is a no-op. (Add via **File → Preferences → Board Manager** tab → **Additional Boards Manager URLs**.)
2. **Install the core:** click the **Boards Manager** icon (bottom-left) or open **Tools → Boards Manager…**; on the **Boards** tab search `Adafruit nRF52` and hit **Install** on **Adafruit nRF52** — the build harness pins `1.7.0`.
3. **Select your board:** under **Tools → Adafruit nRF52 →** pick your board (e.g. **Adafruit Feather 52840 Express**, **Adafruit ItsyBitsy nRF52840**, or a nRF52833-based board). There is **no** **USB Stack** menu here — every nRF52 build in this core uses TinyUSB (USB), so no option is required.
4. **Optional board options:** if the board exposes a **Tools → SoftDevice** menu, you can leave it at the default **S140 6.1.1** (equivalent FQBN `adafruit:nrf52:feather52840:softdevice=s140v6`). (Some nRF52 boards also expose a *Debug* menu — leave it at the default; no option here affects USB.)
5. **Install the library dependency:** open the **Library Manager** icon (bottom-left) or **Tools → Library Manager…**, search `Adafruit TinyUSB Library`, and install version **3.7.7** (or newer). This user-library install shadows the older copy bundled in the core.
6. **Upload:** normal USB upload through the port that appears in **Tools → Port** once the board has been programmed with this core.

#### Seeed XIAO / Wio Terminal — `Seeeduino:samd` 1.8.6 / `Seeeduino:nrf52` 1.1.13

1. **Add the Boards Manager URL:** at **File → Preferences → Board Manager** tab → **Additional Boards Manager URLs**, paste
   `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json`
   and click **OK**.
2. **Install the cores:** click the **Boards Manager** icon (bottom-left) or open **Tools → Boards Manager…**; on the **Boards** tab search `Seeed` and hit **Install** on:
   * **Seeed SAMD Boards** (the build harness pins `1.8.6`) — needed for XIAO M0, XIAO 0.9", Wio Terminal and the other SAMD boards, and
   * **Seeed nRF52 Boards** (the build harness pins `1.1.13`) — needed for Seeeduino XIAO nRF52840 (and its Sense / Plus variants).
3. **Select your board & set the Tools option (SAMD boards only):** under **Tools → Seeed SAMD →** pick the board (e.g. **Seeeduino XIAO**, **Wio Terminal**) and set **Tools → USB Stack → TinyUSB**. On Seeed nRF52 boards there is no USB Stack menu, so no option is needed (TinyUSB is always built).
   
   → SAMD: FQBN `Seeeduino:samd:seeed_XIAO_m0:usbstack=tinyusb` (e.g. `Seeeduino:samd:seeed_wio_terminal:usbstack=tinyusb`).
   
   → nRF52: FQBN `Seeeduino:nrf52:xiaonRF52840`.
4. **Install the library dependency:** the Seeed cores bundle an old copy of the Adafruit TinyUSB. Open the **Library Manager** icon (bottom-left) or **Tools → Library Manager…**, search `Adafruit TinyUSB Library`, and install **3.7.7** (recommended; anything ≥ 3.0 works). The IDE uses this Library Manager copy for your sketch. No symlink or extra script is needed for normal IDE use — that workaround in `scripts/` only exists for the maintainer build gate, not IDE builds.
5. **Upload:** normal USB upload through the port that lists in **Tools → Port** once the board has been programmed with these cores.

#### Renesas RA4M1 — `arduino:renesas_uno` 1.6.0 (Arduino Nano R4 / UNO R4 Minima)

1. **Boards Manager URL:** none — the core ships in the default Arduino index, so there's nothing to add (skip **File → Preferences → Board Manager** for this family).
2. **Install the core:** click the **Boards Manager** icon (bottom-left) or open **Tools → Boards Manager…**; on the **Boards** tab search `UNO R4` (or `Renesas`) and hit **Install** on **Arduino UNO R4 Boards** (package `arduino:renesas_uno`) — install **exactly** `1.6.0` (the compatibility patch in step 4 hard-rejects any other core version, and its build harness is pinned to `1.6.0`). Newer releases, if the Boards Manager offers them, need `XR4_ALLOW_ANY_VERSION=1` to patch and are not covered by the project's build gate.
3. **Select your board:** under **Tools → Arduino UNO R4 Boards →** pick **Arduino Nano R4** or **Arduino UNO R4 Minima** (the same name appears in the top-toolbar board picker).
4. **One-time patch (required):** after the core is installed, apply this repository's compatibility patch to the *installed* core — open a terminal in the checked-out copy of this repository and run the script for your OS (it self-locates the core at `~/.arduino15/packages/arduino/hardware/renesas_uno` and is idempotent — re-running on a patched core is a no-op, and re-installing the core from the Boards Manager reverts the patch):
   ```bash
   bash scripts/patch_renesas_core.sh          # Linux
   bash scripts/patch_renesas_core.macos.sh    # Apple silicon (macOS)
   ```
   On Windows, use `scripts\patch_renesas_core.windows.ps1` (the `.windows.bat` twin also ships).
   
   The patch turns the core's TinyUSB **`CFG_TUD_VENDOR`** on (and enables the INTERRUPT endpoints this class needs) so the library's vendor-class Xbox 360 backend (the vendor interface at itf 0) can register. The stock core's `tusb_config.h` keeps it at `0`, so the build silently compiles with no vendor interface until the patch is applied.
5. **Build with `DISABLE_USB_SERIAL`** — the XInput HID must be interface 0, which requires turning the core's CDC *Serial* off. The standard **Tools** menu has no flag option, so use one of:
   
   **`arduino-cli` (simplest, build & upload):**
   ```bash
   arduino-cli compile --fqbn arduino:renesas_uno:nanor4 \
     --build-property compiler.cpp.extra_flags=-DDISABLE_USB_SERIAL \
     --library ~/ArduinoX360-tinyusb examples/BasicButtons
   arduino-cli upload -p <dfu-port> --fqbn arduino:renesas_uno:nanor4 \
     --library ~/ArduinoX360-tinyusb examples/BasicButtons
   ```
   
   **Stay in the IDE:** drop a `platform.local.txt` next to the core's `platform.txt` (e.g. `~/.arduino15/packages/arduino/hardware/renesas_uno/1.6.0/platform.local.txt`) containing
   ```
   compiler.cpp.extra_flags=-DDISABLE_USB_SERIAL
   ```
   then select the board and build/upload normally — the IDE's **Tools → Port** lists a DFU / USB-CDC port once the board has this firmware and the `platform.local.txt` override in place.
6. **Upload options:** the core's uploader is `dfu-util` over **native USB** (`upload_tool=dfu-util`, `native_usb=true`) — so the port picker shows a **DFU** port. If the board isn't currently in DFU mode (it isn't, until told to be), hold **BOOT** + tap **RESET** (per the board's manual), or power-cycle it, before choosing the DFU port and uploading.

## API

See `src/ArduinoX360.h:1`: `XInputReport` (20 bytes), `enum Button { DPAD_UP..Y }`, methods `begin`, `isConnected`/`ready`, `press`/`release`/`setButton`/`getButton`, `setLeftTrigger`/`setRightTrigger` (0..32768 → 0..255), `setStickLeft/Right`, `setHat`/`setDpad`/`getHat`, `onRumble`/`onLed`, `setPollInterval` (0 disables, floor 4 ms), `update`/`send`/`pollRumble`, `releaseAll`, `getLastRumble*`/`getLastLedIndex`/`hasReceivedOutput`/`getReport`.

## Examples

* `BasicButtons` — press/release A
* `BasicGamepad` — UART command parser (BTN_A, LX, TRIG_L, etc.)
* `FullController` — GPIO + analog
* `JoystickTest` — stick/trigger sweeps
* `RumbleFeedback` — onRumble/onLed callbacks
* `AutoCycle` — deterministic phased test for pcap verification

## Verification

Isolated build harness (`scripts/avenv.sh` + `scripts/build.sh`) gives per-build user/libraries, downloads, build cache while sharing cores from `AVENV_GOLDEN`:

```bash
export AVENV_GOLDEN=/tmp/arduino_golden # or persistent dir
scripts/prime_golden.sh
scripts/build.sh rp2040 examples/BasicButtons
scripts/build.sh esp32  examples/BasicButtons
```

Full gate: 10+1 board keys × (6 examples + 3 tests) ≈ 90–99 compiles.

## License

MIT — see `LICENSE`.
