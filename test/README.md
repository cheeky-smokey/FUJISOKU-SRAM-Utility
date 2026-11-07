# テスト

実機がなくてもスケッチと Web アプリの動きを確かめるための、PC 上のシミュレータと自動テストです。

## 実行

必要なもの: Linux / macOS の `g++`（C++17）、`python3`（Pillow）、Node.js 18 以降、Playwright（Web アプリのテストのみ。`npm i -g playwright && npx playwright install chromium`）、`git`（本物のライブラリの取得用）。

```sh
test/run_all.sh                  # すべて (4 構成のビルド → 型チェック → 53 件のテスト)
test/build.sh                    # シミュレータだけビルド (test/out/fsu_sim)
test/build.sh fsu_sim_nolcd -DUSE_LCD=0          # LCD のコードなし
test/build.sh fsu_sim_62256 -DUSE_SRAM62256=1    # 62256 あり
test/build.sh fsu_sim_nosd -DUSE_SD=0            # SD カードのコードなし
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
| FUJISOKU カード | ch6 / ch7 のピンから物理的な配線どおりに信号を作り、/WE の立ち上がりで書き込む SRAM |
| 62256 | B0 = /CS、B2 = /OE、/WE 共用 |
| SPI SRAM（23LC512） | READ / WRITE / RDMR / WRMR、バイト / ページ / シーケンシャルモード。/CS は D6 と MCP B5 のどちらか、または両方（`--spi-cs d6\|mcp\|both`） |
| SD カード | ホストのディレクトリ。抜く・挿す・差し替え（世代が変わると古い接続は使えない）、カードが無いときの 2 秒待ち、`SD.end()` で SPI が止まること |
| LCD | HD44780（20×4）。DDRAM の行の並び、CGRAM、`createChar` の後にアドレスが CGRAM に残ること。文字列と画像（PPM）で取り出せる。`--no-lcd` でつながっていない状態（I2C が NACK）にできる |
| バスの故障 | `stuck addr\|data <ビット> <0\|1>` でアドレス線・データ線を H / L に固定できる（62256 のバス試験の確認用） |

数えている不具合の兆候（ファームウェアと LCD のテストの終わりに、0 件であることを確かめる）:

| 名前 | 意味 |
|---|---|
| `no_txn` | `beginTransaction()` の外での SPI 転送 |
| `over_clock` | MCP23S17（10 MHz）/ SPI SRAM（20 MHz）の上限を超えるクロック |
| `spi_dead` | 止まった SPI での転送 |
| `bus_conflict` / `contention` | 2 つのデバイスの同時選択 / データバスの衝突 |
| `addr_change_in_write` / `oe_we_overlap` | 書き込み中のアドレス変更 / /OE と /WE が同時に Low |
| `sram_junk` | MCP への転送が SPI SRAM に書き込まれた |
| `lcd_cgram_stray` | `createChar` の後に位置を決めずに文字を書いた |
| `lcd_absent_writes` | LCD が無いのに LCD に送ろうとした |

制御チャンネル（fd 3 にコマンド、fd 4 に応答）で、SD の抜き差し（`sd remove` / `sd insert` / `sd swap`）、カードの中身の読み書き（`card load` / `card dump` / `poke`）、読み値を 1 回化けさせる（`glitch`）、PC からのバイトの注入（`inject 21` = `!`）、LCD の取り出し（`lcd` / `snap`）、決まった出力行が出たときに操作する（`trigger P>=16384 => sd remove`）などができます。

## 限界

シミュレータはデータシートと記事から作ったモデルです。電気的なタイミング、電源、レベル変換、実際の SD カードや LCD の個体差は再現していません。実機での確認が必要です。
