#!/usr/bin/env bash
#
# aventools.sh - Arduino Virtual Environment for CI and concurrent builds
#
# Provides an isolated, reproducible arduino-cli environment so that multiple
# project builds can run at the same time without overwriting each other's
# cores, libraries, downloads, or build caches.
#
# Two ways to use it:
#   1) Sourced (interactive):  source aventools.sh
#                               aventools_init myproject
#                               arduino-cli compile --profile ci .
#                              (temp root auto-removed on shell exit)
#   2) Executed (CI one-liner): ./aventools.sh run myproject -- arduino-cli compile --profile ci .
#
# Design notes / improvements over a naive env-var setup:
#   * Uses a generated per-build `arduino-cli.yaml` (via ARDUINO_CONFIG_FILE)
#     because ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS does NOT split multiple URLs.
#   * Isolates directories.data, directories.user, directories.downloads AND the
#     build cache (XDG_CACHE_HOME) so concurrent builds never share mutable state.
#   * user/libraries starts empty so undeclared dependencies fail the build.
#   * Optional shared read-only "golden" cache (AVENV_GOLDEN) holds the heavy
#     toolchains; per-build state stays isolated for speed + safety in CI.

# ---- default configuration -------------------------------------------------

# 3rd-party board manager indexes. Override with AVENV_ADDITIONAL_URLS
# (space/newline separated) or pass them through your sketch.yaml profile's
# platform_index_url instead.
AVENV_ADDITIONAL_URLS="${AVENV_ADDITIONAL_URLS:-https://espressif.github.io/arduino-esp32/package_esp32_index.json https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json https://adafruit.github.io/arduino-board-index/package_adafruit_index.json https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json }"

# Shared, read-only cache of installed cores/toolchains. Set to a persistent
# path and populate it once with `aventools_prime` to avoid re-downloading
# multi-hundred-MB toolchains on every build. Leave empty for full isolation.
AVENV_GOLDEN="${AVENV_GOLDEN:-}"

AVENV_ROOT="${AVENV_ROOT:-}"
AVENV_DATA="${AVENV_DATA:-}"
AVENV_USER="${AVENV_USER:-}"

# ---- helpers ---------------------------------------------------------------

_aventools_err() { printf 'aventools: %s\n' "$*" >&2; }
_aventools_die() { _aventools_err "$*"; return 1; }

# Write the per-build arduino-cli config file.
_aventools_write_config() {
  local cfg="$1" data="$2" user="$3" dl="$4" cache="$5"
  {
    echo "board_manager:"
    echo "    additional_urls:"
    # split on whitespace
    local IFS=$' \n\t'
    for u in $AVENV_ADDITIONAL_URLS; do
      [[ -n "$u" ]] && echo "        - $u"
    done
    echo "directories:"
    echo "    data: $data"
    echo "    user: $user"
    echo "    downloads: $dl"
    # Pin the build cache INTO the per-build root. Without an explicit key,
    # hosts whose standard arduino-cli.yaml sets build_cache.path (or which
    # export ARDUINO_BUILD_CACHE_PATH) leak a SHARED cache dir into every
    # build — two concurrent builds of the same sketch (different boards)
    # then race in one sketch dir (.libsdetect.d / response-file corruption,
    # 'ld: final link failed: bad value').
    echo "build_cache:"
    echo "    path: $cache/arduino"
  } > "$cfg"
}

# ---- public API ------------------------------------------------------------

# aventools_init [project_name]
# Creates an isolated environment and exports the variables arduino-cli needs.
aventools_init() {
  local project="${1:-${PWD##*/}}"
  [[ -n "$AVENV_ROOT" ]] && _aventools_die "aventools already initialized (AVENV_ROOT=$AVENV_ROOT)"

  AVENV_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/acli_${project}_XXXXXX")" || return 1

  local user dl cache cfg
  if [[ -n "$AVENV_GOLDEN" && -d "$AVENV_GOLDEN" ]]; then
    # Shared, pre-populated, read-only core/toolchain cache (fast path).
    AVENV_DATA="$AVENV_GOLDEN"
  else
    # Fully isolated core/toolchain cache for this build.
    if [[ -n "$AVENV_GOLDEN" ]]; then
      _aventools_err "WARNING: AVENV_GOLDEN='$AVENV_GOLDEN' is not a directory — falling back to FULL isolation (slow: re-downloads toolchains)."
    fi
    AVENV_DATA="$AVENV_ROOT/data"
    mkdir -p "$AVENV_DATA"
  fi
  user="$AVENV_ROOT/user"
  dl="$AVENV_ROOT/downloads"
  cache="$AVENV_ROOT/cache"   # backs ~/.cache/arduino (build cache)
  mkdir -p "$user/libraries" "$dl" "$cache"

  cfg="$AVENV_ROOT/arduino-cli.yaml"
  _aventools_write_config "$cfg" "$AVENV_DATA" "$user" "$dl" "$cache"

  # Host-exported ARDUINO_* variables outrank EVERY config file (including
  # ARDUINO_CONFIG_FILE itself). Observed failure mode: a host
  # ARDUINO_BUILD_CACHE_PATH pointed all builds at one shared cache dir, so
  # concurrent builds of the same sketch (different boards) corrupted each
  # other. Drop the overrides so the per-build config file governs.
  unset ARDUINO_BUILD_CACHE_PATH ARDUINO_DIRECTORIES_DATA \
        ARDUINO_DIRECTORIES_DOWNLOADS ARDUINO_DIRECTORIES_USER

  export ARDUINO_CONFIG_FILE="$cfg"
  export XDG_CACHE_HOME="$cache"   # isolates the compile-cache (~/.cache/arduino)
  export AVENV_ROOT AVENV_DATA AVENV_USER="$user"

  # Auto-cleanup on normal exit / signals (only register once).
  trap 'aventools_cleanup' EXIT INT TERM HUP

  _aventools_err "aventools ready: root=$AVENV_ROOT data=$AVENV_DATA"
  _aventools_err "  libraries are EMPTY — undeclared deps will fail the build."
}

