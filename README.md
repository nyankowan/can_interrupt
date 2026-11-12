# can_interrupt

STM32F446RE（Arduino フレームワーク / PlatformIO）で，CAN1 と CAN2 を**割り込みで受信**するサンプル．
コードはすべて `src/main.cpp` に入っている．受信データはバッファに保存せず，フレームを受信するたびに割り込み内からユーザー関数を呼び出す．

## 仕様

| 項目 | 内容 |
|---|---|
| ボード | genericSTM32F446RE |
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

`src/main.cpp` の `onCanReceive` に受信時の処理を書く．

```cpp
static void onCanReceive(const CanFrame &f) {
  // f.bus  : 1 (CAN1) or 2 (CAN2)
  // f.id   : ID（標準/拡張）
  // f.ext  : 拡張IDなら 1
  // f.dlc  : データ長
  // f.data : データ(8byte)
}
```

### 注意

`onCanReceive` は**割り込みコンテキスト**で呼ばれる．

- `Serial` 出力や `delay` など時間のかかる処理は避けること（取りこぼしの原因になる）．
- 重い処理は，`volatile` のフラグだけ立てて `loop()` 側で行うこと．
- 受信データはバッファされないため，保存が必要な場合は `onCanReceive` 内で自分でコピーすること．

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
