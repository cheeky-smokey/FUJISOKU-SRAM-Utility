# 通信仕様（PC ⇄ 本体）

Web アプリ（`webapp/FSRAM-Utility.html`）は、この仕様のコマンドだけで本体を操作しています。シリアル端末から同じコマンドを手で送ることもできます。

## 1. 基本

| 項目 | 値 |
|---|---|
| 接続 | USB シリアル（UNO R4 の USB CDC）、115200 bps |
| コマンド | 1 行 1 コマンド。改行は LF か CRLF。1 行 199 文字まで（超えると `ERR LINE`） |
| コマンド名 | 大文字・小文字を区別しない |
| 数値 | 10 進、または `0x` 付きの 16 進 |
| 対象 | `f` = FUJISOKU カード、`s` = SPI SRAM（`USE_SPI_SRAM` が 1 で、起動時に見つかったときだけ。見つからなければ `ERR NOSPI`、0 なら `ERR DISABLED`）、`p` = 62256（`USE_SRAM62256` が 1 のときだけ。0 なら `ERR DISABLED`） |
| 本体からの行 | 改行は CRLF |
| 中断 | 処理中に `!`（1 バイト、改行不要）を送る。待機中に届いた `!` は捨てる |

1 文字だけの行は、v1 と同じ 1 文字コマンドとして扱います（§4）。

### 1.1 行の種類

| 先頭 | 意味 |
|---|---|
| `OK ` | 成功（終了行） |
| `NG ` | 処理はできたが結果が不一致（終了行） |
| `ERR <コード> <説明>` | 失敗（終了行）。コードは §3 |
| `HELLO ` / `STATUS ` / `LCD ` | それぞれ `hello` / `status` / `lcd` の応答（終了行） |
| `D <アドレス> <16進>` | `rd` のデータ（1 行 64 バイトまで。アドレスは 16 進 4 桁、0x10000 以上は 5 桁） |
| `F <名前> <バイト数>` | `sd ls` のファイル |
| `T ...` | 自己診断・バス試験の詳細 |
| `M <アドレス> <値1> <値2>` | `cmp` の不一致（最初の 8 か所、16 進） |
| `S <段階>` | 処理の段階が変わった（例: `S Reading card`） |
| `P <済み> <合計>` | 進捗（0.1 秒ごと、最後は必ず合計） |
| `E SD 0` / `E SD 1` | SD カードが使えなくなった / 使えるようになった |
| `E UI timeout` | PC 主導の表示（§2.4）で PC から 8 秒連絡がなく、表示を戻した |
| `LCD <16進 160 桁>` | LCD の表示（`lcd on` のとき、変わるたびに送られる） |

`E` と `LCD` の行は、ほかのコマンドの応答の途中にも割り込みます。受け取る側は行の先頭で振り分けてください。

処理中（コピーや SD 保存など）は、`!` 以外の入力を読み捨てます。終了行が届いてから次のコマンドを送ってください。

## 2. コマンド

### 2.1 情報

| コマンド | 応答の例 |
|---|---|
| `hello` | `HELLO FSU 2.5 size=32768 cardmax=262144 chunk=64 lcd=20x4 sram62256=0 sdcard=0 spisram=ok`（既定の構成）。`spisram` は SPI SRAM の状態で `ok`（使える）/ `none`（見つからない）/ `off`（`USE_SPI_SRAM` が 0）（v2.5 から。無ければ `ok` とみなす）。`size` は本体だけの処理（§2.3）が扱う大きさ、`cardmax` は `rd` / `wr` / `fill` でカードに使える大きさ（v2.4 から。無ければ 32768 とみなす）。`lcd` は LCD が見つかれば `20x4`、無ければ `none`。`sram62256` / `sdcard` は `USE_SRAM62256` / `USE_SD` の値 |
| `status` | `STATUS sd=- busy=0 spi=40 lcd=1`（`sd` は SD カードが使えるか（`1` / `0`）。`USE_SD` が 0（既定）なら `sd=-`。`spi` は SPI SRAM（/CS = D6）のモードレジスタを 16 進 2 桁で。起動時にシーケンシャルにするので通常は `40`、応答がなければ `FF`、`USE_SPI_SRAM` が 0 なら `-`。`lcd` は LCD の有無） |
| `help`（または `?` / `h`） | コマンドの一覧（終了行なし） |
| `lcd` | `LCD <16進 160 桁>`: 画面の 4 行 × 20 文字。文字コード `08`〜`0F` は自作文字（進捗バーなど）。LCD が無くても本体は画面の内容を持っているので、同じように返る |
| `lcd on` / `lcd off` | 表示が変わるたびに `LCD` 行を送る / やめる（`lcd off` は `OK lcd off`）。USB が切れると自動でやめる |

### 2.2 メモリの読み書き（PC 主導）

