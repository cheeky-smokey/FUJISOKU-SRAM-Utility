// カードの抜き差し (/CARD DET = D2) と電池電圧 (BATT-DET = A2)
//
// /CARD DET (カードの F37): カードが入ると Low になる (CARD_DET_ACTIVE_LOW)。D2 は内蔵プルアップの入力。
//   50 ms 同じ値が続いたら抜き差しとみなし、"E CARD 1" / "E CARD 0" を PC に送る。
//   カードが無いとき、カードを使うコマンドは ERR NOCARD を返す (protocol.ino の dev_ready())。
//   処理中に抜かれたら op_poll_cancel() が気づいて止める (ERR NOCARD Card removed ...)。
// BATT-DET (カードの F2): カードの電池の電圧。A2 で 0.5 秒ごとに測る (12 ビット、8 回の平均)。
//   A2 と GND の間の 1 MΩ で、電池が無いときは 0 V 近くになる (無いと漏れ電流で 3〜4 V に浮く)。
//   BATT_NONE_MV 以下 = 電池なし、BATT_LOW_MV 以下 = 少ない。
//   50 mV 以上変わったときと、この境目をまたいだときに "E BATT <mV>" を送る。
//   カードが無いときは A2 が浮いているので測らない (batt_mv = -1)。
// USE_CARD_DET = 0 なら、いつもカードが入っているものとして扱う。

#define CARD_DEBOUNCE_MS 50
#define BATT_PERIOD_MS 500
#define BATT_SETTLE_MS 100     // カードを挿してから最初に測るまで
#define BATT_REPORT_STEP_MV 50
#define ADC_MAX 4095           // 12 ビット

// 今この瞬間、カードが入っているか (チャタリング除去なし)
bool card_now() {
#if USE_CARD_DET
  return digitalRead(PIN_CARD_DET) == (CARD_DET_ACTIVE_LOW ? LOW : HIGH);
#else
  return true;
#endif
}

// 電池が無いか (BATT_NONE_MV 以下)
bool batt_none() {
  return batt_mv >= 0 && batt_mv <= BATT_NONE_MV;
}

// 電池の警告中か (少ない、または無い)
bool batt_low() {
  return batt_mv >= 0 && batt_mv <= BATT_LOW_MV;
}

#if USE_BATT_DET
static uint32_t batt_next_ms = 0;
static int16_t batt_sent_mv = -1;  // 最後に PC に送った値

static int16_t batt_measure_mv() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 8; i++) sum += (uint32_t)analogRead(PIN_BATT_DET);
  const uint32_t div = 8UL * ADC_MAX;
  return (int16_t)((sum * BATT_ADC_FULL_MV + div / 2) / div);
}

static void batt_report() {
  Serial.print("E BATT ");
  Serial.println(batt_mv);
  batt_sent_mv = batt_mv;
}

static void batt_poll(uint32_t now) {
  if (!card_inserted) {
    batt_mv = -1;
    batt_sent_mv = -1;
    return;
  }
  if ((int32_t)(now - batt_next_ms) < 0) return;
  batt_next_ms = now + BATT_PERIOD_MS;
  bool was_low = batt_low(), was_none = batt_none();
  batt_mv = batt_measure_mv();
  int16_t diff = batt_mv - batt_sent_mv;
  if (batt_sent_mv < 0 || diff >= BATT_REPORT_STEP_MV || diff <= -BATT_REPORT_STEP_MV || batt_low() != was_low || batt_none() != was_none)
    batt_report();
}
#endif

void card_begin() {
#if USE_CARD_DET
  pinMode(PIN_CARD_DET, INPUT_PULLUP);
  delay(2);  // プルアップで電圧が上がるのを待つ
  card_inserted = card_now();
#endif
#if USE_BATT_DET
  analogReadResolution(12);
  if (card_inserted) batt_mv = batt_measure_mv();
  batt_next_ms = millis() + BATT_PERIOD_MS;
#endif
}

// loop() から繰り返し呼ぶ
void card_poll() {
  uint32_t now = millis();
#if USE_CARD_DET
  static bool raw_last = true;
  static uint32_t raw_ms = 0;
  bool raw = card_now();
  if (raw != raw_last) {
    raw_last = raw;
    raw_ms = now;
  } else if (raw != card_inserted && now - raw_ms >= CARD_DEBOUNCE_MS) {
    card_inserted = raw;
    Serial.print("E CARD ");
    Serial.println(raw ? 1 : 0);
#if USE_BATT_DET
    batt_mv = -1;
    batt_sent_mv = -1;
    batt_next_ms = now + BATT_SETTLE_MS;
#endif
  }
#endif
#if USE_BATT_DET
  batt_poll(now);
#else
  (void)now;
#endif
}

// 処理中にカードが抜かれたか (op_poll_cancel() から呼ぶ。3 回続けて無ければ抜かれたとみなす)
bool card_check_lost() {
#if USE_CARD_DET
  static uint8_t gone = 0;
  if (card_now()) gone = 0;
  else if (gone < 3) gone++;
  return gone >= 3;
#else
  return false;
#endif
}
