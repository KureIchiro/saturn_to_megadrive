/*
 * セガサターン パッド -> メガドライブ 3/6ボタンパッド 変換アダプタ
 * Arduino Uno / Nano (ATmega328P, 5V / 16MHz)
 *
 * 2.ino からの変更点 (アップロードされた版の説明をそのまま残している)
 *  1. TH応答を INT0割り込み(約6us) から 割り込み禁止のポーリング(1us未満) に変更
 *     MDのゲームは THを書いてから 1〜2us 程度でデータを読むため、割り込みでは間に合わない
 *  2. Saturn読み出し等の裏の処理は、MDの読み出しが来ない時間帯だけで行う
 *     (読み出し直後 100us〜1.5ms の間、または 100ms 以上読み出しが無いとき)
 *  3. MD側出力を オープンドレイン(DDRC切替) から プッシュプル(PORTC) に変更
 *     Hi-Z -> High の立ち上がりがMD側の弱いプルアップ任せだと遅く、読み出しに間に合わない
 *  4. Mode出力 を Saturn の L、3/6ボタン切替予約 を L+R 同時押し に変更 (2.ino は どちらも R だった)
 *  5. TH に内部プルアップ (ゲームがTHを出力に設定する前の浮きを防ぐ)
 *
 * [LOG] ログ追加版 (この版で追加した部分には [LOG] を付けてある)
 *  ・割り込みを使わず、UARTのTXレジスタを直接使う。ログは裏の処理の時間帯にだけ、
 *    1回に1バイトずつ送る(待たない)ので、THへの応答時間は変わらない。
 *  ・TH立ち下がりの数え上げは、レジスタ上のローカル変数のインクリメント1命令だけ。
 *  ・ログの送信先は Nano の TX(D1) ピン。USB-シリアル変換の RXD につなぐ。
 *    115200bps 8N1。変換器の5Vは接続しない(GNDだけ共通にする)。
 *    変換器が3.3V専用なら、TXに分圧(例 1k+2k)を入れる。
 *  ・本体から給電中でも、USBを挿さずにログが見られる。
 *
 * 配線 (2.ino と同じ)
 *  Saturn側: SEL0=D4(PD4) SEL1=D5(PD5) DATA0..3=D8..D11(PB0..PB3) VCC=5V GND
 *  MD側    : Up=A0 Down=A1 Left=A2 Right=A3 pin6(B/A)=A4 pin9(C/Start)=A5 TH=D2(PD2)
 *            MD pin5 +5V -> 5V, pin8 GND -> GND
 *  LED(D13): 点灯=6ボタン / 消灯=3ボタン / 点滅=切替予約中
 *
 * [LOG] ログの見方
 *  起動時  : "[saturn_to_megadrive poll+log] start"
 *  定期/変化: B=xxxx F=xxxx K=xx m=ST r=ABCD t=HN,LN,LI,HX,AH
 *    B : 前回のログ以降の読み出し回数(THが動いた塊の数)
 *    F : 前回のログ以降のTH立ち下がり回数。0ならTHが検出できていない
 *    K : 1回の読み出しの最大の立ち下がり数 (1〜2: 3ボタン方式 / 3〜4: 6ボタン方式の読み出し)
 *    m : S=現在6ボタンモード(1)/3ボタンモード(0)、T=予約中のモード
 *    r : Saturnから読んだ生データ(押下=0)の16進。左から r0(R X Y Z) r1(右左下上) r2(St A C B) r3(L..)
 *        何も押していないときの正常値は FFFC
 *    t : MDに出す値(PORTC) HI_N, LO_N, LO_ID, HI_X, LO_AH (3F = 全部離し)
 *  イベント: "*Q"      THが100ms以上動かなかった(リセット/起動直後とみなした)
 *            "*MODE 6" / "*MODE 3"  予約したモードを反映した
 */

#include <Arduino.h>
#include <avr/io.h>

#define TH_BIT    0x04      // PD2
#define SEL_MASK  0x30      // PD4=SEL0, PD5=SEL1
#define LED_BIT   0x20      // PB5 (D13)

