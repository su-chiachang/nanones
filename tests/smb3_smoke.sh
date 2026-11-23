#!/bin/sh
# Smoke test with a user-supplied SMB3 (USA) ROM; the ROM is never stored in the repo.
# usage: SMB3_ROM=/path/to/smb3.nes tests/smb3_smoke.sh   (skipped, rc 77, if unset)
cd "$(dirname "$0")/.." || exit 2
[ -n "$SMB3_ROM" ] && [ -f "$SMB3_ROM" ] || { echo "SMB3_ROM not set: skipped"; exit 77; }
out=$(mktemp -d)
# title -> world map -> World 1-1 -> run right with jumps
KEYS="420:08,430:00,600:08,610:00,900:80,910:00,1000:10,1010:00,1100:01,1110:00,1500:82,1520:83,1560:82,1580:83"
./build/nes-emu "$SMB3_ROM" --headless 1640 "$out/f.ppm" "$KEYS" || exit 1
python3 -I - "$out/f.ppm" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read().split(b'\n', 3)[3]
px = lambda x, y: d[(y * 256 + x) * 3:(y * 256 + x) * 3 + 3]
colors = {px(x, y) for y in range(0, 240, 2) for x in range(0, 256, 2)}
# status bar (y>=196) must contain white text/panel and the playfield several colours
bar = {px(x, y) for y in range(200, 224) for x in range(16, 160)}
ok = len(colors) >= 8 and b'\xec\xee\xec' in bar
print("distinct colours:", len(colors), "| status bar has white:", b'\xec\xee\xec' in bar)
sys.exit(0 if ok else 1)
PY
rc=$?; rm -rf "$out"; exit $rc
