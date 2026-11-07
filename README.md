![FUJISOKU SRAM Utility](docs/images/banner.png)

# FUJISOKU SRAM Utility

**最新版 v2.7**（電池なしの検出を追加。v2.6 でカードの検出と電池電圧の表示を追加）。Arduino UNO R4 と作者の基板で動作確認済みです。ダウンロードは [Releases](https://github.com/cheeky-smokey/FUJISOKU-SRAM-Utility/releases) から。

Silver Reed 編み機のメモリカード（FUJISOKU SRAM、基本は 32KB。PC からは 256KB まで）を、Arduino UNO R4 で読み書きするツールです。MCP23S17 を 2 個使った GPIO エキスパンダでカードのアドレス・データ・制御線を直接動かし、PC・SD カード・SPI SRAM との間でコピーと照合をします。

- **PC から操作**: ブラウザの Web アプリで、カードを PC に保存・PC のファイルをカードに書き込み・比較。
- **表示**: 状態・処理の段階・進捗バー・結果を 20 文字 × 4 行で表示。同じ画面が Web アプリにも出るので、LCD はオプションです。
- **SD カード（オプション）**: 本体だけでカード ⇄ SD のバックアップ・照合・書き戻し。SD カードは処理の合間に抜き差しでき、最初から入っていなくても止まりません。
- **カードの検出と電池電圧**: カードが入っていなければ操作を止め、カードの電池電圧を LCD と Web アプリに表示します。2.00 V 以下で「少ない」（Low battery）、0.40 V 以下で「電池なし」と警告します。
- **62256（オプション）**: 動作確認用の SRAM で、カードを使わずにバスの配線を試験できます。

![Web アプリ](docs/images/webapp.png)

![LCD の表示](docs/images/lcd-screens.png)

## 使い方

1. 本体をスケッチ v2 で動かし、USB で PC につなぐ。
2. PC の **Google Chrome** または **Microsoft Edge** で [`webapp/FSRAM-Utility.html`](webapp/FSRAM-Utility.html) を開き、「本体に接続」を押してシリアルポートを選ぶ。
3. 目的のタブを選ぶ。

| タブ | できること |
|---|---|
| PC に保存 | カードを読み込んで `.bin` に保存。読み込む大きさは 32KB（既定）/ 64KB / 128KB / 256KB のボタンで選ぶ。2 回読んで一致を確かめる設定あり |
| カードに書き込み | `.bin`（32KB〜256KB）をカードへ。32KB より大きいファイルは「先頭 32KB だけ」か「すべて」かを警告で選ぶ。書き込む前の自動保存、変わった部分だけの書き込み、書き込み後の照合 |
| 比較 | カードとファイル、またはファイル同士を比較。違いはメモリマップと 16 進表示に赤で出る |
| SD カード | 本体の SD カードとの保存・照合・書き込み、ファイル一覧（SD を使う設定のときだけ表示） |
| SPI SRAM・診断 | SPI SRAM とのコピー・比較・消去、SPI SRAM の自己診断（SPI SRAM を使う設定のとき）、カードの消去。62256 を使う設定なら、62256 でのバス試験など |

「カードに書き込み」「比較」のファイル欄は、クリックかドロップで選び直せ、右の × で外せます。

Web アプリの詳しい説明は [docs/webapp.md](docs/webapp.md)、シリアルコマンドは [docs/protocol.md](docs/protocol.md) にあります。従来の 1 文字コマンド（`B` `L` `V` `R` `C` `X` `S` `W` `T` `E` `e`）もそのまま使えます。

## 必要なもの

| 区分 | 部品 | `config.h` |
|---|---|---|
| 必須 | Arduino UNO R4、MCP23S17 × 2、FUJISOKU カードのスロット | - |
| オプション | 20 × 4 キャラクタ LCD（HD44780 + PCF8574 の I2C バックパック。5V・GND・SDA = A4・SCL = A5） | `USE_LCD`（既定 1） |
| オプション | SD カードモジュール（/CS は D7） | `USE_SD`（既定 0） |
| オプション | SPI SRAM（23LC512。本体だけでのコピー・比較の一時置き場。/CS は D6） | `USE_SPI_SRAM`（既定 1。起動時に自動検出） |
| オプション | 62256 並列 SRAM（動作確認用。カードと同じバスにつなぐ） | `USE_SRAM62256`（既定 0） |
| 配線 | カードの検出（カードの F37 /CARD DET → D2） | `USE_CARD_DET`（既定 1） |
| 配線 | 電池電圧（カードの F2 BATT-DET → A2。A2 と GND の間に 1 MΩ） | `USE_BATT_DET`（既定 1） |

カードの吸い出し・書き込み・比較は Web アプリでできるので、必須なのは Arduino UNO R4、MCP23S17 × 2、スロットだけです。既定は SD カードなし、LCD と SPI SRAM はあり（どちらも見つからなければ飛ばして動く）、62256 なしです。オプション部品は `FUJISOKU_SRAM_COPY/config.h` の先頭で選びます。

```c
#define USE_SD 0          // SD カードをつないだら 1 (SdFat ライブラリが要る)
#define USE_LCD 1         // LCD をつながないなら 0 (LiquidCrystal I2C ライブラリも不要になる)
#define USE_SPI_SRAM 1    // SPI SRAM をつながないなら 0 (1 のままでも、見つからなければ飛ばす)
#define USE_SRAM62256 0   // 62256 をつないだら 1
#define USE_CARD_DET 1    // D2 にカードの検出を配線していないなら 0
#define USE_BATT_DET 1    // A2 にカードの電池電圧を配線していないなら 0
```

`USE_LCD` と `USE_SPI_SRAM` は 1 のままでも、起動時に見つからなければその部品を使わずに動きます（SPI SRAM を使う操作は `ERR NOSPI` になり、Web アプリは自己診断だけを出します）。LCD の I2C アドレスは `LCD_I2C_ADDR`（既定 `0x27`、表示されないときは `0x3F`）です。

電源は 5 V で、部品はすべて 5 V 系なのでレベル変換は要りません。

接続図・信号表・ピン配置は [docs/hardware.md](docs/hardware.md) を参照してください。メモリカードスロットの作り方と参考写真（秋月電子の 2.54 mm ⇔ 1.27 mm 変換基板を使ったもの）は、同じ文書の [§12](docs/hardware.md#12-スロット記事より) にあります。

![ハードウェア接続図](docs/images/hardware-connection.svg)

## ビルド

1. Arduino IDE のライブラリマネージャで、使う部品に合わせて入れる（SD と LCD を使わないなら不要）。
   - **SdFat**（Bill Greiman、v2 系）。`USE_SD` を 1 にしたときだけ必要です（既定の 0 なら不要）。
   - **LiquidCrystal I2C**（Frank de Brabander）。`USE_LCD` が 1 のときだけ必要です。「AVR 用」と警告が出ることがありますが、UNO R4 でも使えます。
2. `FUJISOKU_SRAM_COPY/FUJISOKU_SRAM_COPY.ino` を開いてボードに書き込む（同じフォルダの `.ino` と `config.h` はタブとして一緒に開かれます）。

v1 で使っていた Adafruit GFX / ILI9341 / XPT2046 は不要になりました。

## 構成

```
FUJISOKU_SRAM_COPY/         スケッチ (メイン、config.h、hardware / storage / operations / protocol / lcd_ui)
webapp/FSRAM-Utility.html   Web アプリ (1 ファイル、インストール不要)
docs/                       hardware.md (配線)、specification.md (ソフト仕様)、protocol.md (通信)、webapp.md
test/                       PC 上でスケッチを動かすシミュレータと自動テスト (test/README.md)
```

## 確かめたこと

スケッチをそのまま PC でビルドし、MCP23S17・カード・SPI SRAM・SD カード・LCD を再現したシミュレータで動かして、64 件の自動テスト（ファームウェア 31、表示 9、Web アプリ 24。SD・LCD・SPI SRAM・62256・カード検出のあり・なしの構成を含む）が通ることを確認しています。変更点は [docs/specification.md §12](docs/specification.md#12-変更履歴) にあります。本物の SdFat と LiquidCrystal I2C のヘッダに対する型チェックも通っています。**実機（Arduino UNO R4 と作者の基板）での動作も v2.6 まで確認済みです。** 詳しくは [test/README.md](test/README.md) を参照してください。
