```
$ sudo usermod -aG uucp "$USER"
$ newgrp uucp

# --jobs 4 はこの環境でリンクまで安定して速い並列数。
# --build-path を固定すると、2 回目以降は変更分だけ再コンパイルされる。
$ arduino-cli compile \
  --jobs 4 \
  --build-path .build/daikin-fast \
  --fqbn 'esp32:esp32:esp32c6:CDCOnBoot=cdc' \
  --build-property 'compiler.cpp.extra_flags=-D_IR_ENABLE_DEFAULT_=false -DSEND_DAIKIN=true -DDECODE_DAIKIN=true' \
  daikin-an22nesj-w

$ arduino-cli upload \
  -p /dev/ttyACM0 \
  --build-path .build/daikin-fast \
  --fqbn 'esp32:esp32:esp32c6:CDCOnBoot=cdc' \
  daikin-an22nesj-w

# Press RST Button
$ arduino-cli monitor \
  -p /dev/ttyACM0 \
  --config baudrate=115200
```

## 配線

VS1838B は次のように接続する（基板の `VCC` / `GND` / `OUT` 表記を優先し、
製品ごとのピン順を決め打ちしない）。

```text
VS1838B VCC  -> ESP32-C6 3V3
VS1838B GND  -> ESP32-C6 GND
VS1838B OUT  -> ESP32-C6 GPIO5
```

GPIO5 は受信用です。VS1838B の OUT を GPIO5 に接続したまま、GPIO5 を送信出力
にする旧スケッチは実行しないでください。

送信には別の赤外線 LED 回路が必要です。次のように GPIO4 に接続します。

```text
ESP32 GPIO4 --1kΩ--> NPN トランジスタのベース
NPN エミッタ       -> GND
3V3 --47〜100Ω--> IR LED アノード
IR LED カソード   -> NPN コレクタ
```

ESP32 の GPIO から赤外線 LED に大電流を直接流さず、トランジスタを使ってください。
LED はエアコン室内機の受光部へ向けます。VS1838B と ESP32、送信回路の GND は共通に
します。

起動時は赤外線を送信しない。シリアルモニタから次のコマンドでテストできる。

対象リモコンは `ARC446A3`、プロトコルは `DAIKIN 280-bit`。

```text
on
off
auto 25
cool 24
heat 24
dry 25
fan
temp 26
fan auto
fan 1
fan 2
fan 3
fan 4
fan 5
swing on
swing off
sleep on
sleep off
comfort on
comfort off
mold on
mold off
quiet on
quiet off
timer-on 420
timer-off 1380
timer-cancel
replay
raw_on
raw_off
burst_on
burst_off
inv_on
inv_off
status
```

赤外線は片方向通信なので、`[INFO] IR frame sent` は ESP32 がフレームを出力
したことだけを示す。エアコン本体の受光音・運転ランプ・風の変化で実機の受信を
確認すること。VS1838B で純正リモコンのボタンを押すと、シリアルモニタに
`IR frame received`、プロトコル、raw データが表示される。まず純正リモコンの
`運転/停止` または `冷房` をエアコンへ向けず VS1838B に向けて押し、受信できる
ことを確認する。その後 `replay` を入力すると、受信した純正リモコンの状態を
そのままGPIO4から再送できる。

`raw_on` / `raw_off` は、今回の純正リモコンから取得したON/OFFフレームを
そのまま送る診断用コマンド。`burst_on` / `burst_off` は同じフレームを3回送り、
`inv_on` / `inv_off` は送信極性を反転して3回送る。

純正リモコンの未知ボタンをVS1838Bへ向けて押すと、前回のDAIKINフレームとの差分が
`[DEBUG] diff byte[...]` として表示される。快眠・健康冷房などは、この差分を確認して
から専用コマンドへ割り当てる。

ARC446A3の快眠は`byte[29]`の`0x04`として確認済みで、`sleep on/off`で操作できる。
