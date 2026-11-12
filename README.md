# can_interrupt

Nucleo-F446RE（Arduino フレームワーク / PlatformIO）で，CAN1 と CAN2 を**割り込みで受信**するサンプル．
コードはすべて `src/main.cpp` に入っている．受信データはバッファに保存せず，フレームを受信するたびに割り込み内からユーザー関数を呼び出す．

## 仕様

| 項目 | 内容 |
|---|---|
| ボード | nucleo_f446re |
| クロック | HSE 8MHz（ST-Link から供給，バイパス）を PLL で 180MHz に逓倍，APB1=45MHz |
| ビットレート | CAN1 / CAN2 とも 1Mbps |
| タイミング | APB1=45MHz, Prescaler 3, BS1=11TQ, BS2=3TQ（サンプル点 80%） |
| 受信 | FIFO0 / 全ID受信（標準ID・拡張ID） |
| 割り込み | `CAN1_RX0_IRQn` / `CAN2_RX0_IRQn`（優先度 5） |

### クロックについて

標準のクロック設定は内蔵 RC 発振器（HSI）で，誤差が約 ±1% ある．1Mbps の CAN ではこの精度では受信できなかったため，`SystemClock_Config` を上書きして HSE を使っている．
PLL は 8MHz ÷ 8 × 360 ÷ 2 = 180MHz である．
Nucleo では ST-Link が 8MHz を供給するので，水晶の追加は不要．ST-Link からの供給を外した基板では，HSE が起動せず止まるので注意すること．

## ピン配置

| 機能 | ピン |
|---|---|
| CAN1 RX / TX | PA11 / PA12 |
| CAN2 RX / TX | PB12 / PB13 |
| シリアル（USART2, ST-Link 仮想 COM） | PA2 / PA3 |

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
pio run                      # ビルド
pio run -t upload            # 書き込み（ST-Link）
pio device monitor           # シリアルモニタ（115200bps）
```

`platformio.ini` の設定は次のとおり．

| 設定 | 内容 |
|---|---|
| `-DHAL_CAN_MODULE_ENABLED` | Arduino コアで HAL の CAN モジュールを有効にする |
| `-DSERIAL_UART_INSTANCE=2` | `Serial` を USART2（ST-Link 仮想 COM）にする |
| `upload_protocol = stlink` | ST-Link で書き込む |
| `monitor_speed = 115200` | モニタのボーレート |

書き込み時に権限エラーが出る場合は，`99-platformio-udev.rules` をインストールすること．

## 設定の変更

- **ビットレート**: `canBegin()` 内の `Prescaler` / `TimeSeg1` / `TimeSeg2` を変更する．`APB1 / Prescaler / (1+BS1+BS2)` がビットレートになる．
- **ループバックで動作確認**: `h->Init.Mode` を `CAN_MODE_LOOPBACK` にする（自分が送ったフレームだけ受信できる．送信コードは含まれていない）．
- **CAN2 のピン**: PB5/PB6 に変える場合は `canBegin()` の GPIO 設定を差し替える．
- **クロック**: `SystemClock_Config` を変更した場合は，APB1 の周波数に合わせてビットレートの計算をやり直すこと．
