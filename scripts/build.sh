#!/usr/bin/env bash
set -euo pipefail
export PATH="$HOME/bin:$HOME/.local/bin:$PATH"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/avenv.sh"
usage() { echo "usage: build.sh <board> <sketch_dir> [arduino-cli args...]" >&2; echo "  board ∈ rp2040 | esp32 | esp32s2 | samd21 | samd51 | grandcentral | nrf52 | xiaom0 | wioterminal | xiaonrf52 | renesas | minima" >&2; exit 2; }
[[ $# -ge 2 ]] || usage
BOARD_KEY="$1"; shift
if [[ ! -d "$1" ]]; then echo "build.sh: no such sketch dir: $1" >&2; exit 1; fi
TARGET="$(cd "$1" && pwd)"; shift
AVENV_GOLDEN="${AVENV_GOLDEN:-}"
[[ -n "$AVENV_GOLDEN" && -d "$AVENV_GOLDEN" ]] || { echo "build.sh: AVENV_GOLDEN must be set to an existing directory (see scripts/prime_golden.sh)" >&2; exit 1; }
LOCK="$AVENV_GOLDEN/.install.lock"
case "$BOARD_KEY" in
  rp2040) CORE="rp2040:rp2040@6.0.0"; FQBN="rp2040:rp2040:rpipico:usbstack=tinyusb,freq=200,flash=2097152_0,opt=Small"; NEED_TINYUSB=0 ;;
  esp32) CORE="esp32:esp32@3.3.11"; FQBN="esp32:esp32:esp32s3:USBMode=default"; NEED_TINYUSB=0 ;;
  esp32s2) CORE="esp32:esp32@3.3.11"; FQBN="esp32:esp32:esp32s2"; NEED_TINYUSB=0 ;;
  samd21) CORE="adafruit:samd@1.7.17"; FQBN="adafruit:samd:adafruit_feather_m0_express:usbstack=tinyusb"; NEED_TINYUSB=1 ;;
  samd51) CORE="adafruit:samd@1.7.17"; FQBN="adafruit:samd:adafruit_metro_m4:usbstack=tinyusb"; NEED_TINYUSB=1 ;;
  grandcentral) CORE="adafruit:samd@1.7.17"; FQBN="adafruit:samd:adafruit_grandcentral_m4:usbstack=tinyusb"; NEED_TINYUSB=1 ;;
  nrf52) CORE="adafruit:nrf52@1.7.0"; FQBN="adafruit:nrf52:feather52840"; NEED_TINYUSB=1 ;;
  xiaom0) CORE="Seeeduino:samd@1.8.6"; FQBN="Seeeduino:samd:seeed_XIAO_m0:usbstack=tinyusb"; NEED_TINYUSB=1 ;;
  wioterminal) CORE="Seeeduino:samd@1.8.6"; FQBN="Seeeduino:samd:seeed_wio_terminal:usbstack=tinyusb"; NEED_TINYUSB=1 ;;
  xiaonrf52) CORE="Seeeduino:nrf52@1.1.13"; FQBN="Seeeduino:nrf52:xiaonRF52840"; NEED_TINYUSB=1 ;;
  renesas) CORE="arduino:renesas_uno@1.6.0"; FQBN="arduino:renesas_uno:nanor4"; NEED_TINYUSB=0; EXTRA_FLAGS="-DDISABLE_USB_SERIAL" ;;
  minima) CORE="arduino:renesas_uno@1.6.0"; FQBN="arduino:renesas_uno:minima"; NEED_TINYUSB=0; EXTRA_FLAGS="-DDISABLE_USB_SERIAL" ;;
  *) echo "build.sh: unknown board '$BOARD_KEY'" >&2; usage ;;
esac
aventools_init "ardx360-$BOARD_KEY"
flock "$LOCK" arduino-cli core install "$CORE"
if [[ "$NEED_TINYUSB" -eq 1 ]]; then flock "$LOCK" arduino-cli lib install "Adafruit TinyUSB Library@3.7.7"; fi
SKETCH_YAML="$TARGET/sketch.yaml"
HAD_YAML=0
if [[ -e "$SKETCH_YAML" ]]; then mv "$SKETCH_YAML" "$SKETCH_YAML.buildbak"; HAD_YAML=1; fi
restore_yaml() { if [[ $HAD_YAML -eq 1 && -e "$SKETCH_YAML.buildbak" ]]; then mv "$SKETCH_YAML.buildbak" "$SKETCH_YAML"; fi; aventools_cleanup; }
trap restore_yaml EXIT INT TERM HUP
if [[ "${EXTRA_FLAGS:-}" != "" ]]; then
  arduino-cli compile --fqbn "$FQBN" --build-property "compiler.cpp.extra_flags=$EXTRA_FLAGS" --library "$PROJ_ROOT" "$TARGET" "$@"
else
  arduino-cli compile --fqbn "$FQBN" --library "$PROJ_ROOT" "$TARGET" "$@"
fi