| コマンド | 内容 | 応答 |
|---|---|---|
| `rd <f\|s\|p> <アドレス> <長さ>` | 読み出し | `D 0100 DEADBEEF...` を 64 バイトずつ、最後に `OK rd <長さ>` |
| `wr <f\|s\|p> <アドレス> <16進>` | 書き込み（64 バイトまで） | `OK wr <アドレス(10進)> <バイト数>` |
| `fill <f\|s\|p> <アドレス> <長さ> <値>` | 同じ値で埋める | `OK fill <長さ>` |

- 範囲は対象ごとに違います。はみ出すと `ERR RANGE`（`アドレス + 長さ` が 32 ビットで桁あふれする値も `ERR RANGE`）。

  | 対象 | 範囲 | 理由 |
  |---|---|---|
  | `f` カード | 0x00000〜0x3FFFF（256KB） | アドレス線 A0〜A17 がつながっている。32KB のカードでは 0x8000 以降は先頭と同じ場所になる |
  | `s` SPI SRAM | 0x0000〜0xFFFF（64KB） | 23LC512 の容量 |
  | `p` 62256 | 0x0000〜0x7FFF（32KB） | 62256 の容量 |

- `wr` は 16 進をすべて確かめてから書き込みます（不正な文字があれば何も書かずに `ERR ARG`）。
- `rd` と `fill` は途中で `!` を受け付けます（`ERR CANCEL cancelled`）。
- 本体だけの処理（§2.3。コピー・比較・消去・SD）は、これまでどおり 0x0000〜0x7FFF の 32KB を扱います。

### 2.3 本体だけで行う処理

すべて `S` / `P` 行で進み具合を知らせ、`OK` / `NG` / `ERR` の 1 行で終わります。SD のコマンド（`sd` `sdcheck`）は `USE_SD` を 1 にしたときだけ使えます。0（既定）のときは `ERR DISABLED SD card is not used (USE_SD 0)` を返します。

| コマンド | 内容 | 終了行の例 |
|---|---|---|
| `copy <x> <y>` | x → y のコピー（`copy f s` = カード → SPI SRAM、`copy s f` = SPI SRAM → カード、`copy f p` など） | `OK copy 32768 bytes` |
| `cmp [<x> <y>]` | 比較。引数なしは SPI SRAM とカード（`cmp s f`） | `M 0100 3C 00` …、`OK cmp all 32768 bytes match` / `NG cmp mismatches=3 first=0x0100` |
| `erase <f\|s\|p>` | すべて `FF` にする | `OK fill 32768 bytes = FF` |
| `test`（`test s`） | SPI SRAM（/CS = D6）の自己診断。SPI SRAM を探し直して（見つかっていなくても実行する）モードレジスタを読み、両端の 16 バイトに 2 種類の値を書いて読み戻す（元の値は戻す） | `T mode=40`、`T readback=A0A1…AF bad=0`、`OK test SPI SRAM OK` / `NG test no response (check /CS D6) bad=64`（モードが `FF`）/ `NG test read/write failed bad=3` |
| `test p` | 62256 でバスの配線試験（62256 の中身は消える） | `T 62256 data_bad=0x00 addr_bad=0x0`、`OK test D0-D7 OK A0-A14 OK` / `NG test data bad=0x04 addr bad=0x0020`（ビット = 疑わしい線） |
| `sd save <f\|s\|p> [名前]` | カード（または SPI SRAM、62256）→ SD | `OK sd 32768 bytes -> game1.bin` |
| `sd load <f\|s\|p> [名前] [v]` | SD → カード（など）。`v` を付けると書き込み後に照合 | `OK sd 32768 bytes <- game1.bin` |
| `sd verify <f\|s\|p> [名前]` | SD とカード（など）を照合 | `OK sd all 32768 bytes match` / `NG sd mismatches=1 first=0x0100` |
| `sd ls` | ルートの `*.bin` を一覧 | `F game1.bin 32768` …、`OK sd 2 files` |
| `sdcheck`（`sd check`） | SD カードを認識し直す | `OK sdcheck SD card ready` / `ERR NOSD No SD card` |

- 名前は英数字と `_` `-` の 16 文字まで（`.bin` は付けない）。省略または `-` で `fujisoku`。
- `sd load` は 32,768 バイトでないファイルを書き込みません（`ERR BADSIZE File is 1000 bytes`）。`sd verify` では `NG sd File is 1000 bytes (need 32768)` になります。
- `sd save` は一時ファイル `FSU_TMP.TMP` に書いてから名前を付け替えるので、失敗しても同じ名前の前のファイルは残ります。

### 2.4 PC 主導の処理を LCD に表示する

PC が `rd` / `wr` で処理をするとき、本体の LCD に進捗を出すためのコマンドです。

| コマンド | 内容 |
|---|---|
| `ui begin <合計> <題名>` | 進捗画面を出す。題名の `_` は空白になる。以後、ほかの処理（`copy` など）は `ERR BUSY` |
| `ui prog <済み> [段階]` | 進捗を更新（段階の `_` は空白） |
| `ui end <ok\|ng\|err\|cancel> [文]` | 結果画面を出して終わる |

