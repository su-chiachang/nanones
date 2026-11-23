#!/bin/sh
# Runs every blargg ROM under tests/roms and prints a pass/fail table.
# usage: tests/run_blargg.sh [dir-substring]
cd "$(dirname "$0")/.." || exit 2
pass=0; fail=0
for rom in $(find tests/roms -name '*.nes' | sort | grep -v sprite_hit_tests | grep "${1:-.}"); do
  out=$(./build/blargg_runner "$rom" 2400 2>&1); rc=$?
  if [ $rc -eq 0 ]; then pass=$((pass+1)); printf 'PASS %s\n' "$rom"
  else fail=$((fail+1)); printf 'FAIL %s (rc=%d) %s\n' "$rom" "$rc" "$(printf '%s' "$out" | tr '\n' ' ' | cut -c1-110)"; fi
done
echo "pass=$pass fail=$fail"
