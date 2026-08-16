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

シリアル操作コマンドとARC446A3の調査結果は、[daikin-an22nesj-w/README.md](daikin-an22nesj-w/README.md)
にまとめています。
