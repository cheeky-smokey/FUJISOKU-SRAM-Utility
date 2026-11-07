#!/bin/bash
# スケッチをホストでビルドする (Arduino IDE と同じ連結 + プロトタイプ生成、-Wall -Wextra)
#   使い方: test/build.sh [出力名] [g++ に渡す追加オプション...]
#   例    : test/build.sh fsu_sim -DUSE_SD=1   (オプションの既定は config.h。USE_SD の既定は 0)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
OUT="$HERE/out"
mkdir -p "$OUT"
NAME="${1:-fsu_sim}"
shift || true
python3 "$HERE/mkproto.py" "$ROOT/FUJISOKU_SRAM_COPY" > "$OUT/$NAME.cpp"
FLAGS=(-std=gnu++17 -O1 -g -DARDUINO=10819 -I "$HERE/sim" -I "$ROOT/FUJISOKU_SRAM_COPY")
g++ "${FLAGS[@]}" -Wall -Wextra -Wno-unused-parameter "$@" -c "$OUT/$NAME.cpp" -o "$OUT/$NAME.sketch.o"
g++ "${FLAGS[@]}" -Wall -Wextra -Wno-unused-parameter -Wno-unused-function "$@" -c "$HERE/sim/sim.cpp" -o "$OUT/$NAME.sim.o"
g++ "$OUT/$NAME.sketch.o" "$OUT/$NAME.sim.o" -o "$OUT/$NAME"
echo "built $OUT/$NAME"
