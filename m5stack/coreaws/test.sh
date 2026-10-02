#!/usr/bin/env bash
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

if [ -z "${ESP_MATTER_PATH:-}" ]; then
  docker build --pull -t flipped-iot-ci "$root/ci"
  docker run --rm -v "$root":/work flipped-iot-ci /work/m5stack/coreaws/test.sh
  exit 0
fi

if [ -d /work/spec ]; then
  node --experimental-strip-types --no-warnings=ExperimentalWarning /work/spec/verify-vectors.ts
fi
if [ ! -d "$here/managed_components/bblanchon__arduinojson" ]; then
  idf.py -C "$here" reconfigure
fi
"$here/../common/test.sh" coreaws
node --experimental-strip-types --no-warnings=ExperimentalWarning --test "$here/../common/test/web/token_page.test.ts"