# aventools_prime [sketch_dir]
# Populate AVENV_GOLDEN with the platforms/libs declared by the sketch profile.
# Run this once (single-threaded) before concurrent builds use the golden cache.
aventools_prime() {
  [[ -n "$AVENV_GOLDEN" ]] || { _aventools_err "AVENV_GOLDEN is not set; nothing to prime."; return 1; }
  [[ -d "$AVENV_GOLDEN" ]] || mkdir -p "$AVENV_GOLDEN"

  local sketch="${1:-.}"
  local saved_root="$AVENV_ROOT" saved_data="$AVENV_DATA" saved_user="$AVENV_USER"
  local saved_cfg="${ARDUINO_CONFIG_FILE:-}" saved_xdg="${XDG_CACHE_HOME:-}"

  # Force a writable, isolated golden data dir for this priming step.
  local prime_root
  prime_root="$(mktemp -d "${TMPDIR:-/tmp}/acli_prime_XXXXXX")"
  AVENV_ROOT="$prime_root"
  AVENV_DATA="$AVENV_GOLDEN"
  local user="$AVENV_ROOT/user" dl="$AVENV_ROOT/downloads" cache="$AVENV_ROOT/cache" cfg="$AVENV_ROOT/arduino-cli.yaml"
  mkdir -p "$user/libraries" "$dl" "$cache"
  _aventools_write_config "$cfg" "$AVENV_DATA" "$user" "$dl" "$cache"
  export ARDUINO_CONFIG_FILE="$cfg"
  export XDG_CACHE_HOME="$cache"
  export AVENV_USER="$user"
  unset ARDUINO_BUILD_CACHE_PATH ARDUINO_DIRECTORIES_DATA \
        ARDUINO_DIRECTORIES_DOWNLOADS ARDUINO_DIRECTORIES_USER

  _aventools_err "aventools prime: fetching pinned platforms/libs into $AVENV_GOLDEN"
  # `compile` pulls the exact pinned platforms + libraries into the data dir.
  arduino-cli compile --profile "$(_aventools_default_profile "$sketch")" "$sketch" \
    || arduino-cli compile "$sketch"
  rc=$?

  # restore caller environment if there was one
  AVENV_ROOT="$saved_root"; AVENV_DATA="$saved_data"; AVENV_USER="$saved_user"
  [[ -n "$saved_cfg" ]] && export ARDUINO_CONFIG_FILE="$saved_cfg" || unset ARDUINO_CONFIG_FILE
  [[ -n "$saved_xdg" ]] && export XDG_CACHE_HOME="$saved_xdg" || unset XDG_CACHE_HOME
  rm -rf "$prime_root"
  return $rc
}

# aventools_run project -- cmd...
# One-shot wrapper for CI: init, run the command, always clean up.
aventools_run() {
  local project="$1"; shift
  [[ "$1" == "--" ]] && shift
  aventools_init "$project" || return 1
  "$@"
  local rc=$?
  aventools_cleanup
  return $rc
}

# aventools_cleanup
# Remove the per-build temp root (never touches AVENV_GOLDEN).
aventools_cleanup() {
  trap - EXIT INT TERM HUP 2>/dev/null || true
  if [[ -n "${AVENV_ROOT:-}" && "$AVENV_ROOT" != "${AVENV_GOLDEN:-}" && -d "$AVENV_ROOT" ]]; then
    rm -rf "$AVENV_ROOT"
    _aventools_err "aventools cleaned up: $AVENV_ROOT"
  fi
  AVENV_ROOT=""; AVENV_DATA=""; AVENV_USER=""
  unset ARDUINO_CONFIG_FILE XDG_CACHE_HOME 2>/dev/null || true
}

# Extract the default_profile name from a sketch.yaml (best effort).
_aventools_default_profile() {
  local sketch="$1"
  local f="$sketch/sketch.yaml"
  [[ -f "$f" ]] || { echo ""; return; }
  awk '/^default_profile:/{gsub(/.*:[[:space:]]*/,""); print; exit}' "$f"
}

# ---- main dispatcher (only when executed, not sourced) ----------------------

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  set -euo pipefail
  cmd="${1:-}"; shift || true
  case "$cmd" in
    run)  aventools_run "$@" ;;
    init) aventools_init "$@" ;;
    prime) aventools_prime "$@" ;;
    cleanup) aventools_cleanup ;;
    *)
      cat >&2 <<'USAGE'
aventools.sh - Arduino virtual build environment

Usage:
  aventools.sh run <project> -- <arduino-cli command...>   # CI one-shot
  aventools.sh init <project>                               # interactive setup
  aventools.sh prime [sketch_dir]                           # fill AVENV_GOLDEN cache
  aventools.sh cleanup                                      # remove temp root

Or source it for the aventools_init / aventools_cleanup functions.
USAGE
      exit 2
      ;;
  esac
fi
