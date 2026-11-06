# FUJISOKU SRAM Utility

シルバー編機のメモリカード（FUJISOKU SRAM）を、Arduino 互換ボードから SPI SRAM / SD カードとの間でコピー・照合するユーティリティです。MCP23S17 を 1個使った GPIO エキスパンダ経由でカードのアドレス/データバスを直接制御します。

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

接続図と信号表は [docs/hardware.md](docs/hardware.md) にあります。これらは **スケッチのコードから読み取った構成**で、基板の回路図ではありません。要確認事項は [hardware.md §9](docs/hardware.md#9-要確認事項) を参照してください。

## ビルド

1. Arduino IDE に次のライブラリを入れる：Adafruit GFX Library、Adafruit ILI9341、XPT2046_Touchscreen、SdFat（v2 系）。
2. `FUJISOKU_SRAM_COPY/FUJISOKU_SRAM_COPY.ino` を開く（32 ビットボードが前提）。
3. SD カードを挿入して書き込む。起動時に SD が初期化できないと停止します。

## 構成

```
FUJISOKU_SRAM_COPY/FUJISOKU_SRAM_COPY.ino   スケッチ本体
docs/hardware.md                            ハードウェア仕様・信号表
docs/specification.md                       ソフトウェア仕様（コマンド・関数）
docs/images/hardware-connection.svg         接続図
```
