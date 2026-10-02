#!/usr/bin/env bash
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

if [ -z "${ESP_MATTER_PATH:-}" ]; then
  mkdir -p "$here/build"
  docker build --pull --metadata-file "$here/build/toolchain-image.json" -t flipped-iot-ci "$root/ci"
  node -e '
const metadata = JSON.parse(require("fs").readFileSync(process.argv[1], "utf8"));
const base = metadata["buildx.build.provenance"].materials.find((m) => m.uri.startsWith("pkg:docker/espressif/esp-matter@"));
console.log(`image espressif/esp-matter:release-v1.5@sha256:${base.digest.sha256}`);
console.log(`image flipped-iot-ci@${metadata["containerimage.digest"]}`);
' "$here/build/toolchain-image.json" > "$here/build-log.txt"
  cpus=$(docker info --format '{{.NCPU}}')
  if [ "$cpus" -gt 8 ]; then
    cpus=8
  fi
  docker run --rm \
    --cpuset-cpus "0-$((cpus - 1))" \
    -v flipped-iot-ccache:/root/.cache/ccache \
    -v "$root":/work \
    flipped-iot-ci /work/m5stack/coreaws/build.sh "$@"
  exit 0
fi

variants=("$@")
if [ ${#variants[@]} -eq 0 ]; then
  variants=(default noenergy eve)
fi
for variant in "${variants[@]}"; do
  case $variant in
    default | noenergy | eve) ;;
    *) echo "build.sh: unknown variant $variant" >&2; exit 2 ;;
  esac
done

build() {
  local head
  head=$(git -C "$ESP_MATTER_PATH" rev-parse HEAD)
  printf 'esp-matter %s\n' "$head"
  idf.py --version
  /work/m5stack/patches/apply.sh
  cd "$here"
  export PATH="/opt/ccache-bin:$PATH"
  local started
  for variant in "${variants[@]}"; do
    started=$SECONDS
    if [ "$variant" = default ]; then
      idf.py build
      printf 'build seconds %s\n' "$((SECONDS - started))"
      idf.py merge-bin -o "$here/build/flipped_coreaws-merged.bin"
      started=$SECONDS
      "$here/test.sh"
      printf 'test seconds %s\n' "$((SECONDS - started))"
    else
      idf.py -B "$here/build-$variant" -D "SDKCONFIG=$here/build-$variant/sdkconfig" -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.$variant" build
      printf 'build-%s seconds %s\n' "$variant" "$((SECONDS - started))"
    fi
  done
  ccache --show-stats
}

build 2>&1 | tee -a "$here/build-log.txt"
