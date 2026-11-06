# ハードウェア仕様 / 接続

> この資料は `FUJISOKU_SRAM_COPY/FUJISOKU_SRAM_COPY.ino` の `#define` とアクセス手順から読み取った構成です。**基板の回路図ではありません。** コードから確定できない項目は「要確認」としています（§9）。

## 1. 全体接続図

![ハードウェア接続図](images/hardware-connection.svg)

- Arduino 互換の 32 ビットボードが SPI マスタとして、TFT / タッチ / SD / SPI SRAM / MCP23S17 を同一 SPI バスで制御します。
- FUJISOKU メモリカード（シルバー編機のメモリパック）と 62256 並列 SRAM は、直接 MCU のピンにはつながらず、**MCP23S17 を 2 個（ch6 = アドレス、ch7 = データ+制御）** 使った GPIO エキスパンダ経由でアクセスします。

## 2. Arduino ピン割り当て

| ピン | 定義名 | 接続先 | 備考 |
|---|---|---|---|
| SCK / MOSI / MISO | - | 全 SPI デバイス共通 | `SPI.begin()`、SPISettings 指定なし |
| D10 | `CS_TFT` | ILI9341 /CS | `tft.begin()` のみ使用 |
| D9 | `DC_TFT` | ILI9341 DC | |
| D8 | `CS_touch` | XPT2046 /CS | |
| D7 | `CS_SDcard` | SD カード /CS | `SD.begin(CS_SDcard)` |
| D6 | `CS_SRAM_512` | SPI SRAM /CS | 既定の経路（§7 参照） |
| D5 | `CS_RESET` | nRESET | setup で Low→High のパルス。MCP23S17 の /RESET と思われる（要確認） |
| D4 | `CS_MCP23S17` | MCP23S17 /CS（8 個共通） | |
| D3 | `IRQ_MCP` | MCP23S17 INT（想定） | `pinMode(INPUT)` のみ。割り込みは未使用 |
| D2 | `IRQ_touch` | XPT2046 IRQ | `pinMode(INPUT)` のみ。割り込みは未使用 |
| D1 | `CS_FSRAM_512` | - | 定義のみ（未使用） |

SPI は `SPI.transfer()` を直接呼んでおり、`SPISettings` / `beginTransaction` は使っていません（モード 0・MSB ファースト・ボード既定のクロック）。

## 3. MCP23S17 × 8

### 3.1 構成

- 8 個すべてが同じ /CS（D4）を共有し、ハードウェアアドレス（A2:A0）で区別します。
- コードはオペコードにチャネル番号を加算してアクセスします。

| 項目 | 定義名 | 値 | 内容 |
|---|---|---|---|
| 書き込みオペコード | `MCP23x17_OPE_WRITE` | `0x40` | 実際は `0x40 + ch*2` |
| 書き込みオペコード（A2=1 用） | `MCP23x17_OPE_WRITE_A2` | `0x48` | 初期化の IOCON 書き込み専用 |
| 読み出しオペコード | `MCP23x17_OPE_READ` | `0x41` | 実際は `0x41 + ch*2` |

### 3.2 初期化（`setup()`）

1. D5 を Low→High（nRESET）。
2. IOCON（BANK=0 のアドレス `0x0A`）へ `0x8C`（`IOCR_BANK1` + `IOCR_HAEN` + `IOCR_ODR`）を、オペコード `0x40` と `0x48` の 2 回書き込み。以後は **BANK=1** のレジスタマップを使用します。
   - `0x48` を使う理由はスケッチ内コメントに記載があります（A2=H のチップは A2 ビットを立てたオペコードでないと IOCON が指定されない、との記述。データシートには記載がないとのコメント）。
3. ch0〜ch7 すべてで OLAT_A/B = `0x00`、DDR_A/B = 入力、GPPU_A/B = 全ビットプルアップ。
4. ch7 DDR_A = 入力、ch6 DDR_A/B = 入力、ch7 DDR_B = 出力、ch7 GPIO_B = `0x3F`（全制御線 非選択）。

### 3.3 レジスタ（BANK=1）

| レジスタ | 定義名 | アドレス |
|---|---|---|
| DDR A / B | `MCP23x17_DDR_A` / `MCP23x17_DDR_B` | `0x00` / `0x10` |
| GPPU A / B | `MCP23x17_GPUP_A` / `MCP23x17_GPUP_B` | `0x06` / `0x16` |
| GPIO A / B | `MCP23x17_GPIO_A` / `MCP23x17_GPIO_B` | `0x09` / `0x19` |
| OLAT A / B | `MCP23x17_OLAT_A` / `MCP23x17_OLAT_B` | `0x0A` / `0x1A` |

