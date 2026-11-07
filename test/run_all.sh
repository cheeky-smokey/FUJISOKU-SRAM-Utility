#!/bin/bash
# すべての確認を順に行う: ホストビルド → 本物のヘッダで型チェック → ファームウェア / LCD / Web アプリのテスト
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
"$HERE/build.sh"                                    # 既定 (LCD あり、62256 なし)
"$HERE/build.sh" fsu_sim_nolcd -DUSE_LCD=0          # LCD のコードなし
"$HERE/build.sh" fsu_sim_62256 -DUSE_SRAM62256=1    # 62256 あり
"$HERE/build.sh" fsu_sim_nosd -DUSE_SD=0            # SD カードのコードなし
"$HERE/check_real_headers.sh"
node --test --test-concurrency=1 "$HERE/tests/fw.test.js" "$HERE/tests/lcd.test.js" "$HERE/tests/web.test.js"
