#!/usr/bin/env bash
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)

if [ -z "${M5_PORT:-}" ]; then
  shopt -s nullglob
  printf 'M5_PORT is not set. Candidate ports:\n' >&2
  for port in /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial* /dev/cu.usbmodem*; do
    printf '%s\n' "$port" >&2
  done
  exit 1
fi

cd "$here/build"
esptool --chip esp32 --port "$M5_PORT" --baud 1500000 write-flash "@flash_args"
