#!/usr/bin/env bash
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

if [ -z "${ESP_MATTER_PATH:-}" ]; then
  env_args=()
  for n in 1 2 3; do
    ssid="FLIPPED_DEV_WIFI_${n}_SSID"
    pass="FLIPPED_DEV_WIFI_${n}_PASSWORD"
    if [ -n "${!ssid:-}" ]; then
      env_args+=(-e "$ssid" -e "$pass")
    fi
  done
  docker run --rm --dns 1.1.1.1 \
    ${env_args[@]+"${env_args[@]}"} \
    -v flipped-iot-dial-ccache:/root/.cache/ccache \
    -v "$root":/work \
    flipped-iot-ci /work/m5stack/dial/build.sh 2>&1 | tee "$here/build-log.txt"
  exit "${PIPESTATUS[0]}"
fi

/work/m5stack/patches/apply.sh
export PATH="/opt/ccache-bin:$PATH"
cd "$here"
idf.py build
