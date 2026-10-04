#!/usr/bin/env bash
set -euo pipefail

board="$1"
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

if [ -z "${ESP_MATTER_PATH:-}" ]; then
  docker build --pull -t flipped-iot-ci "$root/ci"
  docker run --rm -v "$root":/work flipped-iot-ci /work/m5stack/common/test.sh "$board"
  exit 0
fi

node --experimental-strip-types --no-warnings=ExperimentalWarning "$here/test/check-common.ts"
cmake -S "$here/test/host" -B "$here/build-host" -G Ninja \
  -DARDUINOJSON_DIR="$root/m5stack/$board/managed_components/bblanchon__arduinojson"
cmake --build "$here/build-host"
"$here/build-host/core_tests" "$root/m5stack/$board/test/vectors" "$root/m5stack/$board/test/sequences"
"$here/build-host/provision_tests" "$here/test/web/token_vector.json"
"$here/build-host/eve_history_tests" "$root/m5stack/$board/test/eve-history"
"$here/build-host/ui_core_tests"
