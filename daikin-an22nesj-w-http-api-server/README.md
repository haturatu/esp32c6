# Daikin AN22NESJ-W HTTP API

ESP32-C6からDaikin AN22NESJ-WをHTTP経由で操作するための別スケッチです。
既存のシリアル操作用スケッチには手を入れず、HTTP API用の責務をこのディレクトリに分離します。

対象ハードウェアとIR仕様は次のとおりです。

- ESP32-C6
- Daikin AN22NESJ-W
- リモコン ARC446A3
- DAIKIN 280-bit
- IR LED送信 GPIO4
- VS1838B受信 GPIO5

## セキュリティとWi-Fi provisioning

Wi-Fi SSIDとパスワードはソースコード、README、`.ino`のビルド時定数に含めません。
ESP32のPreferences/NVSの`secrets` namespaceへ、USBシリアル経由で保存します。

```text
PCの環境変数
    ↓
provision.py
    ↓ USB Serial
ESP32-C6 Preferences/NVS
    ↓
Wi-Fi接続
```

実値を入れた`.env`はローカルだけに置き、Gitへ追加しないでください。
雛形は[`secrets.example.env`](secrets.example.env)です。

Python側の依存をインストールします。

```bash
python3 -m pip install pyserial
```

API用ファームウェアを書き込んだ後、ユーザー環境の値をシェルから渡してprovisioningします。
値はこのリポジトリへ記録しません。

```bash
export WIFI_SSID='your-ssid'
export WIFI_PASSWORD='your-wifi-password'
python3 provision.py \
  --port /dev/ttyACM0 \
  --ssid "$WIFI_SSID" \
  --password "$WIFI_PASSWORD"
```

ESP32側は次の行プロトコルを受け付けます。値はスクリプトがUTF-8 hexへ変換するため、
パスワード中の空白や記号がシリアル行の区切りと衝突しません。

```text
PROVISION1 <hex-ssid> <hex-password>
```

保存後、ESP32は再起動してNVSから読み込みます。Wi-Fi情報が未設定、または接続に失敗した場合は
HTTPサーバーを起動せず、シリアルから再provisioningできます。

時刻同期には`time.google.com`と`time.cloudflare.com`を使用します。

Basic認証は実装しません。HTTP APIは信頼できるLANまたはVPN内だけで使用し、インターネットへ
直接公開しないでください。将来、異なる信頼境界で使う場合はTLS終端プロキシまたはBearer tokenを
追加する設計余地を残します。

## ビルドと書き込み

リポジトリのルートで実行します。`IRremoteESP8266`は既存のArduinoライブラリを利用します。

```bash
arduino-cli compile --jobs 4 \
  --build-path .build/daikin-http-api-fast \
  --fqbn 'esp32:esp32:esp32c6:CDCOnBoot=cdc' \
  --build-property 'compiler.cpp.extra_flags=-D_IR_ENABLE_DEFAULT_=false -DSEND_DAIKIN=true -DDECODE_DAIKIN=true' \
  daikin-an22nesj-w-http-api-server
```

書き込みは、実際のポートを確認してから行います。

```bash
arduino-cli upload -p /dev/ttyACM0 \
  --build-path .build/daikin-http-api-fast \
  --fqbn 'esp32:esp32:esp32c6:CDCOnBoot=cdc' \
  daikin-an22nesj-w-http-api-server
```

GitHubへのpushは、変更内容を確認したうえで明示的に行ってください。

## APIの基本方針

Base pathは`/api/v1`です。HTTP APIはシリアルコマンドを単純に置き換えるのではなく、
エアコンの状態をリソースとして扱います。

```text
/api/v1/ac/*       通常のエアコン操作
/api/v1/ir/*       IR受信・再送・診断
/api/v1/system/*   ESP32本体の状態
```

Daikin IRは片方向でACKがないため、APIが返すstateはエアコン本体から読み取った実状態では
ありません。最後にESP32が送信した、またはVS1838Bが純正リモコンから受信した状態です。

