# テスト

実機がなくてもスケッチと Web アプリの動きを確かめるための、PC 上のシミュレータと自動テストです。

## 実行

必要なもの: Linux / macOS の `g++`（C++17）、`python3`（Pillow）、Node.js 18 以降、Playwright（Web アプリのテストのみ。`npm i -g playwright && npx playwright install chromium`）、`git`（本物のライブラリの取得用）。

```sh
test/run_all.sh                  # すべて (5 構成のビルド → 型チェック → 65 件のテスト)
test/build.sh fsu_sim -DUSE_SD=1                          # シミュレータだけビルド (SD・LCD あり。テストの大半はこれ)
test/build.sh fsu_sim_nolcd -DUSE_SD=1 -DUSE_LCD=0        # LCD のコードなし
test/build.sh fsu_sim_62256 -DUSE_SD=1 -DUSE_SRAM62256=1  # 62256 あり
test/build.sh fsu_sim_nosd                                # config.h の既定 (SD のコードなし)
test/build.sh fsu_sim_nospi -DUSE_SPI_SRAM=0              # SPI SRAM のコードなし
test/check_real_headers.sh       # 本物の SdFat / LiquidCrystal I2C のヘッダで型チェック (5 構成。使わないライブラリは渡さない)
node --test test/tests/fw.test.js   # ファームウェアのテストだけ
```

テストで撮った LCD と Web アプリの画像は `test/out/lcd/`、`test/out/web/` に保存されます。

## 仕組み

```
test/mkproto.py     Arduino IDE と同じ順で .ino をつなぎ、関数のプロトタイプを入れて 1 つの .cpp にする
test/sim/           Arduino の代わりの部品 (Arduino.h、SPI、Wire、SdFat、LiquidCrystal_I2C) とハードウェアのモデル
test/lib/fw.js      シミュレータを起動して操作する Node のラッパ
test/lib/web.js     Playwright の Chromium で Web アプリを開き、Web Serial の代わりにシミュレータへつなぐ
test/tests/         fw (ファームウェア)、lcd (LCD 表示)、web (Web アプリ) のテスト
test/realhdr/       本物のライブラリのヘッダで型チェックするための Arduino 互換ヘッダ
```

シミュレータ（`test/sim/sim.cpp`）が再現しているもの:

| 部品 | 内容 |
|---|---|
| 時計 | 仮想時計。`delay()` と SPI の 1 バイトごとに進む。`--realtime` で待機中は実時間に合わせる |
| SPI | UNO R4 と同じく `endTransaction()` は何もせず、`end()` で止まり、`beginTransaction()` が未初期化なら `begin()` する |
| MCP23S17 × 8 | IOCON の BANK / HAEN、BANK0 / BANK1 のレジスタ配置、HAEN 無効時は A2 だけで応答する癖 |
| FUJISOKU カード | ch6 / ch7 のピンから物理的な配線どおりに信号を作り、/WE の立ち上がりで書き込む SRAM。A0〜A17。容量は既定 32KB で `--card-kb 64\|128\|256` で変えられ、容量より上のアドレス線はつながっていない扱い（32KB のカードでは 0x8000 が 0x0000 と同じ場所）。/CARD DET（D2、カードがあると Low）と BATT-DET（A2、`--batt-mv` で電圧を指定、既定 3000 mV。カードが無いと 1 MΩ で 0 V）も再現。`--no-card` で抜いた状態から始める |
| 62256 | B0 = /CS、B2 = /OE、/WE 共用 |
| SPI SRAM（23LC512） | READ / WRITE / RDMR / WRMR、バイト / ページ / シーケンシャルモード（電源投入時はシーケンシャル）。/CS は D6。`--no-spi-sram` でつながっていない状態（MISO が High のまま。起動時の検出で「見つからない」になる）にできる |
| SD カード | ホストのディレクトリ。抜く・挿す・差し替え（世代が変わると古い接続は使えない）、カードが無いときの 2 秒待ち、`SD.end()` で SPI が止まること |
| LCD | HD44780（20×4）。DDRAM の行の並び、CGRAM、`createChar` の後にアドレスが CGRAM に残ること。文字列と画像（PPM）で取り出せる。`--no-lcd` でつながっていない状態（I2C が NACK）にできる |
| バスの故障 | `stuck addr\|data <ビット> <0\|1>` でアドレス線・データ線を H / L に固定できる（62256 のバス試験の確認用） |

数えている不具合の兆候（ファームウェアと LCD のテストの終わりに、0 件であることを確かめる）:

| 名前 | 意味 |
|---|---|
| `no_txn` | `beginTransaction()` の外での SPI 転送 |
| `over_clock` | MCP23S17（10 MHz）/ SPI SRAM（20 MHz）の上限を超えるクロック |
| `spi_dead` | 止まった SPI での転送 |
| `bus_conflict` / `contention` | SPI の /CS が 2 つ以上同時に Low / データバスの衝突 |
| `addr_change_in_write` / `data_change_in_write` | /WE が Low のあいだにアドレス / データ線が変わった |
| `oe_we_overlap` | /OE と /WE が同時に Low |
| `lcd_cgram_stray` | `createChar` の後に位置を決めずに文字を書いた |
| `lcd_absent_writes` | LCD が無いのに LCD に送ろうとした |

環境変数 `FSU_SIM_TRACE=1` を付けると、MCP23S17 のレジスタアクセスを 1 回ずつ標準エラーに出します（`docs/hardware.md` §8 の表はこれで記録）。

制御チャンネル（fd 3 にコマンド、fd 4 に応答）で、SD の抜き差し（`sd remove` / `sd insert` / `sd swap`）、カードの中身の読み書き（`card load` / `card dump` / `poke`）、カードの抜き差し（`cardpresent 0` / `cardpresent 1`）、電池電圧の変更（`batt 900`）、読み値を 1 回化けさせる（`glitch`）、PC からのバイトの注入（`inject 21` = `!`）、LCD の取り出し（`lcd` / `snap`）、決まった出力行が出たときに操作する（`trigger P>=16384 => sd remove`）などができます。

## 限界

シミュレータはデータシートと記事から作ったモデルです。電気的なタイミング、電源、レベル変換、実際の SD カードや LCD の個体差は再現していません。実機での動作は Arduino UNO R4 と作者の基板で v2.6 まで確認済みです。スケッチを変えたときは、実機でも確かめてください。
