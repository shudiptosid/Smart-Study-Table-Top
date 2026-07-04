/* Smart Study Table - Fixed encoder & countdown (UNO) - COMPILATION FIXED
   - SH1106 OLED via U8g2 (page mode)
   - DS3231 RTC via RTClib
   - HC-SR04 ultrasonic presence -> relay (LED)
   - Rotary encoder (A=2,B=3) with switch (4)
   - IR phone detector (6)
   - Buzzer (5) active/passive selectable
   - Non-blocking millis() timing
   - Serial commands: SET YYYY MM DD HH MM SS | SETNOW | READ
   - Encoder long-press (>2s) also triggers SETNOW
   - Uses countdownEndMillis for robust countdown timing
*/

#include <Wire.h>
#include <U8g2lib.h>
#include <RTClib.h>

// DISPLAY
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0);

// RTC
RTC_DS3231 rtc;
bool rtcOk = false;

// PINS
const int trigPin = 8;
const int echoPin = 9;
const int relayPin = 7;
const int irPin = 6;
const int buzzerPin = 5;

const int encA = 2;
const int encB = 3;
const int encBtn = 4;

// CONFIG
#define ACTIVE_BUZZER 1
const unsigned int BUZZER_FREQ = 1000;
const unsigned long BUZZER_TOGGLE_MS = 220;

#define USE_PULLUP_FOR_IR true
const int PHONE_DETECTED_STATE = LOW;

// ENCODER VARS
volatile long encoderPos = 0;
volatile int lastEncoded = 0;
// note: we'll sample encoderPos in main loop
bool lastBtnState = HIGH;
bool btnState = HIGH;
unsigned long lastDebounceBtn = 0;
const unsigned long debounceDelay = 50;
unsigned long btnPressStart = 0;
const unsigned long LONG_PRESS_MS = 2000;

// STATE MACHINE
enum State { IDLE, SET_TIMER, WAIT_PHONE, COUNTDOWN, ABORTED };
State state = IDLE;

// TIMER
unsigned int timerMinutes = 25;
unsigned long countdownSeconds = 0;
bool countdownRunning = false;
unsigned long countdownEndMillis = 0UL; // <-- end timestamp
unsigned long countdownStartMillis = 0UL; // declared to avoid undefined reference

// ULTRASONIC
unsigned long lastUltrasonicCheck = 0;
const unsigned long ultrasonicInterval = 200;
long studentDistance = 999;
const int presenceThreshold = 100; // cm

// RELAY / LED
bool ledState = false;

// BUZZER
unsigned long lastBuzzerToggle = 0;
bool buzzerOn = false;

// DISPLAY
unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 250;

// PHONE DETECTION
unsigned long lastPhoneCheck = 0;
const unsigned long phoneCheckInterval = 150;
bool phonePresent = false;

// SERIAL
String serialLine = "";

// Forward declaration (ensures available when called from loop)
void handleStudentPresence(bool present);

