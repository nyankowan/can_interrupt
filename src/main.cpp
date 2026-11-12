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
// CAN1: ID 0x101 で速度司令を受信
struct VelCmd {
  int16_t vx;
  int16_t vy;
  int16_t omega;
  uint8_t count;
};
static volatile VelCmd velCmd = {0, 0, 0, 0};

// CAN2: RoboMaster モータ (C620/C610) のフィードバック, ID 0x201〜0x208
struct RobomasFb {
  uint16_t angle;    // 0〜8191
  int16_t  rpm;
  int16_t  current;  // トルク電流
  uint8_t  temp;
};
static constexpr uint8_t ROBOMAS_NUM = 4;
static volatile RobomasFb robomas[ROBOMAS_NUM];

// CAN受信割り込み内から1フレームごとに呼ばれる．ISRコンテキストなので
// 重い処理・Serial出力・delay は避けること．
static void onCanReceive(const CanFrame &f) {
  const uint8_t *rx_data = f.data;

  if (f.bus == 1) {
    if (!f.ext && f.id == 0x101) {
      velCmd.vx    = (int16_t)((rx_data[1] << 8) | rx_data[0]);
      velCmd.vy    = (int16_t)((rx_data[3] << 8) | rx_data[2]);
      velCmd.omega = (int16_t)((rx_data[5] << 8) | rx_data[4]);
      velCmd.count = rx_data[7];
    }
  } else {
    if (!f.ext && f.id >= 0x201 && f.id < 0x201 + ROBOMAS_NUM) {
      volatile RobomasFb &m = robomas[f.id - 0x201];
      m.angle   = (uint16_t)((rx_data[0] << 8) | rx_data[1]);
      m.rpm     = (int16_t)((rx_data[2] << 8) | rx_data[3]);
      m.current = (int16_t)((rx_data[4] << 8) | rx_data[5]);
      m.temp    = rx_data[6];
    }
  }
}
// ===================================================

// ---- クロック設定 ----
// Nucleo の ST-Link が出す 8MHz (HSE バイパス) から 180MHz を作る．
// 標準の HSI (±1%) だと 1Mbps の CAN には精度が足りないため上書きする．
// APB1 = 45MHz
extern "C" void SystemClock_Config(void) {
  RCC_OscInitTypeDef osc = {};
  RCC_ClkInitTypeDef clk = {};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_BYPASS;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = 8;    // 8MHz / 8 = 1MHz
  osc.PLL.PLLN = 360;  // 1MHz * 360 = 360MHz
  osc.PLL.PLLP = RCC_PLLP_DIV2;  // 180MHz
  osc.PLL.PLLQ = 2;
  osc.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) while (true) {}
  if (HAL_PWREx_EnableOverDrive() != HAL_OK) while (true) {}

  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV4;
  clk.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) while (true) {}
}

// 診断用: 割り込みで受信したフレーム数
static volatile uint32_t rxCount[2];

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
    rxCount[f.bus - 1]++;
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
    while (true) {Serial.println("CAN1 init failed");delay(1000);}
  }
  if (!canBegin(&hcan2, 2)) {
    Serial.println("CAN2 init failed");
    while (true) {Serial.println("CAN2 init failed");delay(1000);}
  }
  Serial.println("CAN1/CAN2 RX interrupt ready");
  Serial.printf("SYSCLK=%lu PCLK1=%lu\n", HAL_RCC_GetSysClockFreq(), HAL_RCC_GetPCLK1Freq());
}

// ---- P制御 (RoboMaster C620/C610, 速度制御) ----
static constexpr float   KP = 5.0f;            // [電流指令値 / rpm]
static constexpr int16_t CURRENT_LIMIT = 4000;  // 電流指令の上限 (C620: ±16384 = ±20A)
static constexpr uint8_t MOTOR_NUM = 4;         // 0x200 で制御できるのは ID 1〜4
static constexpr uint32_t CONTROL_PERIOD_MS = 1;

