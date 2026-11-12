# can_interrupt

STM32F446RE（Arduino フレームワーク / PlatformIO）で，CAN1 と CAN2 を**割り込みで受信**するサンプル．
コードはすべて `src/main.cpp` に入っている．受信データはバッファに保存せず，フレームを受信するたびに割り込み内からユーザー関数を呼び出す．

## 仕様

| 項目 | 内容 |
|---|---|
| ボード | nucleo_f446re |
| ビットレート | CAN1 / CAN2 とも 1Mbps |
| タイミング | APB1=45MHz, Prescaler 3, BS1=11TQ, BS2=3TQ（サンプル点 80%） |
| 受信 | FIFO0 / 全ID受信（標準ID・拡張ID） |
| 割り込み | `CAN1_RX0_IRQn` / `CAN2_RX0_IRQn`（優先度 5） |

## ピン配置

| バス | RX | TX |
|---|---|---|
| CAN1 | PA11 | PA12 |
| CAN2 | PB12 | PB13 |

- 各ピンには CAN トランシーバ（SN65HVD230 など）が必要．
- 1Mbps ではバス両端に終端抵抗（120Ω）を付けること．
- CAN2 はスレーブペリフェラルのため，CAN1 のクロックも有効化している（フィルタバンクは CAN1 が 0〜13，CAN2 が 14〜27）．

## 使い方

`src/main.cpp` のユーザー実装部にある `onCanReceive` が，フレームを受信するたびに呼ばれる．

### CAN1: 速度司令（ID 0x101）

| バイト | 内容 |
|---|---|
| 0-1 | vx（int16, リトルエンディアン） |
| 2-3 | vy（int16, リトルエンディアン） |
| 4-5 | omega（int16, リトルエンディアン） |
| 7 | count（uint8） |

受信値はグローバル変数 `velCmd` に保存される．

### CAN2: RoboMaster モータ（ID 0x201〜0x208）

C620/C610 のフィードバックを想定し，ID 0x201〜0x208 をモータ 1〜8 として，グローバル配列 `robomas[0〜7]` に保存する．

| バイト | 内容 |
|---|---|
| 0-1 | 角度（uint16, ビッグエンディアン, 0〜8191） |
| 2-3 | 回転数 rpm（int16, ビッグエンディアン） |
| 4-5 | トルク電流（int16, ビッグエンディアン） |
| 6 | 温度（uint8） |

### 注意

`onCanReceive` は**割り込みコンテキスト**で呼ばれる．

- `Serial` 出力や `delay` など時間のかかる処理は避けること（取りこぼしの原因になる）．
- 重い処理は，`volatile` のフラグだけ立てて `loop()` 側で行うこと．
- `velCmd` や `robomas` を `loop()` で読むときは，複数フィールドの値がずれないように，割り込みを一時的に止めてコピーすること（`noInterrupts()` / `interrupts()`）．

## ビルド・書き込み

```sh
pio run                 # ビルド
pio run -t upload       # 書き込み
```

`platformio.ini` の `build_flags = -DHAL_CAN_MODULE_ENABLED` は，Arduino コアで HAL の CAN モジュールを有効にするために必要．

## 設定の変更

- **ビットレート**: `canBegin()` 内の `Prescaler` / `TimeSeg1` / `TimeSeg2` を変更する．`APB1 / Prescaler / (1+BS1+BS2)` がビットレートになる．
- **ループバックで動作確認**: `h->Init.Mode` を `CAN_MODE_LOOPBACK` にする（自分が送ったフレームだけ受信できる．送信コードは含まれていない）．
- **CAN2 のピン**: PB5/PB6 に変える場合は `canBegin()` の GPIO 設定を差し替える．
- **クロック**: 180MHz 動作（APB1=45MHz）を前提としている．クロック設定を変えた場合は，ビットレートの計算をやり直すこと．