`state_source`の値は次の意味です。

- `initial`: 起動時の初期状態
- `transmitted`: ESP32が最後に送信した状態
- `received`: VS1838Bが最後に受信した状態

PATCHは全フィールドを検証してから候補状態へ一括適用し、完成したDAIKIN 280-bitフレームを
1回だけ送信します。不正なフィールドがあれば内部状態も送信内容も変更しません。

## API一覧

| Method | Path | 認証 | 用途 |
| --- | --- | --- | --- |
| GET | `/api/v1/system/health` | 不要 | 死活監視 |
| GET | `/api/v1/ac/state` | 不要 | 保持しているAC状態 |
| PATCH | `/api/v1/ac/state` | 不要 | 状態の部分更新とIR送信 |
| GET | `/api/v1/ir/received` | 不要 | 最後の受信フレーム情報 |
| POST | `/api/v1/ir/replay` | 不要 | 最後に受信したフレームの再送 |
| POST | `/api/v1/ir/diagnostics/send` | 不要 | 診断用。現在は無効 |
| GET | `/api/v1/system/info` | 不要 | ESP32とIRの情報 |

認証を行わないため、ルーターのポート開放や外部公開は禁止です。

## GET `/api/v1/system/health`

```json
{"status":"ok"}
```

常に`200 OK`を返す軽量な死活監視用エンドポイントです。

## GET `/api/v1/ac/state`

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

`temperature`は通常モードでは摂氏10〜32度です。`mode`は`auto`、`cool`、`heat`、`dry`、`fan`、
`fan`は`auto`、`quiet`、`1`〜`5`です。`timer`は現在時刻からの相対時間で、0〜720分
（最大12時間）です。無効時は`null`です。

除湿（`mode: "dry"`）では温度設定は使わず、`temperature`は`null`になります。
代わりに`dry_offset`で除湿補正を表し、値は`-2`、`-1`、`0`、`1`、`2`です。
除湿以外のモードでは`dry_offset`は`null`です。

自動（`mode: "auto"`）では温度設定は使わず、`temperature`は`null`になります。
代わりに`auto_offset`で自動運転の温度補正を表し、値は`-5`〜`+5`です。
自動以外のモードでは`auto_offset`は`null`です。

```json
{
  "mode": "dry",
  "temperature": null,
  "dry_offset": -1
}
```

```json
{
  "mode": "auto",
  "temperature": null,
  "auto_offset": -3
}
```

DaikinのIRフレーム内部ではタイマーが「0時からの絶対時刻」として保存されますが、APIでは
リモコン表示に合わせて「何分後」を返します。受信フレームの現在時刻を基準にし、未受信時は
ESP32の時計を基準にして送信時に相互変換します。

### リモコンと同じ風量制約

純正リモコンの制約に合わせ、`health: true`（健康冷房）または
`comfort: true`（風ないス）が有効な間は、`fan`を変更できません。該当するPATCHは
`409 Conflict`で拒否し、内部状態と赤外線送信内容は変更しません。

```json
{
  "error": {
    "code": "fan_locked_by_feature",
    "message": "fan cannot be changed while health or comfort is enabled"
  }
}
```

風量を変更する場合は、先に該当機能を無効化してください。`health: false`または
`comfort: false`と`fan`を同じPATCHで指定した場合は、変更後に制約が解除されるため許可します。

## PATCH `/api/v1/ac/state`

Content-Typeは`application/json`です。次のフィールドを任意の組み合わせで指定できます。

```json
{
  "power": true,
  "mode": "cool",
  "temperature": 26,
  "dry_offset": null,
  "auto_offset": null,
  "fan": "auto",
  "swing": true,
  "sleep": true,
  "health": false,
  "comfort": false,
  "clean": false,
  "quiet": false,
  "timer": {"on": 120, "off": null}
}
```

`mode`を指定すると運転開始状態になります。明示的な`power`が同時にある場合は、指定された
`power`を最後に適用します。`timer.on`と`timer.off`は現在時刻からの相対分数です。
タイマーは片方だけ、または両方を更新できます。720分を超える値は拒否します。