// ----------------- SETUP -----------------
void setup() {
  Serial.begin(115200);
  while (!Serial) ; // optional wait

  // pins
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(relayPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  if (USE_PULLUP_FOR_IR) pinMode(irPin, INPUT_PULLUP);
  else pinMode(irPin, INPUT);

  pinMode(encA, INPUT_PULLUP);
  pinMode(encB, INPUT_PULLUP);
  pinMode(encBtn, INPUT_PULLUP);

  // encoder interrupts
  attachInterrupt(digitalPinToInterrupt(encA), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(encB), updateEncoder, CHANGE);

  // rtc
  rtcOk = rtc.begin();
  if (!rtcOk) Serial.println("Couldn't find RTC. Check wiring (SDA->A4, SCL->A5).");
  else Serial.println("RTC found.");

  // display
  u8g2.begin();

  // defaults
  setRelay(false);
}

// ----------------- LOOP -----------------
void loop() {
  unsigned long now = millis();

  handleSerialInput();
  readEncoderButton();

  // Ultrasonic
  if (now - lastUltrasonicCheck >= ultrasonicInterval) {
    lastUltrasonicCheck = now;
    studentDistance = readUltrasonic();
    bool present = (studentDistance > 0 && studentDistance <= presenceThreshold);
    handleStudentPresence(present);
  }

  // Phone checks
  if (state == WAIT_PHONE || state == COUNTDOWN) {
    if (now - lastPhoneCheck >= phoneCheckInterval) {
      lastPhoneCheck = now;
      int irVal = digitalRead(irPin);
      phonePresent = (irVal == PHONE_DETECTED_STATE);
      // if waiting and phone placed, start countdown
      if (state == WAIT_PHONE && phonePresent && !countdownRunning) {
        // start countdown reliably
        startCountdown();
        countdownSeconds = (unsigned long)timerMinutes * 60UL;
        state = COUNTDOWN;
      }
    }
  }

  // COUNTDOWN handling using end timestamp
  if (state == COUNTDOWN && countdownRunning) {
    if (millis() >= countdownEndMillis) {
      // finished
      countdownSeconds = 0;
      countdownRunning = false;
      setRelay(false);
      showMessage("Timer Done");
      state = IDLE;
      stopBuzzer();
    } else {
      unsigned long remMs = countdownEndMillis - millis();
      countdownSeconds = remMs / 1000UL;
    }

    if (!phonePresent) rapidBuzzer();
    else stopBuzzer();
  }

  // SET_TIMER: update timerMinutes from encoderPos (smoothly)
  static long lastSnap = 0;
  if (state == SET_TIMER) {
    long snap = encoderPos;
    if (snap != lastSnap) {
      lastSnap = snap;
      long mins = snap / 4; // 4 steps per minute as before
      if (mins < 1) mins = 1;
      if (mins > 600) mins = 600;
      timerMinutes = (unsigned int)mins;
    }
  }

  // Display update
  if (now - lastDisplayUpdate >= displayInterval) {
    lastDisplayUpdate = now;
    updateDisplay();
  }
}

// ----------------- ENCODER ISR -----------------
void updateEncoder() {
  int MSB = digitalRead(encA);
  int LSB = digitalRead(encB);
  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;
  if (sum == 0b0001 || sum == 0b0111 || sum == 0b1110 || sum == 0b1000) encoderPos++;
  if (sum == 0b0010 || sum == 0b1011 || sum == 0b1101 || sum == 0b0100) encoderPos--;
  lastEncoded = encoded;
}

// ----------------- ENCODER BUTTON -----------------
void readEncoderButton() {
  bool reading = digitalRead(encBtn);
  unsigned long t = millis();
  if (reading != lastBtnState) lastDebounceBtn = t;
  if ((t - lastDebounceBtn) > debounceDelay) {
    if (reading != btnState) {
      btnState = reading;
      if (btnState == LOW) {
        // pressed down
        btnPressStart = t;
      } else {
        // released
        unsigned long pressDur = t - btnPressStart;
        if (pressDur >= LONG_PRESS_MS) {
          // long press => set RTC to compile time
          if (rtcOk) {
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
            Serial.println("RTC set to compile time (long press).");
            showMessage("RTC Saved");
          } else {
            Serial.println("RTC missing; cannot set time.");
            showMessage("RTC Missing");
          }
        } else {
          // short press -> normal action
          onEncoderShortPress();
        }
      }
    }
  }
  lastBtnState = reading;
}

void onEncoderShortPress() {
  if (state == IDLE) {
    state = SET_TIMER;
    encoderPos = (long)timerMinutes * 4L; // align encoder to current minutes
  } else if (state == SET_TIMER) {
    long mins = encoderPos / 4;
    if (mins < 1) mins = 1;
    if (mins > 600) mins = 600;
    timerMinutes = (unsigned int)mins;
    state = WAIT_PHONE;
  } else if (state == WAIT_PHONE) {
    state = IDLE;
  } else if (state == COUNTDOWN) {
    // cancel
    countdownRunning = false;
    stopBuzzer();
    setRelay(false);
    showMessage("Aborted");
    state = IDLE;
  } else if (state == ABORTED) {
    state = IDLE;
  }
}

// ----------------- ULTRASONIC -----------------
long readUltrasonic() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return -1;
  long distance = duration * 0.034 / 2;
  return distance;
}

// ----------------- Student presence handler -----------------
void handleStudentPresence(bool present) {
  if (present) {
    if (!ledState) setRelay(true);
  } else {
    if (state != COUNTDOWN && ledState) setRelay(false);
  }
}

// ----------------- RELAY -----------------
void setRelay(bool on) {
  digitalWrite(relayPin, on ? HIGH : LOW);
  ledState = on;
}

// ----------------- START COUNTDOWN -----------------
void startCountdown() {
  countdownRunning = true;
  // set end timestamp exactly timerMinutes from now
  unsigned long durMs = (unsigned long)timerMinutes * 60UL * 1000UL;
  countdownEndMillis = millis() + durMs;
  countdownSeconds = (unsigned long)timerMinutes * 60UL;
  countdownStartMillis = millis(); // optional legacy variable
  setRelay(true);
}

// ----------------- BUZZER -----------------
void rapidBuzzer() {
  unsigned long now = millis();
  if (now - lastBuzzerToggle > BUZZER_TOGGLE_MS) {
    lastBuzzerToggle = now;
    buzzerOn = !buzzerOn;
#if ACTIVE_BUZZER
    digitalWrite(buzzerPin, buzzerOn ? HIGH : LOW);
#else
    if (buzzerOn) tone(buzzerPin, BUZZER_FREQ);
    else noTone(buzzerPin);
#endif
  }
}

void stopBuzzer() {
#if ACTIVE_BUZZER
  digitalWrite(buzzerPin, LOW);
#else
  noTone(buzzerPin);
#endif
  buzzerOn = false;
}

// ----------------- SHOW MESSAGE -----------------
void showMessage(const char *msg) {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr(8, 34, msg);
  } while (u8g2.nextPage());
  delay(900);
}

