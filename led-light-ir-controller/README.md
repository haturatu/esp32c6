# LED照明リモコン IR コントローラー

ESP32-C6とVS1838Bで調査したLED照明リモコンを、NEC赤外線送信とHTTP APIから操作する
独立プロジェクトです。

## 調査結果

すべてNEC 32bit、アドレスは`0x01`です。API送信時は、ライブラリが表示するcommand値ではなく、
受信した32bitコードをそのまま送信します。

| リモコン | API command | NEC code | command値 |
| --- | --- | --- | --- |
| 点灯 | `on` | `0x807F00FF` | `0x00` |
| 消灯 | `off` | `0x807F807F` | `0x01` |
| 全灯 | `full` | `0x807F609F` | `0x06` |
| 明るく | `brighter` | `0x807FA05F` | `0x05` |
| 暗く | `dimmer` | `0x807F20DF` | `0x04` |
| 白く | `cooler` | `0x807F40BF` | `0x02` |
| 暖かく | `warmer` | `0x807F50AF` | `0x0A` |
| 切替 | `toggle` | `0x807FC03F` | `0x03` |
| 常夜灯 | `night_light` | `0x807FD02F` | `0x0B` |
| 取消 | `cancel` | `0x807FE01F` | `0x07` |
| 15分 | `timer_15m` | `0x807F22DD` | ライブラリ表示 `0x44` |
| 30分 | `timer_30m` | `0x807FFF00` | `0xFF` |

`cooler`は色温度を上げる、`warmer`は暖色方向へ変更する意味で採用しています。

リモコンの長押しではNEC repeatフレームが続けて送信されます。APIは通常の短押し相当として、
32bitフレームを1回だけ送信します。照明が反応しない場合は、実機で確認しながらrepeat回数を
調整します。

## 配線

```text
VS1838B VCC  -> ESP32-C6 3V3
VS1838B GND  -> ESP32-C6 GND
VS1838B OUT  -> ESP32-C6 GPIO5
```

IR LED送信回路は既存のGPIO4配線を使用します。

```text
ESP32 GPIO4 --1kΩ--> NPNトランジスタのベース
NPNエミッタ       -> GND
3V3 --47〜100Ω--> IR LEDアノード
IR LEDカソード   -> NPNコレクタ
```

GPIO4からIR LEDへ直接大電流を流さず、トランジスタを使用してください。

## HTTP API

認証は実装していません。LANまたはVPN内で使用し、インターネットへ直接公開しないでください。

### `GET /api/v1/system/health`

認証不要の死活確認です。

```json
{"status":"ok"}
```

### `GET /api/v1/light/state`

最後にESP32が送信したコマンドを返します。赤外線は片方向なので、照明本体の実状態ではありません。

```json
{
  "last_command": "on",
  "last_code": "0x807F00FF",
  "protocol": "NEC",
  "bits": 32,
  "last_transmitted_at_ms": 12345,
  "state_source": "transmitted"
}
```

### `POST /api/v1/light/command`

リクエスト:

```json
{"command":"on"}
```

`command`に指定できる値は、調査結果表のAPI command列です。

レスポンス:

```json
{
  "ok": true,
  "command": "on",
  "code": "0x807F00FF",
  "ir": {
    "transmitted": true,
    "acknowledged": false
  }
}
```

成功の意味はESP32が赤外線送信処理を実行したことです。照明本体からACKは返りません。
100ms未満の連続送信は`429 ir_rate_limited`になります。

使用例:

```bash
curl http://esp32-light.local/api/v1/light/state

curl -X POST \
  -H 'Content-Type: application/json' \
  -d '{"command":"cooler"}' \
  http://esp32-light.local/api/v1/light/command
```

## Wi-FiとNVS

SSIDとパスワードはソースコードに含めず、Preferences/NVSの`secrets` namespaceへ保存します。
電源を切ってもNVSの値は維持されます。

```bash
python3 -m pip install pyserial
python3 provision.py \
  --port /dev/ttyACM0 \
  --ssid '<SSID>' \
  --password '<PASSWORD>'
```

既存のエアコンAPIと同じ`ssid`、`wifi_pass`キーを使用するため、同じESP32へ書き込む場合は
保存済みのWi-Fi設定をそのまま利用できます。

## コンパイル・書き込み

リポジトリのルートで、全プロトコルを有効にして高速コンパイルします。

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

シリアルモニタからは、例えば次のコマンドでも送信できます。

```text
send on
send cooler
status
help
```
