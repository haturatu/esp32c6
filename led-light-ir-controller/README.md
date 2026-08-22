# LED照明リモコン IR 調査・制御

ESP32-C6とVS1838Bで、LED照明リモコンの赤外線信号をボタンごとに調査し、
最終的にHTTP APIから操作するための独立プロジェクトです。

## 対象APIコマンド

| リモコンのボタン | API command | 状態 |
| --- | --- | --- |
| 点灯 | `on` | 調査待ち |
| 消灯 | `off` | 調査待ち |
| 全灯 | `full` | 調査待ち |
| 明るく | `brighter` | 調査待ち |
| 暗く | `dimmer` | 調査待ち |
| 白く | `cooler` | 調査待ち |
| 暖かく | `warmer` | 調査待ち |
| 切替 | `toggle` | 調査待ち |
| 常夜灯 | `night_light` | 調査待ち |
| 取消 | `cancel` | 調査待ち |
| 15分 | `timer_15m` | 調査待ち |
| 30分 | `timer_30m` | 調査待ち |

`cooler`は色温度を上げる、`warmer`は色温度を下げる意味で採用します。
実際の送信実装では、リモコンが絶対値を送るのか、現在値に対する増減命令を送るのかを
受信結果の差分で確認します。

## 現在の調査スケッチ

`led-light-ir-controller.ino`は受信専用です。GPIO4のIR LEDは使用せず、VS1838Bから
受信した全フレームをシリアルへ出力します。

出力には次を含めます。

- IRremoteESP8266が判定したプロトコル
- ビット数、リピート、オーバーフロー
- 人間向けのデコード結果
- `uint16_t rawData[]`形式のソースコード

未知プロトコルでもraw timingを記録できるため、ライブラリが直接デコードできない場合も
ボタンごとの差分を調べられます。

## 配線

既存のVS1838B配線をそのまま使用します。

```text
VS1838B VCC  -> ESP32-C6 3V3
VS1838B GND  -> ESP32-C6 GND
VS1838B OUT  -> ESP32-C6 GPIO5
```

この調査スケッチは受信専用なので、IR LED送信回路は接続したままでも問題ありませんが、
GPIO4からは出力しません。

## コンパイル・書き込み・監視

リポジトリのルートで実行します。全プロトコルのデコーダを有効にして調査します。

```bash
arduino-cli compile \
  --jobs 4 \
  --build-path .build/led-light-ir-controller-fast \
  --fqbn 'esp32:esp32:esp32c6:CDCOnBoot=cdc' \
  led-light-ir-controller

arduino-cli upload \
  -p /dev/ttyACM0 \
  --build-path .build/led-light-ir-controller-fast \
  --fqbn 'esp32:esp32:esp32c6:CDCOnBoot=cdc' \
  led-light-ir-controller

arduino-cli monitor \
  -p /dev/ttyACM0 \
  --config baudrate=115200
```

## ボタン調査の手順

1. シリアルモニタを115200 baudで開く。
2. リモコンをVS1838Bへ向ける。
3. 指定されたボタンを短く1回押す。
4. `LED_REMOTE_CAPTURE_BEGIN`から`LED_REMOTE_CAPTURE_END`までを記録する。
5. 長押しが必要なボタンは、短押しと長押しを分けて調査する。

一度の押下で複数フレームが出る場合があります。`repeat`と各raw timingを比較し、
1回のAPI呼び出しで何回送信すべきかを決めます。

## 実装方針

まず全ボタンを受信して、次の観点で信号を分類します。

1. プロトコルがNEC等としてデコードできるか。
2. 同じボタンで値が安定するか。
3. `brighter`、`dimmer`、`cooler`、`warmer`が相対操作か。
4. タイマーや常夜灯が独立した命令か、照明状態全体のフレームか。
5. 短押し・長押し・リピートで送信回数が変わるか。

信号調査が終わったら、受信スケッチを送信機能とHTTP APIへ分離して実装します。