// ----------------- DISPLAY -----------------
void updateDisplay() {
  char buf1[32], buf2[32];
  if (rtcOk) {
    DateTime now = rtc.now();
    sprintf(buf1, "%02d-%02d-%04d %02d:%02d:%02d",
            now.day(), now.month(), now.year(),
            now.hour(), now.minute(), now.second());
  } else {
    strncpy(buf1, "-- RTC MISSING --", sizeof(buf1));
    buf1[sizeof(buf1)-1] = 0;
  }

  if (studentDistance > 0 && studentDistance < 1000)
    sprintf(buf2, "Dist:%ldcm LED:%s", studentDistance, ledState ? "On" : "Off");
  else
    sprintf(buf2, "Dist:-- LED:%s", ledState ? "On" : "Off");

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 10, buf1);
    u8g2.drawStr(0, 22, buf2);

    u8g2.setFont(u8g2_font_ncenB08_tr);
    switch (state) {
      case IDLE:
        u8g2.drawStr(0, 36, "State: Idle");
        u8g2.setFont(u8g2_font_6x12_tr);
        u8g2.drawStr(0, 52, "Press encoder -> Set timer");
        break;
      case SET_TIMER: {
        char tbuf[24];
        sprintf(tbuf, "Set Timer: %u min", timerMinutes);
        u8g2.drawStr(0, 36, "State: Set Timer");
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(0, 50, tbuf);
        u8g2.setFont(u8g2_font_6x12_tr);
        u8g2.drawStr(0, 62, "Press encoder -> Confirm");
      } break;
      case WAIT_PHONE:
        u8g2.drawStr(0, 36, "State: Waiting Phone");
        u8g2.setFont(u8g2_font_6x12_tr);
        u8g2.drawStr(0, 50, "Place your phone to start");
        u8g2.drawStr(0, 62, phonePresent ? "Phone: YES" : "Phone: NO");
        break;
      case COUNTDOWN: {
        char tbuf[20];
        unsigned int mm = countdownSeconds / 60;
        unsigned int ss = countdownSeconds % 60;
        sprintf(tbuf, "%02u:%02u left", mm, ss);
        u8g2.drawStr(0, 36, "State: Counting...");
        u8g2.drawStr(0, 52, tbuf);
        u8g2.setFont(u8g2_font_6x12_tr);
        u8g2.drawStr(0, 62, "Press encoder -> Cancel");
      } break;
      case ABORTED:
        u8g2.drawStr(0, 36, "State: Aborted");
        break;
    }
  } while (u8g2.nextPage());
}

// ----------------- SERIAL HANDLING -----------------
void handleSerialInput() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      serialLine.trim();
      if (serialLine.length() > 0) {
        processSerialCommand(serialLine);
        serialLine = "";
      }
    } else {
      serialLine += c;
      if (serialLine.length() > 200) serialLine.remove(0, serialLine.length() - 200);
    }
  }
}

void processSerialCommand(String &line) {
  char buf[220];
  line.toCharArray(buf, sizeof(buf));
  char *tokens[12];
  int tk = 0;
  char *p = strtok(buf, " \t");
  while (p && tk < 12) {
    tokens[tk++] = p;
    p = strtok(NULL, " \t");
  }
  if (tk == 0) return;
  for (char *q = tokens[0]; *q; ++q) *q = toupper(*q);

  if (strcmp(tokens[0], "SETNOW") == 0) {
    if (!rtcOk) Serial.println("RTC not available - can't set time.");
    else {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      Serial.println("RTC set to compile time (SETNOW).");
      showMessage("RTC Saved");
    }
  } else if (strcmp(tokens[0], "SET") == 0) {
    if (tk < 7) {
      Serial.println("Usage: SET YYYY MM DD HH MM SS");
    } else {
      int year = atoi(tokens[1]);
      int month = atoi(tokens[2]);
      int day = atoi(tokens[3]);
      int hour = atoi(tokens[4]);
      int minute = atoi(tokens[5]);
      int second = atoi(tokens[6]);
      bool ok = true;
      if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31) ok = false;
      if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) ok = false;
      if (!ok) {
        Serial.println("Invalid date/time.");
      } else if (!rtcOk) {
        Serial.println("RTC not available - can't set time.");
      } else {
        rtc.adjust(DateTime(year, month, day, hour, minute, second));
        Serial.print("RTC set to: ");
        Serial.print(year); Serial.print('/'); Serial.print(month); Serial.print('/'); Serial.print(day);
        Serial.print(' '); Serial.print(hour); Serial.print(':'); Serial.print(minute); Serial.print(':'); Serial.println(second);
        showMessage("RTC Saved");
      }
    }
  } else if (strcmp(tokens[0], "READ") == 0) {
    if (!rtcOk) Serial.println("RTC not available.");
    else {
      DateTime n = rtc.now();
      char s[40];
      sprintf(s, "%04d/%02d/%02d %02d:%02d:%02d", n.year(), n.month(), n.day(), n.hour(), n.minute(), n.second());
      Serial.println(s);
    }
  } else {
    Serial.print("Unknown command: ");
    Serial.println(tokens[0]);
    Serial.println("Commands: SET YYYY MM DD HH MM SS | SETNOW | READ");
  }
}
