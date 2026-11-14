#!/bin/bash
# 本物のライブラリ (SdFat, LiquidCrystal_I2C) のヘッダに対してスケッチを型チェックする。
# ライブラリの使い方 (関数名・引数の型) が実物と合っているかを確かめる。実機用のコンパイルの代わり。
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
DEPS="$HERE/.deps"
mkdir -p "$DEPS" "$HERE/out"
[ -d "$DEPS/SdFat" ] || git clone --depth 1 https://github.com/greiman/SdFat.git "$DEPS/SdFat"
[ -d "$DEPS/LiquidCrystal_I2C" ] || git clone --depth 1 https://github.com/johnrickman/LiquidCrystal_I2C.git "$DEPS/LiquidCrystal_I2C"
python3 "$HERE/mkproto.py" "$ROOT/FUJISOKU_SRAM_COPY" > "$HERE/out/realhdr_sketch.cpp"
check() {  # $1 = 説明、残り = g++ の追加オプション (ライブラリのパスもここで渡す)
  local label="$1"; shift
  g++ -std=gnu++17 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -DARDUINO=10819 \
    -I "$HERE/realhdr" -I "$ROOT/FUJISOKU_SRAM_COPY" "$@" \
    "$HERE/out/realhdr_sketch.cpp" 2>&1 | grep -v 'redefined\|FsApiConstants.h' || true
  g++ -std=gnu++17 -fsyntax-only -w -DARDUINO=10819 -I "$HERE/realhdr" -I "$ROOT/FUJISOKU_SRAM_COPY" \
    "$@" "$HERE/out/realhdr_sketch.cpp"
  echo "OK: $label"
}
SDFAT=(-isystem "$DEPS/SdFat/src")
LCDLIB=(-isystem "$DEPS/LiquidCrystal_I2C")
# 使わない部品のライブラリは渡さずに確かめる (ライブラリが無くてもビルドできること)
check "既定 (SD なし、LCD あり。SdFat なし)" "${LCDLIB[@]}"
check "SD・LCD あり" "${SDFAT[@]}" "${LCDLIB[@]}" -DUSE_SD=1
check "SD あり、LCD なし (LiquidCrystal I2C なし)" "${SDFAT[@]}" -DUSE_SD=1 -DUSE_LCD=0
check "最小構成 (SD・LCD・SPI SRAM・カード検出・電池なし。外部ライブラリなし)" -DUSE_LCD=0 -DUSE_SPI_SRAM=0 -DUSE_CARD_DET=0 -DUSE_BATT_DET=0
