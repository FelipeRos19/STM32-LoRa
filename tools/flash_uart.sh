#!/bin/sh
# Interactive UART flash helper for STM32_Programmer_CLI.
# - Prompts for the serial port (with a configurable default), unless prompt=OFF
# - Kills processes holding serial devices (optional)
# - Flashes a provided .bin at 0x08000000

set -eu

BIN_PATH=${1:?"Usage: flash_uart.sh <bin_path> [baud] [default_port] [kill_all] [prompt]"}
BAUD=${2:-115200}
DEFAULT_PORT=${3:-/dev/ttyUSB0}
KILL_ALL_RAW=${4:-OFF}
PROMPT_RAW=${5:-ON}

# Normalize boolean flags (accepts: 1/0, ON/OFF, TRUE/FALSE, YES/NO)
is_true() {
  case "$1" in
    1|ON|on|YES|yes|TRUE|true) return 0 ;;
    *) return 1 ;;
  esac
}
if is_true "$KILL_ALL_RAW"; then KILL_ALL=1; else KILL_ALL=0; fi

if [ ! -f "$BIN_PATH" ]; then
  echo "ERROR: Binary not found: $BIN_PATH" >&2
  exit 1
fi

echo "Available serial ports (if any):"
if ls -1 /dev/ttyUSB* /dev/ttyACM* 2>/dev/null; then
  :
else
  echo "  (none found under /dev/ttyUSB* or /dev/ttyACM*)"
fi

PORT=""
if is_true "$PROMPT_RAW"; then
  echo ""
  printf "Enter serial port [%s]: " "$DEFAULT_PORT"
  read PORT || PORT=""
fi
if [ -z "${PORT}" ]; then
  PORT="$DEFAULT_PORT"
fi

if [ ! -e "$PORT" ]; then
  echo "ERROR: Port does not exist: $PORT" >&2
  exit 1
fi

kill_dev_users() {
  DEV="$1"

  if command -v fuser >/dev/null 2>&1; then
    # fuser is provided by psmisc on many distros
    fuser -k "$DEV" >/dev/null 2>&1 || true
    return 0
  fi

  if command -v lsof >/dev/null 2>&1; then
    PIDS=$(lsof -t "$DEV" 2>/dev/null || true)
    if [ -n "$PIDS" ]; then
      # shellcheck disable=SC2086
      kill $PIDS >/dev/null 2>&1 || true
    fi
    return 0
  fi

  echo "NOTE: Neither 'fuser' nor 'lsof' found; cannot kill processes holding serial ports." >&2
  return 0
}

if [ "$KILL_ALL" -eq 1 ]; then
  echo "Killing processes using any /dev/ttyUSB* or /dev/ttyACM* (best-effort)..."
  for DEV in /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$DEV" ] || continue
    kill_dev_users "$DEV"
  done
else
  echo "Killing processes using $PORT (best-effort)..."
  kill_dev_users "$PORT"
fi

echo ""
echo "Flashing: $BIN_PATH"
echo "Port:     $PORT"
echo "Baud:     $BAUD"
echo ""

exec STM32_Programmer_CLI \
  --connect port="$PORT" br="$BAUD" \
  --write "$BIN_PATH" 0x08000000 \
  --verify \
  --go 0x08000000
