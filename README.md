![FUJISOKU SRAM Utility](docs/images/banner.png)

# FUJISOKU SRAM Utility

> **このブランチ（`wo62256`）は v3.0 です。まだ実機では確かめていません。**
> 62256 をやめ、カードの F32〜F35（A16〜A19）を MCP23S17 ch7（U2）の GPB4〜GPB7 につなぐ設計にしました。カードは PC から 1MB まで読み書きできます。v2.8 の基板とは ch7 ポート B の配線が違います（[v2.8 からの変更](#v28-からの変更ハードウェア)）。
> 動作確認済みのリリース版 v2.8 は main ブランチと [Releases](https://github.com/cheeky-smokey/FUJISOKU-SRAM-Utility/releases) にあります。

Silver Reed 編み機のメモリカード（FUJISOKU SRAM、基本は 32KB。PC からは 1MB まで）を、Arduino UNO R4 で読み書きするツールです。MCP23S17 を 2 個使った GPIO エキスパンダでカードのアドレス・データ・制御線を直接動かし、PC・SD カード・SPI SRAM との間でコピーと照合をします。

- **PC から操作**: ブラウザの Web アプリで、カードを PC に保存・PC のファイルをカードに書き込み・比較。読み込みも書き込みも 32KB〜1MB の大きさをボタンで選べます。
- **表示**: 状態・処理の段階・進捗バー・結果を 20 文字 × 4 行で表示。同じ画面が Web アプリにも出るので、LCD はオプションです。
- **SD カード（オプション）**: 本体だけでカード ⇄ SD のバックアップ・照合・書き戻し。SD カードは処理の合間に抜き差しでき、最初から入っていなくても止まりません。
- **カードの検出と電池電圧**: カードが入っていなければ操作を止め、カードの電池電圧を LCD と Web アプリに表示します。1.70 V 以下で「少ない」（Low battery）、0.50 V 以下で「電池なし」と警告します。
- **シールド基板とスロットユニット**: Arduino UNO R4 に重ねるシールド基板（KiCad、ガーバー付き）に、U-BOX から外したスロットで作るスロットユニットを直接挿します。

![Web アプリ](docs/images/webapp.png)

![LCD の表示](docs/images/lcd-screens.png)

## 使い方

1. 本体にこのブランチのスケッチ（v3.0）を書き込み、USB で PC につなぐ。
2. PC の **Google Chrome** または **Microsoft Edge** で [`webapp/FSRAM-Utility.html`](webapp/FSRAM-Utility.html) を開き、「本体に接続」を押してシリアルポートを選ぶ。
3. 目的のタブを選ぶ。

| タブ | できること |
|---|---|
| PC に保存 | カードを読み込んで `.bin` に保存。読み込む大きさは 32KB（既定）/ 64KB / 128KB / 256KB / 512KB / 1MB のボタンで選ぶ。2 回読んで一致を確かめる設定あり |
| カードに書き込み | `.bin`（32KB〜1MB）をカードへ。書き込む大きさは 32KB / 64KB / 128KB / 256KB / 512KB / 1MB のボタンで選ぶ（既定はファイルの大きさ）。32KB より大きく書くときは「先頭 32KB だけ」か「すべて」かを警告で選ぶ。書き込む前の自動保存、変わった部分だけの書き込み、書き込み後の照合 |
| 比較 | カードとファイル、またはファイル同士を比較。違いはメモリマップと 16 進表示に赤で出る |
| SD カード | 本体の SD カードとの保存・照合・書き込み、ファイル一覧（SD を使う設定のときだけ表示） |
| SPI SRAM・診断 | SPI SRAM とのコピー・比較・消去、SPI SRAM の自己診断（SPI SRAM を使う設定のとき）、カードの消去 |

「カードに書き込み」「比較」のファイル欄は、クリックかドロップで選び直せ、右の × で外せます。ふつうのカードは 32KB です。容量の大きいカードのときだけ大きさを変えてください（32KB のカードに 32KB を超えて書くと、先頭が上書きされます）。

Web アプリの詳しい説明は [docs/webapp.md](docs/webapp.md)、シリアルコマンドは [docs/protocol.md](docs/protocol.md) にあります。従来の 1 文字コマンド（`B` `L` `V` `R` `C` `X` `S` `W` `T` `E` `e`）もそのまま使えます。

## ハードウェア

![ハードウェア接続図](docs/images/hardware-connection.svg)

### 必要なもの

| 区分 | 部品 | `config.h` |
|---|---|---|
| 必須 | Arduino UNO R4（Minima / WiFi） | - |
| 必須 | [シールド基板](#シールド基板) rev 2.0（MCP23S17 × 2 などを載せる。ユニバーサル基板で同じ結線にしてもよい） | - |
| 必須 | [スロットユニット](#スロットユニット)（U-BOX から外したスロット + 変換基板 + L 型のキー付きボックスヘッダ） | - |
| オプション | 20 × 4 キャラクタ LCD（HD44780 + PCF8574 の I2C バックパック。5V・GND・SDA = A4・SCL = A5） | `USE_LCD`（既定 1） |
| オプション | SD カードモジュール（5 V で使えるもの。/CS は D7） | `USE_SD`（既定 0） |
| オプション | SPI SRAM（23LC512。本体だけでのコピー・比較の一時置き場。/CS は D6） | `USE_SPI_SRAM`（既定 1。起動時に自動検出） |
| 配線 | カードの検出（カードの F37 /CARD DET → D2。シールド基板は配線済み） | `USE_CARD_DET`（既定 1） |
| 配線 | 電池電圧（カードの F2 BATT-DET → A2。A2 と GND の間に 1 MΩ。シールド基板は配線済み） | `USE_BATT_DET`（既定 1） |

カードの吸い出し・書き込み・比較は Web アプリでできるので、必須なのは Arduino UNO R4、シールド基板（MCP23S17 × 2）、スロットユニットだけです。電源は 5 V で、部品はすべて 5 V 系なのでレベル変換は要りません。

### シールド基板

![シールド基板（表）](hardware/kicad/shield/FSU_Shield_top.png)

| 項目 | 内容 |
|---|---|
| 大きさ | 68.58 × 53.34 mm（Arduino UNO と同じ形。右側の角が斜め） |
| 層・厚さ | 2 層、1.6 mm |
| 部品 | すべてスルーホール、IC は DIP（MCP23S17 × 2、23LC512、カードの電源のコンデンサ、BATT-DET のプルダウン、SD・LCD 用のピンヘッダ） |
| 取り付け穴 | φ3.2 mm × 4（UNO と同じ位置） |
| カードの接続 | J1 = キー付き MIL メスソケット 40P（キーは奥 = U1 側）。スロットユニットを直接挿す |
| データ | [hardware/kicad/shield/](hardware/kicad/shield/)（`.kicad_pcb`、ガーバー zip、原寸 PDF） |

部品表・注文のしかた（JLCPCB などの設定）・組み立ての注意は [hardware/kicad/README.md](hardware/kicad/README.md#シールド基板arduino-uno-r4-用) にあります。

### スロットユニット

FUJISOKU のカードスロットは製造中止なので、U-BOX から外したスロットを使います。スロットの足（ZIP 配列）を、秋月電子の 2.54 mm（2×5）⇔ 1.27 mm（1×10）変換基板 4 枚（表裏を逆にして使う）で 2 列にし、L 型のキー付きボックスヘッダ（2×20）を付けます。

- **シールドにつなぐ**: シールドの J1 に直接挿します。ボックスヘッダの切り欠きにソケットのキーが入るので、逆向きには挿さりません。
- **U-BOX につなぐ**: U-BOX の CN2 に足を ZIP 配列に曲げたボックスヘッダを付け、ストレートの 40 芯リボンケーブルでつなぎます。
- **ヘッダのピン**: 1 番 = F2、2 番 = F1（下段）、3 番 = F4、4 番 = F3 … 37 番 = F38、38 番 = F37。39・40 番は空き（奇数ピン 2k−1 = F2k、偶数ピン 2k = F2k−1）。

作り方と参考写真は [docs/hardware.md §12](docs/hardware.md#12-スロット記事より) にあります。

### カードの信号の割り当て（v3.0）

| 信号 | カードのピン | つなぐ先 |
|---|---|---|
| A0〜A15 | F16〜F31 | ch6（U1）の GPA0〜GPA7・GPB0〜GPB7 |
| D0〜D7 | F8〜F15 | ch7（U2）の GPA0〜GPA7 |
| CE2 / /OE / /CE1 / /WE | F5 / F7 / F4 / F6 | ch7（U2）の GPB0 / GPB1 / GPB2 / GPB3 |
| A16〜A19 | F32〜F35 | ch7（U2）の GPB4〜GPB7 |
| BATT-DET / /CARD DET | F2 / F37 | Arduino の A2 / D2 |
| Vcc / GND | F3・F36 / F1・F38 | 5 V / GND |

詳しい結線表・ピン配置・アクセス手順は [docs/hardware.md](docs/hardware.md) にあります。KiCad の回路図（PDF 付き）は [hardware/kicad/](hardware/kicad/) にあります。

### v2.8 からの変更（ハードウェア）

| 項目 | v2.8（main） | v3.0（このブランチ） |
|---|---|---|
| ch7 GPB0〜GPB3 | CE2（62256 の /CS と共用）、/OE、62256 の /OE、/WE | CE2、/OE、**/CE1**、/WE |
| ch7 GPB4〜GPB7 | /CE1、未使用、A16、A17 | **A16、A17、A18、A19** |
| カードの F34・F35（A18・A19） | GND に固定 | **U2 の GPB6・GPB7** |
| カードの範囲（PC から） | 256KB（A0〜A17） | **1MB（A0〜A19）** |
| 62256 | オプション（`USE_SRAM62256`） | **使わない** |
| シールド基板 | rev 1.x | **rev 2.0**（U2 の GPB の配線が違う。ガーバーも別） |

v2.8 の基板を v3.0 で使うときは、ch7 の GPB2 と GPB4〜GPB7 の配線を上の表のとおりにやり直してください。

## 設定（`config.h`）

オプション部品は `FUJISOKU_SRAM_COPY/config.h` の先頭で選びます。既定は SD カードなし、LCD と SPI SRAM はあり（どちらも見つからなければ飛ばして動く）です。

```c
#define USE_SD 0          // SD カードをつないだら 1 (SdFat ライブラリが要る)
#define USE_LCD 1         // LCD をつながないなら 0 (LiquidCrystal I2C ライブラリも不要になる)
#define USE_SPI_SRAM 1    // SPI SRAM をつながないなら 0 (1 のままでも、見つからなければ飛ばす)
#define USE_CARD_DET 1    // D2 にカードの検出を配線していないなら 0
#define USE_BATT_DET 1    // A2 にカードの電池電圧を配線していないなら 0
#define BATT_NONE_MV 500  // これ以下は「電池なし」(No battery) [mV]
#define BATT_LOW_MV 1700  // これ以下は「少ない」(Low battery) [mV]。BATT_NONE_MV より大きくする
```

`USE_LCD` と `USE_SPI_SRAM` は 1 のままでも、起動時に見つからなければその部品を使わずに動きます（SPI SRAM を使う操作は `ERR NOSPI` になり、Web アプリは自己診断だけを出します）。LCD の I2C アドレスは `LCD_I2C_ADDR`（既定 `0x27`、表示されないときは `0x3F`）です。

## ビルド

1. Arduino IDE のライブラリマネージャで、使う部品に合わせて入れる（SD と LCD を使わないなら不要）。
   - **SdFat**（Bill Greiman、v2 系）。`USE_SD` を 1 にしたときだけ必要です（既定の 0 なら不要）。
   - **LiquidCrystal I2C**（Frank de Brabander）。`USE_LCD` が 1 のときだけ必要です。「AVR 用」と警告が出ることがありますが、UNO R4 でも使えます。
2. `FUJISOKU_SRAM_COPY/FUJISOKU_SRAM_COPY.ino` を開いてボードに書き込む（同じフォルダの `.ino` と `config.h` はタブとして一緒に開かれます）。

## 構成

```
FUJISOKU_SRAM_COPY/         スケッチ (メイン、config.h、hardware / storage / operations / protocol / lcd_ui / card)
webapp/FSRAM-Utility.html   Web アプリ (1 ファイル、インストール不要)
docs/                       hardware.md (配線)、specification.md (ソフト仕様)、protocol.md (通信)、webapp.md
hardware/kicad/             KiCad の回路図 (.kicad_sch、PDF)、作り直し・確認用のスクリプト
hardware/kicad/shield/      UNO R4 用シールド基板 rev 2.0 (.kicad_pcb、ガーバー zip、原寸 PDF)
test/                       PC 上でスケッチを動かすシミュレータと自動テスト (test/README.md)
```

## 確かめたこと

スケッチをそのまま PC でビルドし、MCP23S17・カード・SPI SRAM・SD カード・LCD を再現したシミュレータで動かして、66 件の自動テスト（ファームウェア 30、表示 9、Web アプリ 27。SD・LCD・SPI SRAM・カード検出のあり・なしの構成と、1MB のカードの読み書きを含む）が通ることを確認しています。本物の SdFat と LiquidCrystal I2C のヘッダに対する型チェックも通っています。シールド基板は DRC（エラー 0、未配線 0）と、銅のつながりを [docs/hardware.md](docs/hardware.md) の結線表と照らし合わせて確かめています。変更点は [docs/specification.md §12](docs/specification.md#12-変更履歴)、テストは [test/README.md](test/README.md) にあります。

**実機（Arduino UNO R4 と作者の基板）での動作は v2.8 まで確認済みです。v3.0 の新しい割り当てとシールド基板 rev 2.0 は、まだ実機で確かめていません。**
