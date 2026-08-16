# daikin-an22nesj-w シリアル操作

このディレクトリのスケッチで、ARC446A3のシリアル操作と赤外線受信・再送を行います。
配線、コンパイル、書き込み、シリアルモニタの手順はリポジトリのルートREADMEを参照してください。

## シリアルコマンド

起動時は赤外線を送信しません。シリアルモニタから次のコマンドでテストできます。

対象リモコンは`ARC446A3`、プロトコルは`DAIKIN 280-bit`です。

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
health on
health off
comfort on
comfort off
clean on
clean off
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

赤外線は片方向通信なので、`[INFO] IR frame sent`はESP32がフレームを出力したことだけを示します。
エアコン本体の受光音・運転ランプ・風の変化で実機の受信を確認してください。

VS1838Bで純正リモコンのボタンを押すと、シリアルモニタに`IR frame received`、プロトコル、rawデータが
表示されます。まず純正リモコンの`運転/停止`または`冷房`をエアコンへ向けずVS1838Bに向けて押し、
受信できることを確認します。その後`replay`を入力すると、受信した純正リモコンの状態をそのまま
GPIO4から再送できます。

`raw_on` / `raw_off`は、純正リモコンから取得したON/OFFフレームをそのまま送る診断用コマンドです。
`burst_on` / `burst_off`は同じフレームを3回送り、`inv_on` / `inv_off`は送信極性を反転して3回送ります。

純正リモコンの未知ボタンをVS1838Bへ向けて押すと、前回のDAIKINフレームとの差分が
`[DEBUG] diff byte[...]`として表示されます。快眠・健康冷房などは、この差分を確認してから専用コマンドへ
割り当てます。

## ARC446A3調査情報

- 快眠: `byte[29]`の`0x04`。`sleep on/off`で操作できます。
- 健康冷房: `byte[29]`の`0x08`。`health on/off`で操作できます。
- 内部クリーン: `byte[6]`の`0x08`。`clean on/off`で操作できます。
- 風向: `byte[24]`下位4ビット。`0x00`が停止、`0x0F`がスイングです。
- 風量: `byte[24]`上位4ビット。`0xA`が自動、`0xB`が静音、`0x3`〜`0x7`が風量1〜5です。

風量は`fan auto|quiet|1..5`で操作できます。

入タイマーを1回押した状態では、`byte[21]`の入タイマービットがONになり、`byte[26]〜[28]`が
120分（2:00）を示しました。`timer-on 120`で生成できます。

その状態で切タイマーを1回押すと切タイマービットもONになり、切タイマーは60分（1:00）になりました。
`timer-off 60`で生成できます。

取消を押すと両タイマーのビットがOFFになり、時刻フィールドも無効値へ戻ります。`timer-cancel`で
同じ状態を生成できます。
