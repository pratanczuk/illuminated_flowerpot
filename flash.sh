#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-/dev/ttyUSB0}"
FQBN="${FQBN:-arduino:avr:pro:cpu=16MHzatmega328}"
SKETCH_DIR="${SKETCH_DIR:-$(dirname "$0")/src/arduino_pro_mini}"

"${ARDUINO_CLI:-arduino-cli}" upload -p "$PORT" --fqbn "$FQBN" "$SKETCH_DIR"