// Timer1: 分周64 -> 4us/tick。THが変化するたびに 0 に戻す (= 最後の変化からの経過時間)
#define BUSY_TICKS        25     // 100us  これより前は読み出し中とみなし、裏の処理をしない
#define COUNT_RESET_TICKS 375    // 1.5ms  6ボタンのカウンタを戻す。以降は次の読み出しに備えて待機
#define QUIET_TICKS       25000  // 100ms  リセットとみなし、予約モードを反映。裏の処理も再開
#define QUIET_CLAMP       60000  // 16bitカウンタ周回対策

#define BG_DIV    128       // ポーリング何回に1回裏の処理を1ステップ進めるか (約60us)
#define DEBOUNCE  8         // Lのチャタリング除去 (スキャン回数)

// ---------------------------------------------------------------- [LOG] ここから追加
#define LOG_ENABLE     0    // 0にするとログ関連の処理を止める
#define LOG_HEARTBEAT  512  // 裏の処理を何周したら定期ログを出すか (周期はMDの読み出し頻度で変わる)
#define LOG_MIN_GAP    8    // 値が変化したときのログの最小間隔 (周)
#define TX_BUF_SIZE    128  // 2の累乗
#define TX_MASK        (TX_BUF_SIZE - 1)

static uint8_t txBuf[TX_BUF_SIZE];
static uint8_t txHead = 0, txTail = 0;

static inline uint8_t txFree() { return (uint8_t)(txTail - txHead - 1) & TX_MASK; }

static void txPutc(char c) {
  if (txFree()) { txBuf[txHead] = (uint8_t)c; txHead = (txHead + 1) & TX_MASK; }
}
static void txPuts(const char* s) { while (*s) txPutc(*s++); }
static void txNib(uint8_t n)   { n &= 15; txPutc(n < 10 ? '0' + n : 'A' + n - 10); }
static void txHex8(uint8_t v)  { txNib(v >> 4); txNib(v); }
static void txHex16(uint16_t v){ txHex8(v >> 8); txHex8(v); }

// UARTが空いていれば1バイトだけ渡す。待たない
static inline void txPump() {
  if (txHead != txTail && (UCSR0A & (1 << UDRE0))) {
    UDR0 = txBuf[txTail];
    txTail = (txTail + 1) & TX_MASK;
  }
}

// setup()内だけで使う、待ってから送る版
static void txBlock(const char* s) {
  while (*s) {
    while (!(UCSR0A & (1 << UDRE0))) {}
    UDR0 = (uint8_t)*s++;
  }
}

static uint8_t  lastEdges = 0;
static uint16_t fallsTotal = 0, bursts = 0;
static uint8_t  maxBurst = 0;
// ---------------------------------------------------------------- [LOG] ここまで

// PORTCに書く値 (1=High=離されている)。bit0..5 = A0..A5 = Up Down Left Right pin6 pin9
enum { HI_N, LO_N, LO_ID, HI_X, LO_AH };
//  HI_N : TH=1             U D L R B C
//  LO_N : TH=0 (1,2回目)    U D 0 0 A St
//  LO_ID: TH=0 (3回目)     0 0 0 0 A St   (3ボタン時は LO_N と同じ)
//  HI_X : TH=1 (3回目の後) Z Y X M B C    (3ボタン時は HI_N と同じ)
//  LO_AH: TH=0 (4回目)     1 1 1 1 A St   (3ボタン時は LO_N と同じ)
static uint8_t tbl[5] = {0x3F, 0x33, 0x33, 0x3F, 0x33};

static inline uint8_t loFor(uint8_t c) {   // c回目の立ち下がりで出す値
  return (c == 3) ? tbl[LO_ID] : (c == 4) ? tbl[LO_AH] : tbl[LO_N];
}
static inline uint8_t hiFor(uint8_t c) {   // c回目の立ち下がりの後の立ち上がりで出す値
  return (c == 3) ? tbl[HI_X] : tbl[HI_N];
}

static inline void setSel(uint8_t s) {
  PORTD = (PORTD & ~SEL_MASK) | (s << 4);
}

// THが変化したら経過時間を 0 に戻し、100us後に裏の処理を許可する
static inline void restartQuietTimer() {
  TCNT1 = 0;
  OCR1A = BUSY_TICKS;
  TIFR1 = (1 << OCF1A);
}