### 3.4 チャネルの役割

| ch | 役割 | ポート A | ポート B |
|---|---|---|---|
| 0〜5 | 初期化のみ（入力+プルアップ）。以後未使用 | - | - |
| 6 (`CH_MCP_BUS_ADDRESS`) | アドレスバス | A0〜A7（出力時） | A8〜A15（出力時） |
| 7 (`CH_MCP_BUS_DATA` / `CH_MCP_CONTROL`) | データ+制御 | D0〜D7（読むときは入力、書くときは出力） | 制御線 + A16/A17（常時出力） |

アドレスバス（ch6）はアクセスのたびに出力へ切り替え、`loop()` の末尾で入力へ戻します。

## 4. ch7 ポート B（制御線）

| ビット | 信号 | 備考 |
|---|---|---|
| B7 | A17 | アドレス ビット 17 |
| B6 | A16 | アドレス ビット 16 |
| B5 | nCS_SPI_SRAM | SPI SRAM の /CS（MCP 経由の経路を使う場合のみ） |
| B4 | nCS1_FUJISOKU | FUJISOKU /CS1（Low で選択） |
| B3 | nWE | 62256 と FUJISOKU の書き込み。同一ビットを両方で使用 |
| B2 | nOE_62256 | 62256 の出力イネーブル |
| B1 | nOE_FUJISOKU | FUJISOKU の出力イネーブル |
| B0 | nCS_62256 / CS2_FUJISOKU | 62256 は Low で選択、FUJISOKU の CS2 は High で選択（極性が逆） |

> スケッチ内のコメントには B4 を `NC` と書いた箇所がありますが、`ENABLE_FUJISOKU_nCS1_CS2` の値（B4 = 0 で選択）に合わせて nCS1_FUJISOKU として扱っています。

### 4.1 制御値の定義

| 定義名 | 値 | 用途 |
|---|---|---|
| `DISENABLE_SRAM` | `0x3F` | 全制御線 非選択（62256 /CS = High） |
| `DISENABLE_FUJISOKU` | `0x3E` | 全制御線 非選択（CS2 = Low） |
| `ENABLE_FUJISOKU_nCS1_CS2` | `0x2F` | FUJISOKU 選択（nCS1 = Low、CS2 = High） |
| `ENABLE_SRAM_nCS` | `0x3E` | 62256 選択（nCS_62256 = Low） |
| `AND_ENABLE_nWE` | `0xF7` | AND マスク。B3 を Low にして書き込みパルス |
| `AND_ENABLE_FUJISOKU_nOE` | `0xFD` | AND マスク。B1 を Low にして FUJISOKU を読み出し |
| `AND_ENABLE_SRAM_nOE` | `0xFB` | AND マスク。B2 を Low にして 62256 を読み出し |
| `AND_ENABLE_SPI_SRAM` | `0xDF` | AND マスク。B5 を Low にして SPI SRAM を選択 |
| `MCP_IODIR_PE1_CONNECT` | `0xDB` | PE1 接続時の ch7 B の入出力方向（定義のみ・未使用） |

A16/A17 はアドレスから `(address >> 10) & 0xC0` で B7/B6 に重ねて出力します（`set_mcp_sram_address()`）。

## 5. FUJISOKU メモリカード

| 信号 | 接続 |
|---|---|
| A0〜A15 | ch6 GPIO A/B |
| A16 / A17 | ch7 GPIO B の B6 / B7 |
| D0〜D7 | ch7 GPIO A |
| /CS1 | ch7 B4 |
| CS2 | ch7 B0 |
| /OE | ch7 B1 |
| /WE | ch7 B3（62256 と共用） |

- アドレス線は A0〜A17 の 18 本分です。コード中のコメントは「MAX 512kB」ですが、18 ビットなら 256 KB です（要確認）。
- ユーティリティが実際に読み書きする範囲は **0x0000〜0x7FFF（32 KB）** です。

### 5.1 読み出しシーケンス（`read_fujisoku()`）

