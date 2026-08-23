# XIAO ESP32-C6 FS90 servo

Seeed Studio XIAO ESP32-C6 で FEETECH FS90 サーボを動かすための単体テスト用
スケッチです。起動時にはサーボを動かしません。シリアルモニタから角度指定や
給湯器スイッチ操作を実行できます。

## 配線

信号線は XIAO の `D1`（GPIO1）を使用します。

```text
XIAO ESP32-C6       FS90
D1  --------------  Signal（橙 / 黄 / 白）
GND --------------  GND（茶 / 黒）
5V  --------------  +5V（赤）
```

サーボを XIAO の `3V3` 端子へ接続しないでください。サーボ始動時の電流で
ESP32-C6 がリセットする場合は、サーボだけ別の 5V 電源から給電し、電源の GND と
XIAO の GND を共通にします。2N2222 はサーボの電源ラインには不要です。

最初は給湯器へ取り付けず、サーボ単体で動作を確認してください。

## ライブラリ

Arduino IDE または Arduino CLI のライブラリマネージャーで `ESP32Servo` を
インストールします。

```bash
arduino-cli lib install ESP32Servo
```

## ビルドと書き込み

リポジトリのルートで実行します。

```bash
arduino-cli compile \
  --jobs 4 \
  --build-path .build/fs90-servo \
  --fqbn 'esp32:esp32:esp32c6:CDCOnBoot=cdc' \
  fs90-servo

arduino-cli upload \
  -p /dev/ttyACM0 \
  --build-path .build/fs90-servo \
  --fqbn 'esp32:esp32:esp32c6:CDCOnBoot=cdc' \
  fs90-servo

arduino-cli monitor \
  -p /dev/ttyACM0 \
  --config baudrate=115200
```

ポートは環境に合わせて変更してください。書き込み後に XIAO のリセットボタンを
押すと、シリアルモニタへ接続できます。

## シリアルコマンド

起動時はサーボを動かしません。次のコマンドを 115200 baud で送信します。

```text
help
status
angle 90
center
sweep
stop
detach
heater-on
heater-off
```

`sweep` はスケッチ内の `kTestLeftAngle` と `kTestRightAngle` の間を 2 秒ごとに
連続往復します。`stop` を送ると 90°へ戻って PWM を停止します。角度を少しずつ
確認するときは `angle 85` のように 5°刻みで指定し、必要に応じて `detach` で
PWM を停止します。

## 給湯器スイッチへの取り付け

サーボホーンをスイッチに対して横方向に取り付け、まず次の中立位置を確認します。

```text
90° = スイッチに触れない位置
```

ホーンを取り付けた後、`fs90-servo.ino` の次の値を実機に合わせて変更します。

```cpp
constexpr uint8_t kHeaterOnAngle = 65;
constexpr uint8_t kHeaterOffAngle = 115;
```

`heater-on` または `heater-off` は、対応する角度へ 500 ms 移動し、90°へ戻って
500 ms 待ってから PWM を停止します。スイッチが確実に切り替わる最小の角度を使い、
最初から 0° / 180°には設定しないでください。取り付け後も、サーボ単体での確認と
同じく、必ず給湯器の動作を監視できる状態で試験してください。
