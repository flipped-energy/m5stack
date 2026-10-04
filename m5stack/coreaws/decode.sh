#!/usr/bin/env bash
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

read -r -a words <<< "$*"
addresses=()
for word in "${words[@]}"; do
  address=${word%%:*}
  if [[ $address == 0x* ]]; then
    addresses+=("$address")
  fi
done
if [ "${#addresses[@]}" -eq 0 ]; then
  printf 'usage: ./decode.sh <backtrace line or addresses>, for example ./decode.sh Backtrace: 0x400d1234:0x3ffb1230 0x40081a2b:0x3ffb1250\n' >&2
  exit 1
fi

docker run --rm -v "$root":/work flipped-iot-ci \
  xtensa-esp32-elf-addr2line -pfiaC -e /work/m5stack/coreaws/build/flipped_coreaws.elf "${addresses[@]}"
