#!/usr/bin/env bash
#
# prime_golden.sh - One-time population of the shared golden cache
# (AVENV_GOLDEN) for the ArduinoX360-tinyusb build gate.
#
# Installs every pinned core plus a persistent Adafruit TinyUSB 3.7.7 copy,
# then replicates the Seeed "bundle symlink" hack from AGENTS.md: the Seeed
# cores hardcode -I.../Adafruit_TinyUSB_Arduino/src/arduino into their build,
# which leaks stale headers even when the library is shadowed in user/libraries.
# Replacing the bundled library's src with a symlink to the persistent 3.7.7
# copy fixes that at the source. The adafruit:samd / adafruit:nrf52 cores do
# NOT need this (a user-library install shadows them cleanly).
#
# Idempotent; safe to re-run. Install commands are flock'd so this can run
# concurrently with gate builds without corrupting the golden cache.
#
# Usage:
#   AVENV_GOLDEN=/mnt/projects/arduino15 scripts/prime_golden.sh

set -euo pipefail

export PATH="$HOME/bin:$HOME/.local/bin:$PATH"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/avenv.sh"   # for AVENV_ADDITIONAL_URLS defaults only

G="${AVENV_GOLDEN:-}"
[[ -n "$G" && -d "$G" ]] || {
  echo "prime_golden.sh: AVENV_GOLDEN must be set to an existing directory" >&2
  exit 1
}
LOCK="$G/.install.lock"

PINS=(
  "rp2040:rp2040@6.0.0"
  "esp32:esp32@3.3.11"
  "adafruit:samd@1.7.17"
  "adafruit:nrf52@1.7.0"
  "Seeeduino:samd@1.8.6"
  "Seeeduino:nrf52@1.1.13"
  "arduino:renesas_uno@1.6.0"
)

CFG="$(mktemp)"
trap 'rm -f "$CFG"' EXIT
{
  echo "board_manager:"
  echo "    additional_urls:"
  local_ifs=$' \n\t'
  IFS="$local_ifs"
  for u in $AVENV_ADDITIONAL_URLS; do
    [[ -n "$u" ]] && echo "        - $u"
  done
  echo "directories:"
  echo "    data: $G"
  echo "    user: $G/user-libraries"
  echo "    downloads: $G/downloads"
} > "$CFG"

mkdir -p "$G/user-libraries" "$G/downloads"
# Environment overrides beat config files — pin them to the golden dirs.
export ARDUINO_DIRECTORIES_DATA="$G"
export ARDUINO_DIRECTORIES_DOWNLOADS="$G/downloads"
unset ARDUINO_BUILD_CACHE_PATH
CLI=(arduino-cli --config-file "$CFG")

echo "== priming golden cache at $G =="
for pin in "${PINS[@]}"; do
  echo "-- core install $pin"
  flock "$LOCK" "${CLI[@]}" core install "$pin"
done

echo "-- lib install Adafruit TinyUSB Library@3.7.7 (persistent)"
flock "$LOCK" "${CLI[@]}" lib install "Adafruit TinyUSB Library@3.7.7"

# directories.user is treated as a sketchbook root -> libraries land one deeper.
TINYUSB_SRC="$G/user-libraries/libraries/Adafruit_TinyUSB_Library/src"
[[ -e "$TINYUSB_SRC" ]] || { echo "prime_golden.sh: TinyUSB src missing at $TINYUSB_SRC" >&2; exit 1; }

echo "-- applying Seeed bundle-symlink hack"
shopt -s nullglob
for plat_dir in "$G"/packages/Seeeduino/hardware/*/; do
  for ver_dir in "$plat_dir"*/; do
    bundled="$ver_dir/libraries/Adafruit_TinyUSB_Arduino"
    [[ -e "$bundled" ]] || continue
    if [[ "$(readlink -f "$bundled/src" 2>/dev/null || true)" == "$(readlink -f "$TINYUSB_SRC")" ]]; then
      echo "   already patched: $bundled"
      continue
    fi
    rm -rf "$bundled"
    mkdir -p "$bundled"
    ln -s "$TINYUSB_SRC" "$bundled/src"
    echo "   patched: $bundled/src -> $TINYUSB_SRC"
  done
done

echo "== verifying pinned cores =="
rc=0
for pin in "${PINS[@]}"; do
  name="${pin%@*}"; ver="${pin#*@}"
  if ! "${CLI[@]}" core list | grep -E "^${name//[./:]/\\.}[[:space:]]+$ver" > /dev/null; then
    echo "prime_golden.sh: MISSING $name@$ver" >&2
    rc=1
  fi
done
[[ $rc -eq 0 ]] && echo "== golden cache ready ==" || exit $rc