// 裏の処理 (OCF1A がセットされている間だけ呼ばれる。1回に1ステップ)
// 戻り値: true = 6ボタンのカウンタを戻す
// [LOG] 引数 edges: loop()が数えているTH立ち下がり回数(8bitで周回)
static bool background(uint8_t edges) {
  static uint8_t step = 0;
  static uint8_t r0 = 0x0F, r1 = 0x0F, r2 = 0x0F, r3 = 0x0F;   // Saturn生データ (押下=0)
  static bool sixMode = false, pending = false, lStable = false;
  static uint8_t deb = 0, blink = 0;

#if LOG_ENABLE
  txPump();                                   // [LOG] UARTに1バイトだけ渡す
  {                                           // [LOG] 前回の呼び出し以降のTH立ち下がり数 = 読み出し1回分
    uint8_t d = (uint8_t)(edges - lastEdges);
    if (d) {
      lastEdges = edges;
      fallsTotal += d;
      ++bursts;
      if (d > maxBurst) maxBurst = d;
    }
  }
#endif

  uint16_t q = TCNT1;
  if (q < QUIET_TICKS) {
    if (q >= COUNT_RESET_TICKS) {
      // 1.5ms〜100ms: 次の読み出しがいつ来てもいいように何もしない。100ms後に再開
      OCR1A = QUIET_TICKS;
      TIFR1 = (1 << OCF1A);
      return true;
    }
  } else if (q > QUIET_CLAMP) {
    TCNT1 = QUIET_TICKS + 1;    // OCF1A はセットのまま
  }
  bool longQuiet = (q >= QUIET_TICKS);

#if LOG_ENABLE
  {                                           // [LOG] 100ms以上の無信号に入ったことを知らせる
    static bool wasLong = false;
    if (longQuiet && !wasLong && txFree() >= 8) txPuts("*Q\r\n");
    wasLong = longQuiet;
  }
#endif

  switch (step) {
    // Saturn読み出し: 前のステップでセットしたSELの値を読み、次のSELをセット
    // SEL=0: D3..D0 = R  X  Y  Z
    // SEL=1: D3..D0 = Rt Lt Dn Up
    // SEL=2: D3..D0 = St A  C  B
    // SEL=3: D3     = L
    case 0: r0 = PINB & 0x0F; setSel(1); step = 1; break;
    case 1: r1 = PINB & 0x0F; setSel(2); step = 2; break;
    case 2: r2 = PINB & 0x0F; setSel(3); step = 3; break;
    case 3: r3 = PINB & 0x0F; setSel(0); step = 4; break;

    case 4: {   // 出力テーブル作成 (r1 のビット並びが MD の U D L R と一致している)
      uint8_t bc  = (r2 & 0x03) << 4;     // B->pin6, C->pin9
      uint8_t ast = (r2 & 0x0C) << 2;     // A->pin6, Start->pin9
      uint8_t h = r1 | bc;
      uint8_t l = (r1 & 0x03) | ast;
      tbl[HI_N] = h;
      tbl[LO_N] = l;
      if (sixMode) {
        tbl[HI_X]  = (r0 & 0x07) | (r3 & 0x08) | bc;   // Z->Up Y->Down X->Left L(Mode)->Right
        tbl[LO_ID] = ast;
        tbl[LO_AH] = ast | 0x0F;
      } else {
        tbl[HI_X]  = h;
        tbl[LO_ID] = l;
        tbl[LO_AH] = l;
      }
      step = 5;
      break;
    }

    case 5: {   // L+R: 3/6ボタン切替の予約 (同時押しのたびにトグル)、リセット検出、LED
      bool combo = !(r3 & 0x08) && !(r0 & 0x08);   // L と R が両方押されている
      if (combo != lStable) {
        if (++deb >= DEBOUNCE) {
          lStable = combo;
          deb = 0;
          if (combo) pending = !pending;
        }
      } else {
        deb = 0;
      }
      if (longQuiet && sixMode != pending) {
        sixMode = pending;
#if LOG_ENABLE
        if (txFree() >= 16) txPuts(sixMode ? "*MODE 6\r\n" : "*MODE 3\r\n");   // [LOG]
#endif
      }

      bool led = sixMode;
      if (pending != sixMode) led = (++blink >> 6) & 1;
      if (led) PORTB |= LED_BIT; else PORTB &= ~LED_BIT;

#if LOG_ENABLE
      {                                       // [LOG] 定期ログ / 値が変わったときのログ
        static uint16_t lastSnap = 0xFFFF;
        static uint8_t  lastM = 0xFF, gap = 0;
        static uint16_t hbCnt = 0;
        uint16_t snap = (uint16_t)(r0 & 15) | ((uint16_t)(r1 & 15) << 4) |
                        ((uint16_t)(r2 & 15) << 8) | ((uint16_t)(r3 & 15) << 12);
        uint8_t mbits = (sixMode ? 2 : 0) | (pending ? 1 : 0);
        if (gap < 255) ++gap;
        bool changed = (snap != lastSnap) || (mbits != lastM);
        ++hbCnt;
        if ((hbCnt >= LOG_HEARTBEAT) || (changed && gap >= LOG_MIN_GAP)) {
          if (txFree() >= 64) {
            txPuts("B="); txHex16(bursts);
            txPuts(" F="); txHex16(fallsTotal);
            txPuts(" K="); txHex8(maxBurst);
            txPuts(" m="); txNib(sixMode); txNib(pending);
            txPuts(" r="); txNib(r0); txNib(r1); txNib(r2); txNib(r3);
            txPuts(" t="); txHex8(tbl[HI_N]);
            txPutc(',');   txHex8(tbl[LO_N]);
            txPutc(',');   txHex8(tbl[LO_ID]);
            txPutc(',');   txHex8(tbl[HI_X]);
            txPutc(',');   txHex8(tbl[LO_AH]);
            txPuts("\r\n");
            hbCnt = 0; gap = 0;
            lastSnap = snap; lastM = mbits;
            fallsTotal = 0; bursts = 0; maxBurst = 0;
          }
        }
      }
#endif
      step = 0;
      break;
    }
  }
  return false;
}

