# home-ir-api-server

ESP32-C6で複数の赤外線家電をHTTP APIから操作するための統合スケッチです。

```text
HTTP API
   ↓
Device API
   ↓
Device
   ↓
IrSender
   ↓
GPIO4 / 赤外線LED
```

照明は「現在状態を設定する」よりも「リモコンのボタンを押す」方が正確なので、
command型APIにしています。赤外線は片方向であり、照明本体からACKは返りません。

## ディレクトリ構成

```text
home-ir-api-server/
├── home-ir-api-server.ino
├── config.h
├── config.example.h
├── provision.py
├── api/
│   ├── AirconApi.cpp
│   ├── AirconApi.h
│   ├── LightApi.cpp
│   ├── LightApi.h
│   ├── SystemApi.cpp
│   └── SystemApi.h
├── devices/
│   ├── daikin/
│   │   ├── DaikinAircon.cpp
│   │   └── DaikinAircon.h
│   └── light/
│       ├── CeilingLight.cpp
│       ├── CeilingLight.h
│       └── CeilingLightCodes.h
└── ir/
    ├── IrSender.cpp
    └── IrSender.h
```

`CeilingLight`はHTTP文字列を`LightCommand` enumへ変換し、IRコードの詳細は
`CeilingLightCodes.h`に閉じ込めています。将来コードを再調査する場合は、原則として
このファイルだけを変更します。

## 照明API

### 汎用command endpoint

```http
POST /api/v1/light/command
Content-Type: application/json
```

```json
{"command":"brighter"}
```

対応コマンド:

| リモコン | command | NEC 32bit code |
| --- | --- | --- |
| 点灯 | `on` | `0x807F00FF` |
| 消灯 | `off` | `0x807F807F` |
| 全灯 | `full` | `0x807F609F` |
| 明るく | `brighter` | `0x807FA05F` |
| 暗く | `dimmer` | `0x807F20DF` |
| 白く | `cooler` | `0x807F40BF` |
| 暖かく | `warmer` | `0x807F50AF` |
| 切替 | `toggle` | `0x807FC03F` |
| 常夜灯 | `night_light` | `0x807FD02F` |
| 取消 | `cancel` | `0x807FE01F` |
| 15分 | `timer_15m` | `0x807F22DD` |
| 30分 | `timer_30m` | `0x807FFF00` |

レスポンス例:

```json
{
  "ok": true,
  "device": "light",
  "command": "brighter",
  "code": "0x807FA05F",
  "ir": {"transmitted": true, "acknowledged": false}
}
```

### 個別command endpoint

汎用endpointに加えて、次の個別endpointも登録しています。

```text
POST /api/v1/light/commands/on
POST /api/v1/light/commands/off
POST /api/v1/light/commands/full
POST /api/v1/light/commands/brighter
POST /api/v1/light/commands/dimmer
POST /api/v1/light/commands/cooler
POST /api/v1/light/commands/warmer
POST /api/v1/light/commands/toggle
POST /api/v1/light/commands/night-light
POST /api/v1/light/commands/cancel
POST /api/v1/light/commands/timer-15m
POST /api/v1/light/commands/timer-30m
```

例:

```bash
curl -X POST http://esp32.local/api/v1/light/commands/brighter
curl -X POST -H 'Content-Type: application/json' \
  -d '{"command":"night_light"}' \
  http://esp32.local/api/v1/light/command
```

未設定のコードは`501 ir_code_not_configured`として返せる設計です。

## エアコンAPI

既存のDaikinデバイスを統合するため、基本的な互換APIを用意しています。

```text
GET  /api/v1/aircon/state
POST /api/v1/aircon/state
POST /api/v1/aircon/off
```

状態指定の完全なエアコンAPIは既存の`daikin-an22nesj-w-http-api-server`を基準に、
今後`DaikinAircon`へ段階的に移植します。照明追加によって既存のエアコンプロジェクトを
変更しない構成です。

## system API

認証は実装していません。LAN/VPN内だけで使用し、インターネットへ直接公開しないでください。

```text
GET /api/v1/health
GET /api/v1/system/health
GET /api/v1/system/info
```

## 配線

```text
VS1838B VCC  -> ESP32-C6 3V3
VS1838B GND  -> ESP32-C6 GND
VS1838B OUT  -> ESP32-C6 GPIO5
```

送信用IR LEDは、既存プロジェクトと同じGPIO4のトランジスタ回路を使用します。

```text
ESP32 GPIO4 --1kΩ--> NPNトランジスタのベース
NPNエミッタ       -> GND
3V3 --47〜100Ω--> IR LEDアノード
IR LEDカソード   -> NPNコレクタ
```

## Wi-Fi provisioning

SSIDとパスワードはソースコードへ入れず、Preferences/NVSの`secrets` namespaceへ保存します。
電源を切ってもNVSの値は維持されます。既存のエアコンAPIと同じキーを使用します。

```bash
python3 -m pip install pyserial
python3 provision.py \
  --port /dev/ttyACM0 \
  --ssid '<SSID>' \
  --password '<PASSWORD>'
```

## コンパイルと書き込み

リポジトリのルートから実行します。現在の統合スケッチで必要なIR機能だけを有効にし、
`--jobs 4`で並列コンパイルします。

```bash
arduino-cli compile \
  --jobs 4 \
  --build-path .build/home-ir-api-server-fast \
  --fqbn 'esp32:esp32:esp32c6:CDCOnBoot=cdc' \
  --build-property 'compiler.cpp.extra_flags=-D_IR_ENABLE_DEFAULT_=false -DDECODE_NEC=true -DSEND_NEC=true -DSEND_RAW=true -DDECODE_DAIKIN=true -DSEND_DAIKIN=true' \
  home-ir-api-server

arduino-cli upload \
  -p /dev/ttyACM0 \
  --build-path .build/home-ir-api-server-fast \
  --fqbn 'esp32:esp32:esp32c6:CDCOnBoot=cdc' \
  home-ir-api-server

arduino-cli monitor \
  -p /dev/ttyACM0 \
  --config baudrate=115200
```

## NTPと認証

この統合版は照明APIに時刻処理を必要としないため、NTPへ接続しません。Wi-Fiパスワードは
NVSに保存し、Basic認証も使用しません。ネットワーク境界はLAN/VPNで管理してください。
