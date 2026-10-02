#!/usr/bin/env bash
set -euo pipefail

uv tool run --from pyserial pyserial-miniterm "$M5_PORT" 115200
