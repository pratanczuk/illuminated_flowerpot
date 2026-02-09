#!/usr/bin/env bash
set -euo pipefail

FQBN="${FQBN:-arduino:avr:pro:cpu=16MHzatmega328}"
SKETCH_DIR="${SKETCH_DIR:-$(dirname "$0")/src/arduino_pro_mini}"

"${ARDUINO_CLI:-arduino-cli}" compile --fqbn "$FQBN" "$SKETCH_DIR"
