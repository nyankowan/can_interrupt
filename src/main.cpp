#include <Arduino.h>

// STM32F446RE / CAN1 (PA11=RX, PA12=TX), CAN2 (PB12=RX, PB13=TX) 1Mbps, FIFO0 受信割り込み
// APB1=45MHz -> 45M / 3 / (1+11+3) = 1Mbps (サンプル点 80%)
// ※ 各ピンにはCANトランシーバが必要
// ※ CAN2 はスレーブなので CAN1 のクロックも有効化が必要

static CAN_HandleTypeDef hcan1;
static CAN_HandleTypeDef hcan2;

struct CanFrame {
  uint8_t  bus;  // 1 or 2
  uint32_t id;
  uint8_t  ext;
  uint8_t  dlc;
  uint8_t  data[8];
};

// ================= ユーザー実装部 =================
// CAN受信割り込み内から1フレームごとに呼ばれる。ISRコンテキストなので
// 重い処理・Serial出力・delay は避けること。
static void onCanReceive(const CanFrame &f) {
  // ここに受信時の処理を書く
  (void)f;
}
// ===================================================

// ---- 割り込みハンドラ ----
extern "C" void CAN1_RX0_IRQHandler(void) {
  HAL_CAN_IRQHandler(&hcan1);
}

extern "C" void CAN2_RX0_IRQHandler(void) {
  HAL_CAN_IRQHandler(&hcan2);
}

extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h) {
  CAN_RxHeaderTypeDef hdr;
  CanFrame f;
  while (HAL_CAN_GetRxFifoFillLevel(h, CAN_RX_FIFO0) > 0) {
    if (HAL_CAN_GetRxMessage(h, CAN_RX_FIFO0, &hdr, f.data) != HAL_OK) break;
    f.bus = (h->Instance == CAN2) ? 2 : 1;
    f.ext = (hdr.IDE == CAN_ID_EXT);
    f.id  = f.ext ? hdr.ExtId : hdr.StdId;
    f.dlc = hdr.DLC;
    onCanReceive(f);
  }
}

static bool canBegin(CAN_HandleTypeDef *h, uint8_t bus) {
  GPIO_InitTypeDef g = {};
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = (bus == 1) ? GPIO_AF9_CAN1 : GPIO_AF9_CAN2;
  if (bus == 1) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin = GPIO_PIN_11 | GPIO_PIN_12;  // PA11=RX, PA12=TX
    HAL_GPIO_Init(GPIOA, &g);
    __HAL_RCC_CAN1_CLK_ENABLE();
  } else {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin = GPIO_PIN_12 | GPIO_PIN_13;  // PB12=RX, PB13=TX
    HAL_GPIO_Init(GPIOB, &g);
    __HAL_RCC_CAN1_CLK_ENABLE();        // CAN2 は CAN1 クロックが必須
    __HAL_RCC_CAN2_CLK_ENABLE();
  }

  h->Instance = (bus == 1) ? CAN1 : CAN2;
  h->Init.Prescaler = 3;
  h->Init.Mode = CAN_MODE_NORMAL;  // 動作確認だけなら CAN_MODE_LOOPBACK
  h->Init.SyncJumpWidth = CAN_SJW_1TQ;
  h->Init.TimeSeg1 = CAN_BS1_11TQ;
  h->Init.TimeSeg2 = CAN_BS2_3TQ;
  h->Init.TimeTriggeredMode = DISABLE;
  h->Init.AutoBusOff = ENABLE;
  h->Init.AutoWakeUp = DISABLE;
  h->Init.AutoRetransmission = ENABLE;
  h->Init.ReceiveFifoLocked = DISABLE;
  h->Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(h) != HAL_OK) return false;

  // 全IDを FIFO0 に受ける (フィルタバンク 0-13 は CAN1, 14-27 は CAN2)
  CAN_FilterTypeDef flt = {};
  flt.FilterBank = (bus == 1) ? 0 : 14;
  flt.FilterMode = CAN_FILTERMODE_IDMASK;
  flt.FilterScale = CAN_FILTERSCALE_32BIT;
  flt.FilterIdHigh = 0;
  flt.FilterIdLow = 0;
  flt.FilterMaskIdHigh = 0;
  flt.FilterMaskIdLow = 0;
  flt.FilterFIFOAssignment = CAN_RX_FIFO0;
  flt.FilterActivation = ENABLE;
  flt.SlaveStartFilterBank = 14;
  if (HAL_CAN_ConfigFilter(h, &flt) != HAL_OK) return false;

  IRQn_Type irq = (bus == 1) ? CAN1_RX0_IRQn : CAN2_RX0_IRQn;
  HAL_NVIC_SetPriority(irq, 5, 0);
  HAL_NVIC_EnableIRQ(irq);

  if (HAL_CAN_Start(h) != HAL_OK) return false;
  return HAL_CAN_ActivateNotification(h, CAN_IT_RX_FIFO0_MSG_PENDING) == HAL_OK;
}

void setup() {
  Serial.begin(115200);
  if (!canBegin(&hcan1, 1)) {
    Serial.println("CAN1 init failed");
    while (true) {}
  }
  if (!canBegin(&hcan2, 2)) {
    Serial.println("CAN2 init failed");
    while (true) {}
  }
  Serial.println("CAN1/CAN2 RX interrupt ready");
}

void loop() {
  
}