1. ch7 DDR_A を入力にする。
2. ch6 DDR を出力にし、GPIO_A/B にアドレス下位 16 ビットを出力。
3. ch7 GPIO_B = `ENABLE_FUJISOKU_nCS1_CS2` + A17/A16。
4. ch7 OLAT_B を読み出して保持。
5. GPIO_B = 保持値 AND `0xFD`（/OE を Low）。
6. ch7 GPIO_A を読んでデータ取得。
7. GPIO_B = 保持値（/OE を戻す）、続けて `DISENABLE_FUJISOKU`（全非選択）。

### 5.2 書き込みシーケンス（`write_fujisoku()`）

1. ch7 DDR_A を出力にする。
2. アドレス出力 + 選択（読み出しの 2〜3 と同じ）。
3. ch7 OLAT_B を読み出して保持。
4. GPIO_B = 保持値 AND `0xF7`（/WE を Low）。
5. ch7 GPIO_A にデータを出力。
6. GPIO_B = 保持値（/WE を High に戻して書き込み確定）、続けて `DISENABLE_FUJISOKU`。
7. ch7 DDR_A を入力に戻す。

1 バイトごとにアドレスと制御線を再設定するため、32 KB の転送には時間がかかります。

## 6. 62256 並列 SRAM（32 KB）

- アドレス・データ・/WE は FUJISOKU と共通のバスを共用し、/CS は B0（Low で選択）、/OE は B2 で個別に制御します。
- `read_sram()` / `write_sram()` が対応します。通常のビルドでは使われず、`SPI_SRAM` を定義したときだけ R/C/X/S/W 系の「バッファ」として使われます（`SPI_SRAM` の名前は SPI SRAM ではなく並列 62256 を指す点に注意）。

## 7. SPI SRAM

| 項目 | 値 |
|---|---|
| 想定デバイス | 23LC512 クラスの SPI SRAM（ピン名 `CS_SRAM_512` からの推定。要確認） |
| アドレス | 16 ビット（`address / 0x100`、`address % 0x100` の 2 バイト） |
| READ / WRITE | `SRAM_OPE_READ` = `0x03` / `SRAM_OPE_WRITE` = `0x02` |
| モードレジスタ READ / WRITE | `REG_READ` = `0x05` / `REG_WRITE` = `0x01` |
| 利用範囲 | 0x0000〜0x7FFF（32 KB） |

### 7.1 /CS の経路（2 通り）

| 経路 | /CS | 関数 | 状態 |
|---|---|---|---|
| A | Arduino D6 | `read_spi_sram_byte()` / `write_spi_sram_byte()` ほか | **既定**（通常ビルドで使用） |
| B | MCP23S17 ch7 B5 | `*_spi_sram4MCP_*` | 通常のコマンドでは未使用（T コマンドの試験のみ `*4MCP_byte` を使用） |

実機でどちらの配線かは **T コマンド**で判定できます（両経路でモードレジスタ値と書き込み→読み戻しを試験）。

## 8. 設定値の一覧（コード由来）

| 項目 | 値 |
|---|---|
| シリアル | 115200 bps |
| 消去値 (`SRAM_ERASE`) | `0xFF` |
| モード値 (`MODE_RW_BYTE` / `MODE_RW_PAGE` / `MODE_RW_SEQ`) | `0x00` / `0x40` / `0x80`（未使用。§9 参照） |

## 9. 要確認事項

コードだけでは確定できないため、基板の回路図と照合してください。

1. FUJISOKU カードエッジ（コネクタ）のピン配置と、各信号線への実配線。
2. 電源電圧と信号レベル（3.3 V / 5 V）、レベル変換の有無。
3. MCP23S17 各チップの A2:A0 の設定（コードは「ch 番号 = アドレス」を前提）。
4. SPI SRAM の /CS が D6 か MCP B5 か（T コマンドで判定可能）、および正確な型番。
5. D5 が MCP23S17 の /RESET につながっているか。D3 / D2 の割り込み線の接続。
6. FUJISOKU のアドレス幅（A0〜A17 の 18 本で 256 KB か、コメントどおり 512 KB か）。
7. SPI SRAM のモード値（`MODE_RW_PAGE` = `0x40`、`MODE_RW_SEQ` = `0x80` は、23LC512 のデータシートでは逆の割り当てに見えます。現状は未使用のため実害なし）。
8. ch7 の DDR_B を出力にした直後（OLAT_B = `0x00`）から GPIO_B = `0x3F` を書き込むまでの短時間、全制御線が Low で出力されます（nCS_62256・nWE を含む）。実機での影響は未確認です。