void setup() {
  // MD側: プッシュプル出力。初期値は全部離し(High)
  PORTC = 0x3F;
  DDRC  = 0x3F;

  // TH: 入力 + プルアップ
  DDRD  &= ~TH_BIT;
  PORTD |=  TH_BIT;

  // Saturn側
  DDRD  |= SEL_MASK;
  setSel(0);
  DDRB  &= ~0x0F;
  PORTB |=  0x0F;           // DATA0..3 プルアップ
// D12: 内部プルアップ (追加)
   DDRB  &= ~(1 << PB4);
   PORTB |=  (1 << PB4);

  DDRB  |=  LED_BIT;

  // Timer1: ノーマルモード、分周64 (Arduinoコアの PWM 設定を解除)
  TCCR1A = 0;
  TCCR1B = (1 << CS11) | (1 << CS10);
  TIMSK1 = 0;
  restartQuietTimer();

#if LOG_ENABLE
  // [LOG] UART0: 送信のみ、115200bps 8N1、割り込みなし
  UCSR0A = (1 << U2X0);
  UBRR0H = 0;
  UBRR0L = 16;                                 // 16MHz, U2X=1 -> 115200bps (誤差 約2%)
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
  UCSR0B = (1 << TXEN0);
  txBlock("\r\n[saturn_to_megadrive poll+log] start\r\n");
#endif
}

void loop() {
  cli();   // 以降、割り込みは一切使わない (loopから戻らない)

  uint8_t cnt = 0;          // THの立ち下がり回数 (6ボタン用)
  uint8_t edges = 0;        // [LOG] THの立ち下がり総数 (8bit周回。ログ用)
  uint8_t div = BG_DIV;
  uint8_t nextLo = loFor(1), nextHi;

  for (;;) {
    // TH=1 の間: 立ち下がりを待つ
    while (PIND & TH_BIT) {
      if ((TIFR1 & (1 << OCF1A)) && !--div) {
        div = BG_DIV;
        if (background(edges)) cnt = 0;
        nextLo = loFor(cnt + 1);
      }
    }
    PORTC = nextLo;           // 検出したら真っ先に出力
    restartQuietTimer();
    ++cnt;
    ++edges;                  // [LOG]
    nextHi = hiFor(cnt);

    // TH=0 の間: 立ち上がりを待つ
    while (!(PIND & TH_BIT)) {
      if ((TIFR1 & (1 << OCF1A)) && !--div) {
        div = BG_DIV;
        if (background(edges)) cnt = 0;
        nextHi = hiFor(cnt);
      }
    }
    PORTC = nextHi;
    restartQuietTimer();
    if (cnt >= 4) cnt = 0;
    nextLo = loFor(cnt + 1);
  }
}
