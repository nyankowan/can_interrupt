# can_interrupt

Nucleo-F446RE（Arduino フレームワーク / PlatformIO）で，CAN1 と CAN2 を**割り込みで受信**し，RoboMaster モータを P 制御で速度制御するサンプル．
コードはすべて `src/main.cpp` に入っている．受信データはバッファに保存せず，フレームを受信するたびに割り込み内からユーザー関数を呼び出す．

## 構成

| バス | 役割 |
|---|---|
| CAN1 | 上位からの速度司令（ID 0x101）を受信 |
| CAN2 | RoboMaster のフィードバックを受信し，電流指令（ID 0x200）を送信 |

## 仕様

| 項目 | 内容 |
|---|---|
| ボード | nucleo_f446re |
| クロック | HSE 8MHz（ST-Link から供給，バイパス）を PLL で 180MHz に逓倍，APB1=45MHz |
| ビットレート | CAN1 / CAN2 とも 1Mbps |
| タイミング | Prescaler 3, BS1=11TQ, BS2=3TQ（サンプル点 80%） |
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

- 各 CAN ピンには CAN トランシーバ（SN65HVD230 など）が必要．
- 1Mbps ではバス両端に終端抵抗（120Ω）を付けること．
- CAN2 はスレーブペリフェラルのため，CAN1 のクロックも有効化している（フィルタバンクは CAN1 が 0〜13，CAN2 が 14〜27）．

## 受信

`onCanReceive` が，フレームを受信するたびに割り込み内から呼ばれる．

### CAN1: 速度司令（ID 0x101）

| バイト | 内容 |
|---|---|
| 0-1 | vx（int16, リトルエンディアン） |
| 2-3 | vy（int16, リトルエンディアン） |
| 4-5 | omega（int16, リトルエンディアン） |
| 7 | count（uint8） |

受信値はグローバル変数 `velCmd` に保存される．

### CAN2: RoboMaster フィードバック（ID 0x201〜0x208）

C620/C610 のフィードバックを想定し，ID 0x201〜0x208 をモータ 1〜8 として，グローバル配列 `robomas[0〜7]` に保存する．

| バイト | 内容 |
|---|---|
| 0-1 | 角度（uint16, ビッグエンディアン, 0〜8191） |
| 2-3 | 回転数 rpm（int16, ビッグエンディアン） |
| 4-5 | トルク電流（int16, ビッグエンディアン） |
| 6 | 温度（uint8） |

### 割り込み内の注意

- `Serial` 出力や `delay` など時間のかかる処理は避けること（取りこぼしの原因になる）．
- `velCmd` や `robomas` を `loop()` で読むときは，複数フィールドの値がずれないように，割り込みを一時的に止めてコピーすること（`noInterrupts()` / `interrupts()`）．

## 速度制御（P 制御）

`loop()` で 1ms ごとに，モータ 1〜4 を P 制御する．

- 誤差: `目標rpm − 現在rpm`
- 出力: `KP × 誤差`（`±CURRENT_LIMIT` で制限）
- 目標値: CAN1 の速度司令 `vx` をモータ 1 の目標 rpm とする（モータ 2〜4 は 0 rpm を保持）
- 出力先: CAN2 の ID 0x200 に，各モータの電流指令を int16（ビッグエンディアン）で送信

| 定数 | 初期値 | 内容 |
|---|---|---|
| `KP` | 5.0 | 比例ゲイン．大きすぎると振動する |
| `CURRENT_LIMIT` | 4000 | 電流指令の上限（C620 は ±16384 が ±20A，C610 は ±10000） |

### 注意

- P 制御のみなので，定常偏差が残る．
- 速度司令が途切れても，目標は保持されたままになる（タイムアウトによる停止は未実装）．

## シリアル出力

115200bps で，100ms ごとに次を出力する．

```
CMD vx=.. vy=.. omega=.. count=..
DIAG rx1=.. rx2=.. ESR1=0x.. ESR2=0x.. err1=0x.. err2=0x..
M1 target=.. rpm=.. out=.. cur=.. temp=..
...
```

- `DIAG` は受信フレーム数（`rx1`，`rx2`）と，CAN のエラー状態（`ESR`，`err`）である．
- `ESR` は bit31-24 が受信エラー数，bit23-16 が送信エラー数，bit6-4 が最後のエラー種別，bit2 が bus-off を表す．
- 起動時に `SYSCLK` と `PCLK1` を出力する．`SYSCLK=180000000`，`PCLK1=45000000` になっていること．

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
| `-DSERIAL_TX_BUFFER_SIZE=1024` | シリアル出力で制御ループが止まらないようにする |
| `upload_protocol = stlink` | ST-Link で書き込む |
| `monitor_speed = 115200` | モニタのボーレート |

書き込み時に権限エラーが出る場合は，`99-platformio-udev.rules` をインストールすること．

## 設定の変更

- **ビットレート**: `canBegin()` 内の `Prescaler` / `TimeSeg1` / `TimeSeg2` を変更する．`APB1 / Prescaler / (1+BS1+BS2)` がビットレートになる．
- **ループバックで動作確認**: `h->Init.Mode` を `CAN_MODE_LOOPBACK` にする（自分が送ったフレームだけ受信できる）．
- **CAN2 のピン**: PB5/PB6 に変える場合は `canBegin()` の GPIO 設定を差し替える．
- **目標値の割り当て**: `loop()` 内の `targetRpm` の計算を変更する．
- **クロック**: `SystemClock_Config` を変更した場合は，APB1 の周波数に合わせてビットレートの計算をやり直すこと．
