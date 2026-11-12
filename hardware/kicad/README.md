# 回路図（KiCad）

FUJISOKU SRAM Utility の回路図です。[docs/hardware.md](../../docs/hardware.md) の結線表と `FUJISOKU_SRAM_COPY/config.h` のピン定義（v2.8）から作りました。

![回路図](../../docs/images/schematic.png)

| ファイル | 内容 |
|---|---|
| `FUJISOKU_SRAM_Utility.kicad_pro` / `.kicad_sch` | KiCad のプロジェクトと回路図（KiCad 7 で作成。8 / 9 でも開ける） |
| `FUJISOKU_SRAM_Utility.pdf` | 印刷・閲覧用の PDF（A3） |
| `gen_schematic.py` | 回路図を作るスクリプト。結線を変えるときはここを直して作り直す |
| `check_netlist.py` | 回路図の結線が仕様書どおりかを確かめるスクリプト |

## 載っている部品

| 部品番号 | 部品 | 備考 |
|---|---|---|
| A1 | Arduino UNO R4 | KiCad の UNO R3 の形（ピン配置は同じ） |
| U1 | MCP23S17 ch6 | アドレス A0〜A15。A2:A0 = 110 |
| U2 | MCP23S17 ch7 | データ D0〜D7、制御線、A16/A17。A2:A0 = 111 |
| J1 | FUJISOKU カードのスロット（F1〜F38） | ピン番号 n = カードのピン Fn |
| R1 | 1 MΩ | BATT-DET（A2）のプルダウン |
| U3 | 23LC512（オプション） | SPI SRAM、/CS = D6 |
| U4 | 62256（オプション） | 動作確認用の SRAM、/CS = CE2（GPB0） |
| J2 | SD カードモジュール（オプション） | 5 V 用。/CS = D7。ピンの並びはモジュールによって違う |
| J3 | 20×4 LCD + PCF8574（オプション） | I2C、アドレス 0x27 |
| C1〜C4 | 0.1 µF | 各 IC の近くに置くパスコン（推奨。仕様書には無い部品） |

- 部品どうしは、ピンに付けた名前（ネットラベル）でつないでいます。同じ名前のラベルがつながっています。
- 電源は Arduino の 5V ピンから取る 5 V だけです。部品はすべて 5 V 系で、レベル変換はありません。
- 基板のレイアウト（PCB）とフットプリントは決めていません。

## 作り直す・確かめる

KiCad 7 以降が入った PC で次を実行します。

```sh
python3 gen_schematic.py                       # 回路図を作り直す (KiCad の標準シンボルライブラリを使う)
kicad-cli sch export pdf -o FUJISOKU_SRAM_Utility.pdf FUJISOKU_SRAM_Utility.kicad_sch
kicad-cli sch export netlist --format kicadsexpr -o /tmp/fsu.net FUJISOKU_SRAM_Utility.kicad_sch
python3 check_netlist.py /tmp/fsu.net          # 結線を確かめる (OK と出れば仕様書どおり)
```

`check_netlist.py` は、カードのピン・MCP23S17・Arduino・SPI の各部品・62256 のつながりと、+5V・GND、未接続のピン（MCP の INT、ch7 GPB5、カードの A18/A19 など）を、仕様書の表と照らし合わせます。