除湿の変更例:

```json
{"mode":"dry","dry_offset":-1}
```

自動の変更例:

```json
{"mode":"auto","auto_offset":-3}
```

除湿中に`temperature`を指定した場合、または除湿以外で`dry_offset`を指定した場合は
`422 Unprocessable Entity`で拒否します。自動中に`temperature`を指定した場合、または自動以外で
`auto_offset`を指定した場合も拒否します。除湿または自動から冷房・暖房へ変更すると、最後に使用した
通常温度を復元します。

成功レスポンス:

```json
{
  "ok": true,
  "state": {
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
    "state_source": "transmitted"
  }
}
```

## IR受信・再送

純正リモコンをVS1838Bへ向けて押すと、DAIKIN 280-bitだけを最後の受信状態として保持します。

```http
GET /api/v1/ir/received
```

受信済みの場合:

```json
{
  "received": true,
  "protocol": "DAIKIN",
  "bits": 280,
  "received_at_ms": 123456,
  "state_loaded": true
}
```

```http
POST /api/v1/ir/replay
```

受信履歴がない場合は`409 Conflict`、受信済みなら次のレスポンスを返します。

```json
{"ok":true,"ir":{"transmitted":true,"acknowledged":false}}
```

診断用raw送信は通常ビルドで無効です。必要な場合だけソースの
`kEnableIrDiagnostics`を明示的に変更して有効化します。本番の通常操作APIからraw frameを
渡せるようにはしません。

## 状態とIR実装の対応

HTTP利用者はraw byteを意識しません。内部では既存のARC446A3実機調査結果を使用します。

| API field | ARC446A3 / IR仕様 |
| --- | --- |
| `sleep` | `byte[29]` mask `0x04` |
| `health` | `byte[29]` mask `0x08` |
| `clean` | `byte[6]` mask `0x08` |
| `comfort` | IRremoteESP8266のComfort field（風ないス） |
| `swing` | `byte[24]`下位4ビット（縦スイング） |
| `fan` | `byte[24]`上位4ビット（自動、静音、1〜5） |
| `timer.on` | 入タイマー、現在時刻からの相対分数（最大720分） |
| `timer.off` | 切タイマー、現在時刻からの相対分数（最大720分） |

## curl例

```bash
curl http://esp32-ac.local/api/v1/system/health

curl http://esp32-ac.local/api/v1/ac/state

curl -X PATCH \
  -H 'Content-Type: application/json' \
  -d '{"power":true,"mode":"cool","temperature":26,"fan":"auto"}' \
  http://esp32-ac.local/api/v1/ac/state

curl -X PATCH \
  -H 'Content-Type: application/json' \
  -d '{"timer":{"on":120,"off":null}}' \
  http://esp32-ac.local/api/v1/ac/state
```

## エラー

レスポンスは次の形式に統一します。

```json
{"error":{"code":"invalid_temperature","message":"temperature must be between 10 and 32"}}
```

実装する主なHTTP statusは次のとおりです。

| Status | 用途 |
| --- | --- |
| 200 | 成功 |
| 400 | JSON構文エラー |
| 404 | 未知のendpoint、無効化された診断API |
| 405 | 既知endpointへのHTTPメソッド違い |
| 409 | replay対象の受信履歴なし |
| 413 | 2048 bytesを超えるリクエスト |
| 422 | 値・フィールドのvalidation失敗 |
| 500 | 将来の内部エラー |

## 実装上の制約

- WebServerは単一ループで処理し、IR送信を並列実行しません。
- IR送信間隔は最低100ms空けます。
- HTTP bodyは2048 bytesまでです。
- CORSは有効化しません。
- Wi-Fiパスワード、SSID、NVSの内容はAPIレスポンスへ返しません。
- Flash Encryption/Secure Boot/NVS暗号化は別途ESP32の本番セキュリティ設定として検討します。
