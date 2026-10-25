# Saturn to Mega Drive Controller Adapter

セガサターンのコントローラーを、メガドライブ互換の3/6ボタンパッドとして使うためのArduinoスケッチです。
An Arduino sketch that lets you use a Sega Saturn controller as a Mega Drive-compatible 3-/6-button pad.

## 日本語

### 必要なもの

- Arduino Uno または Nano（ATmega328P、5 V、16 MHz）
- セガサターンのコントローラー用コネクターと、メガドライブ側のコネクター
- 接続用の配線

このスケッチは追加ライブラリを使いません。1台のArduinoを変換アダプターの中心にして、サターン側の信号を読み取り、メガドライブ側の信号を出力します。

### 配線

両コネクターの信号を、次の対応で同じArduinoに接続します。コネクターのピン番号は、お使いのコネクターの向き・ピン配置を確認してから配線してください。

| サターン側コネクタ | Arduino | メガドライブ側コネクタ | Arduino |
| --- | --- | --- | --- |
| 1 VCC | 5V | 1 Up | A0 |
| 9 GND | GND | 2 Down | A1 |
| 5 SEL0 | D4 | 3 Left | A2 |
| 4 SEL1 | D5 | 4 Right | A3 |
| 3 DATA0 | D8 | 6 B/A | A4 |
| 2 DATA1 | D9 | 9 C/Start | A5 |
| 8 DATA2 | D10 | 7 TH | D2 |
| 7 DATA3 | D11 | 5 +5V | 5V |
| 6 TL | 未接続 | 8 GND | GND |

両側のGNDをArduinoのGNDに接続してください。メガドライブ側のUp/Down/Left/Right、B/A、C/Start信号はArduinoのA0〜A5から出力されます。メガドライブ側の+5VはArduinoの5Vへ接続します。

### 書き込みと使用

1. Arduino IDEで `saturn_to_megadrive.ino` を開きます。
2. ボードにUnoまたはNano（ATmega328P、5 V / 16 MHz）を選び、該当するポートを選択します。古いNanoでは、必要に応じて「プロセッサ」から旧ブートローダーを選んでください。
3. スケッチを書き込みます。
4. 配線したサターンのコントローラーを接続し、メガドライブ本体にアダプターを接続して使用します。

起動時は3ボタンモードです。サターンの方向キーとボタンは次のように出力されます。

- 方向キー → Up / Down / Left / Right
- B、C → メガドライブのB、C
- A、Start → メガドライブのA、Start
- 6ボタンモード時のみ、サターンのX、Y、Z、L（Mode）→ メガドライブの追加ボタンX、Y、Z、Mode

3/6ボタンモードを切り替えるには、サターンのL（Mode）とRを同時に押して切替を予約し、その後メガドライブ本体のリセットボタンを押してください。リセット中にTH信号が100 ms以上変化しないことを検出すると、予約したモードが反映されます。ArduinoのD13 LEDは、3ボタンモードで消灯、6ボタンモードで点灯、切替予約中は点滅します。

### 任意のシリアルログ

ログは初期設定で無効です。ログを使う場合は、スケッチ内の `#define LOG_ENABLE 0` を `#define LOG_ENABLE 1` に変更して、再度書き込んでください。

- ArduinoのTX（D1）をUSB-シリアル変換器のRXDへ接続します。
- Arduinoと変換器のGNDを共通にします。変換器の5V出力は接続しないでください。
- シリアル設定は115200 baud、8N1です。
- 3.3 V専用の変換器では、TX信号を分圧するなどして入力電圧を調整してください。

ログの `r` はサターンから読み取った生データ（押下=0）で、何も押していないときの通常値は `FFFC` です。`m` は現在のモードと予約モード、`t` はメガドライブ側へ出力する値を表します。ログの詳細はスケッチ先頭の「ログの見方」を参照してください。

## English

### Requirements

- Arduino Uno or Nano (ATmega328P, 5 V, 16 MHz)
- Connectors for the Saturn controller and Mega Drive controller port
- Wiring for the connections

The sketch has no external library dependencies. One Arduino acts as the adapter: it reads the Saturn controller signals and drives the Mega Drive signals.

### Wiring

Connect both sides to the same Arduino according to this table. Confirm the connector pin numbering and orientation for your particular connectors before wiring.

| Saturn connector | Arduino | Mega Drive connector | Arduino |
| --- | --- | --- | --- |
| 1 VCC | 5V | 1 Up | A0 |
| 9 GND | GND | 2 Down | A1 |
| 5 SEL0 | D4 | 3 Left | A2 |
| 4 SEL1 | D5 | 4 Right | A3 |
| 3 DATA0 | D8 | 6 B/A | A4 |
| 2 DATA1 | D9 | 9 C/Start | A5 |
| 8 DATA2 | D10 | 7 TH | D2 |
| 7 DATA3 | D11 | 5 +5V | 5V |
| 6 TL | Not connected | 8 GND | GND |

Connect the grounds on both sides to Arduino GND. Mega Drive Up/Down/Left/Right, B/A, and C/Start are driven from Arduino A0–A5. Connect the Mega Drive +5V pin to Arduino 5V.

### Upload and use

1. Open `saturn_to_megadrive.ino` in the Arduino IDE.
2. Select an Uno or Nano board using an ATmega328P at 5 V / 16 MHz, then select the correct port. For older Nano boards, select the old bootloader under **Processor** if required.
3. Upload the sketch.
4. Connect the wired Saturn controller and attach the adapter to the Mega Drive console.

The adapter starts in 3-button mode. Button mapping:

- D-pad → Up / Down / Left / Right
- Saturn B and C → Mega Drive B and C
- Saturn A and Start → Mega Drive A and Start
- In 6-button mode only, Saturn X, Y, Z, and L (Mode) → extra Mega Drive X, Y, Z, and Mode buttons

Press Saturn L (Mode) and R together to request a 3-/6-button mode change, then press the Mega Drive console's reset button. The change is applied when the adapter detects that the TH signal has not changed for at least 100 ms during reset. The Arduino D13 LED is off in 3-button mode, on in 6-button mode, and blinks while a mode change is pending.

### Optional serial logging

Logging is disabled by default. To enable it, change `#define LOG_ENABLE 0` to `#define LOG_ENABLE 1` in the sketch and upload it again.

- Connect Arduino TX (D1) to the USB-to-serial adapter's RXD.
- Connect the grounds together. Do not connect the adapter's 5 V output.
- Use 115200 baud, 8N1.
- For a 3.3 V-only serial adapter, reduce the TX signal voltage with a divider or equivalent level shifting.

In the log, `r` is the raw Saturn input (pressed = 0); `FFFC` is the normal value when no buttons are pressed. `m` indicates the current and pending modes, and `t` shows the values sent to the Mega Drive. See the “ログの見方” comment near the top of the sketch for the full field descriptions.
