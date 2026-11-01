#!/bin/bash
# Guard for apps/build.sh: the Xtensa ld 2.43.1 overflowed a heap buffer when it grew a libgcc member's .eh_frame
# for -shared (64-bit division pulls in _udivdi3.o and friends), and aborted with "double free or corruption" for
# some output paths only (2026-10-31..11-02: Game Boy, Sketch, Print, FPV). This builds a sample app that uses those
# members to many output path lengths and fails on any failed build or on an ELF that still carries .eh_frame.
#   apps/test_build_paths.sh [build.sh]     (default: the build.sh beside this script)
set -u
here=$(cd "$(dirname "$0")" && pwd)
build=${1:-$here/build.sh}
tc=${XTENSA_TOOLCHAIN:-$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
app=$work/divapp
mkdir -p "$app"
cat > "$app/main.c" <<'EOF'
#include <app_api.h>
APP_INFO("Div", 0);
static volatile uint64_t seed = 1234567890123ull;
int app_main(const app_api_t *api) {
  uint64_t a = seed * api->version, b = (uint64_t)api->size + 3;
  int64_t c = (int64_t)a - 99;
  api->log("%u %u %d %d", (unsigned)(a / b), (unsigned)(a % b), (int)(c / (int64_t)b), (int)(c % (int64_t)b));
  return 0;
}
EOF
fail=0 runs=0
for n in $(seq 5 2 80); do
  d=$work/$(printf 'o%.0s' $(seq 1 "$n"))
  runs=$((runs + 1))
  if ! OUT=$d "$build" "$app" > "$work/log" 2>&1; then
    fail=$((fail + 1)); echo "path length $n: build failed: $(grep -m1 -i -e 'double free' -e 'abort' -e 'error' "$work/log")"
  elif "$tc/xtensa-esp32s3-elf-objdump" -h "$d/divapp.elf" | grep -q '\.eh_frame'; then
    fail=$((fail + 1)); echo "path length $n: the ELF still has .eh_frame (ld grows it in place: heap overflow)"
  fi
  rm -rf "$d"
done
echo "$runs builds, $fail failed"
[ "$fail" = 0 ]
