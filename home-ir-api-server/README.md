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
│   ├── ApiResponse.h
│   ├── Json.h
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
このファイルだけを変更します。APIレスポンスのコード表示も同じ`IrCode`から生成します。

Daikinと照明は同じ`IrSender`インスタンスを共有します。Daikinプロトコル用の
`IRDaikinESP`も`IrSender`が所有するため、GPIO4を家電クラスが個別に初期化しません。

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

`/api/v1/light/command`がcanonical APIです。操作のしやすさを目的に、次の個別endpointも
convenience aliasとして登録しています。新規クライアントはcanonical APIを推奨します。

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

未設定のコードは`501 ir_code_not_configured`、未初期化は`500 ir_sender_not_initialized`、
不正なIRデータは`500 invalid_ir_code`として返します。

### 照明状態

```http
GET /api/v1/light/state
```

照明本体からACKは返らないため、最後に送信または受信したリモコン操作を返します。
起動直後は`state_source`が`initial`、短時間に連続送信した場合は`429 ir_rate_limited`です。

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

APIのJSON入力は共通の小さなstrict parserで検証します。空白、型、重複キー、末尾データ、
整数の形式を検証するため、例えば`{"temperature":26foo}`は受け付けません。エラーJSONは
文字列のquoteとbackslashをescapeして生成します。

## エアコンAPI

ARC446A3 / Daikin AN22NESJ-Wの完全な状態APIです。canonical pathは
`/api/v1/aircon/state`ですが、既存クライアント向けに`/api/v1/ac/state`も同じ内容で
提供します。状態変更は`PATCH`を推奨し、旧統合版向けに`POST`も受け付けます。

```text
GET  /api/v1/aircon/state
PATCH /api/v1/aircon/state
POST /api/v1/aircon/state
POST /api/v1/aircon/off
GET  /api/v1/ac/state
PATCH /api/v1/ac/state
POST /api/v1/ac/off
```

対応フィールドは`power`, `mode`, `temperature`, `dry_offset`, `auto_offset`, `fan`,
`swing`, `sleep`, `health`, `comfort`, `clean`, `quiet`, `timer.on`, `timer.off`です。

`temperature`は冷房/暖房/送風で10〜32℃、`dry_offset`は除湿時に-2〜+2、
`auto_offset`は自動時に-5〜+5です。自動・除湿時に`temperature`を指定すると422になります。
タイマーは現在からの相対分数で、0〜720分（最大12時間）です。

```bash
curl -X PATCH -H 'Content-Type: application/json' \
  -d '{"power":true,"mode":"cool","temperature":26,"fan":"auto","swing":true}' \
  http://esp32.local/api/v1/ac/state

curl -X PATCH -H 'Content-Type: application/json' \
  -d '{"mode":"dry","dry_offset":-1}' \
  http://esp32.local/api/v1/ac/state

curl -X PATCH -H 'Content-Type: application/json' \
  -d '{"mode":"auto","auto_offset":-3,"timer":{"off":120}}' \
  http://esp32.local/api/v1/ac/state
```

`health=true`または`comfort=true`の間に風量を変更すると、純正リモコンの制約に合わせて
`409 fan_locked_by_feature`を返します。

起動直後は初期状態を`state_source: initial`として返します。送信後は`transmitted`、
VS1838Bで純正リモコンの280-bitフレームを受信した後は`received`になります。

```json
{
  "power": true,
  "mode": "cool",
  "temperature": 26,
  "dry_offset": null,
  "auto_offset": null,
  "fan": "auto",
  "swing": false,
  "sleep": false,
  "health": false,
  "comfort": false,
  "clean": false,
  "quiet": false,
  "timer": {"on": null, "off": null},
  "state_source": "initial"
}
```

### Daikin受信とreplay

```text
GET  /api/v1/ir/received
POST /api/v1/ir/replay
```

`/ir/received`は最後の280-bitフレームの受信時刻を返し、`/ir/replay`は最後に受信した
フレームを再送します。受信履歴がない場合、replayは`409 no_received_ir_state`です。

送信層はDaikinと照明で共有し、全送信に100msの間隔制御を適用します。Daikinは必要な
待ち時間を内部で待機し、照明の連続送信は`429 ir_rate_limited`になります。

## system API

認証は実装していません。LAN/VPN内だけで使用し、インターネットへ直接公開しないでください。

```text
GET /api/v1/health
GET /api/v1/system/health
GET /api/v1/system/info
```

`system/info`の`ir.protocols.light`はNEC 32-bit、`ir.protocols.aircon`はDAIKIN 280-bitを
示します。URLは存在するがHTTPメソッドが違う場合は`405 method_not_allowed`を返します。

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

現在は空パスワードのOpen Wi-Fiを意図的に拒否しています。WPA/WPA2等のパスワード付き
ネットワークを使用してください。

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

Wi-Fi接続後、Daikinタイマーの絶対時刻変換用に`time.google.com`と
`time.cloudflare.com`をNTPサーバーとして設定します。`pool.ntp.org`は使用しません。
Wi-FiパスワードはNVSに保存し、Basic認証も使用しません。ネットワーク境界はLAN/VPNで
管理してください。

シリアルでは`send on`または`light on`、`status`、`help`を使用できます。`status`は
照明とエアコンの完全なJSON状態を表示します。

## NEC送信のround-trip確認

`CeilingLightCodes.h`の値はVS1838Bで受信した32bit値を元にしていますが、IRremoteESP8266の
bit order差異を避けるため、コード変更時は次を確認してください。

1. VS1838BをGPIO5、IR LEDをGPIO4へ接続する。
2. 受信側で元のリモコンのコードを取得する。
3. APIから同じcommandを1回送信する。
4. 統合スケッチのシリアルログに`[DEBUG] NEC received code=...`が出て、同じNEC 32bit値になることを確認する。

APIの`acknowledged:false`は照明本体から応答がないことを明示しています。
