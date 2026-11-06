# FUJISOKU SRAM Utility

シルバー編機のメモリカード（FUJISOKU SRAM）を、Arduino UNO R4 から SPI SRAM / SD カードとの間でコピー・照合するユーティリティです。MCP23S17 を 2 個使った GPIO エキスパンダ経由でカードのアドレス/データ/制御線を直接駆動します。

## できること

| キー | 動作 |
|---|---|
| `B` | FUJISOKU SRAM → SD（`fujisoku.bin`）にバックアップ |
| `L` | SD（`fujisoku.bin`）→ FUJISOKU SRAM に書き込み |
| `V` | SD と FUJISOKU SRAM を全 32 KB 照合し、`VERIFY OK` / `NG` を表示 |
| `R` / `C` | FUJISOKU SRAM ⇄ SPI SRAM |
| `S` / `W` | SPI SRAM ⇄ SD |
| `X` | SPI SRAM と FUJISOKU SRAM を比較 |
| `T` | SPI SRAM 自己診断（/CS 配線の判定にも使えます） |

シリアルコンソール 115200 bps で 1 文字送信します。詳細は [docs/specification.md](docs/specification.md) を参照してください。

> `L` と `C` はカードの内容を上書きします。事前に `B` でバックアップを取ってください。

## ハードウェア

![ハードウェア接続図](docs/images/hardware-connection.svg)

![MCP23S17 とカードの結線図](docs/images/fujisoku-mcp-wiring.svg)

結線とピン配置は次の記事に基づきます（記事の本文から読み取った値なので、原文と照合してください）。

- MCP23S17 とカードの結線: <https://note.com/cheekysmokey/n/nef1ee2754b83>
- FUJISOKU カードのピン配置: <https://note.com/cheekysmokey/n/n0525462640a3>

接続図、信号表、F1〜F38 のピン配置は [docs/hardware.md](docs/hardware.md) にあります。

> **要注意**: ch7 ポート B の制御線（/CE1、CE2、/WE、/OE）の割り当てが、記事とスケッチで一致していません。どちらが実機の配線かを確認してください。詳細は [hardware.md §6](docs/hardware.md#6-ch7-ポート-b-記事とスケッチの食い違い) を参照してください。

## ビルド

1. Arduino IDE に次のライブラリを入れる：Adafruit GFX Library、Adafruit ILI9341、XPT2046_Touchscreen、SdFat（v2 系）。
2. `FUJISOKU_SRAM_COPY/FUJISOKU_SRAM_COPY.ino` を開く（UNO R4 でのビルドは未検証）。
3. SD カードを挿入して書き込む。起動時に SD が初期化できないと停止します。

## 構成

```
FUJISOKU_SRAM_COPY/FUJISOKU_SRAM_COPY.ino   スケッチ本体
docs/hardware.md                            ハードウェア仕様・結線・ピン配置
docs/specification.md                       ソフトウェア詳細仕様（定義・起動・コマンド・関数）
docs/images/hardware-connection.svg         全体接続図
docs/images/fujisoku-mcp-wiring.svg         MCP23S17 とカードの結線図
```
