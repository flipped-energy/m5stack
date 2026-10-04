#!/usr/bin/env bash
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
for patch in "$here"/esp-matter/*.patch; do
  name=$(basename "$patch")
  if git -C "$ESP_MATTER_PATH" apply --reverse --check "$patch" 2>/dev/null; then
    printf 'esp-matter %s already applied\n' "$name"
    continue
  fi
  git -C "$ESP_MATTER_PATH" apply "$patch"
  target=""
  while IFS= read -r line; do
    case "$line" in
      "+++ b/"*)
        target=${line#+++ b/}
        break
        ;;
    esac
  done < "$patch"
  touch -r "$patch" "$ESP_MATTER_PATH/$target"
  printf 'esp-matter %s applied to %s\n' "$name" "$target"
done