`ui begin` から 8 秒間 PC からコマンドが届かないと、本体は「PC LOST」を表示して `E UI timeout` を送り、待機に戻ります（ブラウザを閉じた場合など）。

### 2.5 Web アプリが使う手順

| 操作 | 送るコマンド |
|---|---|
| 接続 | 改行 → `hello`（応答があるまで最大 4 回。`lcd=` `sram62256=` `sdcard=` `cardmax=` で構成を知り、`sdcard=0` なら SD のタブを隠し、`cardmax` より大きい読み込みのボタンは押せなくする）→ `status` → `lcd on` |
| PC に保存 | `ui begin <大きさ×2> Card_to_PC` → `rd f <a> 2048` と `ui prog` を（大きさ ÷ 2048）回 × 2 周（32KB なら 16 回）→ 2 周の結果を比べる → `ui end ok Saved_to_PC` |
| カードに書き込み | （32KB より大きいファイルは、先頭 32KB だけかすべてかを PC 側で選ぶ）→（書き込む前の保存: 書き込む大きさの読み込みを 1 周）→ `ui begin <合計> PC_to_Card` → 変わった 64 バイトだけ `wr f <a> <16進>`（16 回ごとに `ui prog`）→ `rd` で読み戻して照合 → `ui end ok ...` |
| 比較（カード） | `ui begin <ファイルの大きさ> Compare` → `rd` をファイルの大きさまで → PC 側で比較 → `ui end ok\|ng ...` |
| SD・SPI SRAM の操作 | 2.3 のコマンドをそのまま送り、`S` / `P` 行で進捗を出す |
| 中断 | `!` を送る。PC 主導の処理では区切りで止めて `ui end cancel Canceled` |

## 3. エラーコード

| コード | 意味 | 対処 |
|---|---|---|
| `ARG` | 引数が足りない・不正（ファイル名の文字、使えない対象 `p` を含む） | コマンドを確かめる |
| `RANGE` | アドレスが対象の範囲（§2.2）をはみ出す | |
| `CMD` | 知らないコマンド | |
| `LINE` | 1 行が長すぎる | 199 文字以内にする |
| `BUSY` | ほかの処理中（PC 主導の表示中を含む） | 終わるのを待つ |
| `CANCEL` | 中断した | |
| `NOSD` | SD カードが無い・認識できない | 挿してやり直す |
| `SDLOST` | 処理の途中で SD カードが抜かれた | 挿してやり直す（前のファイルは残る） |
| `SDERR` | SD カードは応答するが読み書きに失敗した | カードを確かめる |
| `NOFILE` | 指定したファイルが無い | `sd ls` で確かめる |
| `BADSIZE` | ファイルが 32,768 バイトでない | |
| `DISABLED` | その機能を使わない設定でビルドされている（`USE_SD` / `USE_SPI_SRAM` / `USE_SRAM62256` が 0 のときのコマンド・対象） | `config.h` を確かめる |
| `NOSPI` | SPI SRAM が見つからない（`USE_SPI_SRAM` は 1） | 配線（/CS = D6・SCK・MISO・MOSI・電源）を確かめて `test` を実行する |

## 4. 1 文字コマンド（v1 互換）

| 文字 | 同じ意味のコマンド |
|---|---|
| `R` / `C` | `copy f s` / `copy s f`（`R` `C` `X` `E` `T` は SPI SRAM を使う。使えなければ `ERR NOSPI` / `ERR DISABLED`。`T` は見つからなくても実行する） |
| `X` | `cmp` |
| `B` / `L` / `V` | `sd save f` / `sd load f` / `sd verify f`（`fujisoku.bin`）。`USE_SD` が 0 なら `ERR DISABLED` |
| `S` / `W` | `sd save s` / `sd load s`（同上。SPI SRAM が使えなければ `ERR NOSPI` / `ERR DISABLED`） |
| `T` | `test` |
| `E` / `e` | `erase s` / `erase f` |
| `?` / `h` | `help` |

v1 との違い: 応答の書式が §1.1 に変わりました。`R` / `C` は書き込み前に消去しません（全域を上書きするため）。`L` / `W` は 32,768 バイトちょうどのファイルだけを受け付けます。

## 5. やりとりの例

`USE_SD` を 1 にした本体の例です。

```
> hello
< HELLO FSU 2.5 size=32768 cardmax=262144 chunk=64 lcd=20x4 sram62256=0 sdcard=1 spisram=ok
> sd save f game1
< S Checking SD card
< S Opening file
< S Reading card
< P 2560 32768
< ...
< P 32768 32768
< S Closing file
< S Saving file
< OK sd 32768 bytes -> game1.bin
> rd f 0x100 4
< D 0100 DEADBEEF
< OK rd 4
> sd verify f nothing
< S Checking SD card
< S Opening file
< ERR NOFILE File not found
```