static int16_t targetRpm[MOTOR_NUM];
static int16_t outCurrent[MOTOR_NUM];

// CAN2 で RoboMaster に電流指令 (ID 0x200, 各モータ int16 ビッグエンディアン)
static void sendRobomasCurrent(const int16_t cur[MOTOR_NUM]) {
  if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan2) == 0) return;
  CAN_TxHeaderTypeDef hdr = {};
  hdr.StdId = 0x200;
  hdr.IDE = CAN_ID_STD;
  hdr.RTR = CAN_RTR_DATA;
  hdr.DLC = 8;
  uint8_t d[8];
  for (uint8_t i = 0; i < MOTOR_NUM; i++) {
    d[2 * i]     = (uint8_t)((uint16_t)cur[i] >> 8);
    d[2 * i + 1] = (uint8_t)((uint16_t)cur[i] & 0xFF);
  }
  uint32_t mailbox;
  HAL_CAN_AddTxMessage(&hcan2, &hdr, d, &mailbox);
}

void loop() {
  static uint32_t lastCtl = 0;
  static uint32_t lastPrint = 0;
  uint32_t now = millis();

  if (now - lastCtl >= CONTROL_PERIOD_MS) {
    lastCtl = now;

    // 目標値: 速度司令 vx をモータ1の目標 rpm とする (他は 0 rpm を保持)
    VelCmd v;
    int16_t rpm[MOTOR_NUM];
    //noInterrupts();
    v.vx = velCmd.vx;
    for (uint8_t i = 0; i < MOTOR_NUM; i++) rpm[i] = robomas[i].rpm;
    //interrupts();

    targetRpm[0] = v.vx*4;
    for (uint8_t i = 1; i < MOTOR_NUM; i++) targetRpm[i] = 500;

    for (uint8_t i = 0; i < MOTOR_NUM; i++) {
      float out = KP * (float)(targetRpm[i] - rpm[i]);
      if (out >  CURRENT_LIMIT) out =  CURRENT_LIMIT;
      if (out < -CURRENT_LIMIT) out = -CURRENT_LIMIT;
      outCurrent[i] = (int16_t)out;
    }
    sendRobomasCurrent(outCurrent);
  }

  if (now - lastPrint >= 1000) {  // 100ms ごとに出力
    lastPrint = now;

    VelCmd v;
    RobomasFb m[ROBOMAS_NUM];
    //noInterrupts();
    v.vx = velCmd.vx;
    v.vy = velCmd.vy;
    v.omega = velCmd.omega;
    v.count = velCmd.count;
    for (uint8_t i = 0; i < ROBOMAS_NUM; i++) {
      m[i].angle = robomas[i].angle;
      m[i].rpm = robomas[i].rpm;
      m[i].current = robomas[i].current;
      m[i].temp = robomas[i].temp;
    }
    //interrupts();

    Serial.printf("CMD vx=%d vy=%d omega=%d count=%u\n", v.vx, v.vy, v.omega, v.count);
    // 診断: ESR の REC(bit31-24)/TEC(bit23-16)/LEC(bit6-4)/BOFF(bit2)/EPVF(bit1)/EWGF(bit0)
    Serial.printf("DIAG rx1=%lu rx2=%lu ESR1=0x%08lX ESR2=0x%08lX err1=0x%lX err2=0x%lX\n",
                  rxCount[0], rxCount[1], CAN1->ESR, CAN2->ESR,
                  HAL_CAN_GetError(&hcan1), HAL_CAN_GetError(&hcan2));
    for (uint8_t i = 0; i < MOTOR_NUM; i++) {
      Serial.printf("M%u target=%d rpm=%d out=%d cur=%d temp=%u\n", i + 1, targetRpm[i], m[i].rpm, outCurrent[i], m[i].current, m[i].temp);
    }
  }
}
