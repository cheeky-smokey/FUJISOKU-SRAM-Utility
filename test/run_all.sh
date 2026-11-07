#!/bin/bash
# すべての確認を順に行う: ホストビルド → 本物のヘッダで型チェック → ファームウェア / LCD / Web アプリのテスト
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
"$HERE/build.sh" fsu_sim -DUSE_SD=1                            # SD・LCD あり、62256 なし (テストの大半)
"$HERE/build.sh" fsu_sim_nolcd -DUSE_SD=1 -DUSE_LCD=0          # LCD のコードなし
"$HERE/build.sh" fsu_sim_62256 -DUSE_SD=1 -DUSE_SRAM62256=1    # 62256 あり
"$HERE/build.sh" fsu_sim_nosd                                  # config.h の既定 (SD なし、LCD あり)
"$HERE/build.sh" fsu_sim_nospi -DUSE_SPI_SRAM=0 -DUSE_CARD_DET=0 -DUSE_BATT_DET=0  # SPI SRAM・カード検出・電池のコードなし
"$HERE/check_real_headers.sh"
node --test --test-concurrency=1 "$HERE/tests/fw.test.js" "$HERE/tests/lcd.test.js" "$HERE/tests/web.test.js"
