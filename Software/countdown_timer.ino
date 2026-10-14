// Pomodoro-style timer: 4 work sessions of the chosen length (5-60 min), 5 min break between each
// Display: 0.96" 128x64 SSD1306 I2C OLED
// LED + buzzer: blink/beep at the end of every work session and break, until button press
// Encoder: sets minutes (1 min/detent) before start, locked for the whole cycle
// Encoder SW: starts the cycle; while running, press = pause/resume;
//             when a phase ends, press = begin the next phase (after session 4, press = reset)

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---- OLED ----
// Micro's I2C pins are the same physical pins as D2 (SDA) / D3 (SCL) - no extra wiring pins needed
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---- External alert LED (with series resistor to GND) ----
const uint8_t LED_PIN = 4;

// ---- Passive piezo buzzer ----
const uint8_t BUZZER_PIN = 7;
const unsigned int BUZZER_FREQ_HZ = 2000;

// ---- Encoder (sets minutes before start, locked once started; SW = start/pause/resume) ----
const uint8_t ENC_A  = A3;
const uint8_t ENC_B  = A4;
const uint8_t ENC_SW = A5;
const int MIN_MINUTES = 5;
const int MAX_MINUTES = 60;
int selectedMinutes = 30;
int lastEncA = HIGH;

// ---- Timer state ----
const int SESSIONS_PER_CYCLE = 4;
const unsigned long BREAK_SECONDS = 5UL * 60UL;
unsigned long remainingSeconds = (unsigned long)selectedMinutes * 60UL;
unsigned long lastSecondMillis = 0;
int session = 1;       // current work session, 1..SESSIONS_PER_CYCLE
bool onBreak = false;  // true while in (or just finished) a break
bool started = false;  // true from first button press until the cycle resets
bool running = false;
bool timeUp = false;   // current phase finished, waiting for button press

// ---- Button debounce / edge detect ----
int lastReading = HIGH;
int buttonState = HIGH;
unsigned long lastDebounceMillis = 0;
const unsigned long DEBOUNCE_MS = 40;

// ---- End-of-phase beep: continues until button press, non-blocking ----
bool flashOn = false;
unsigned long lastFlashMillis = 0;
const unsigned long FLASH_INTERVAL_MS = 300;

void setup() {
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    while (true) {} // no display - nothing useful to do
  }
  display.clearDisplay();
  renderDisplay();
}

void loop() {
  handleEncoder();
  handleButton();
  handleCountdown();
  handleFlash();
}

// ---- Encoder: adjusts selectedMinutes, only before the timer has started ----
void handleEncoder() {
  if (started) return;

  int a = digitalRead(ENC_A);
  if (a == LOW && lastEncA == HIGH) { // falling edge on A
    int b = digitalRead(ENC_B);
    selectedMinutes += (b == HIGH) ? 1 : -1;
    selectedMinutes = constrain(selectedMinutes, MIN_MINUTES, MAX_MINUTES);
    renderDisplay();
  }
  lastEncA = a;
}

// ---- Encoder button (SW): debounce + edge detect, start/pause/resume ----
void handleButton() {
  int reading = digitalRead(ENC_SW);

  if (reading != lastReading) {
    lastDebounceMillis = millis();
  }

  if ((millis() - lastDebounceMillis) > DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) { // pressed (active LOW with INPUT_PULLUP)
        if (timeUp) {
          timeUp = false;
          noTone(BUZZER_PIN);
          digitalWrite(LED_PIN, LOW);
          if (onBreak) {                           // break over -> next work session
            onBreak = false;
            session++;
            startPhase((unsigned long)selectedMinutes * 60UL);
          } else if (session < SESSIONS_PER_CYCLE) { // work session over -> break
            onBreak = true;
            startPhase(BREAK_SECONDS);
          } else {                                 // all sessions done -> back to setting mode
            started = false;
            running = false;
            session = 1;
          }
        } else if (!started) {
          started = true;
          session = 1;
          onBreak = false;
          startPhase((unsigned long)selectedMinutes * 60UL);
        } else {
          running = !running;
          lastSecondMillis = millis(); // avoid an instant tick on resume
        }
        renderDisplay();
      }
    }
  }
  lastReading = reading;
}

// ---- Begin a work session or break of the given length ----
void startPhase(unsigned long seconds) {
  remainingSeconds = seconds;
  running = true;
  lastSecondMillis = millis();
}

// ---- Countdown: one tick per real second while running ----
void handleCountdown() {
  if (!running || timeUp) return;

  if (millis() - lastSecondMillis >= 1000) {
    lastSecondMillis += 1000;
    if (remainingSeconds > 0) {
      remainingSeconds--;
    }
    if (remainingSeconds == 0) {
      running = false;
      timeUp = true;
      renderDisplay();
      flashOn = false;
      lastFlashMillis = millis();
      return;
    }
    renderDisplay();
  }
}

// ---- Alert: LED blinks and buzzer beeps on/off every FLASH_INTERVAL_MS while timeUp is true ----
void handleFlash() {
  if (!timeUp) return;
  if (millis() - lastFlashMillis >= FLASH_INTERVAL_MS) {
    lastFlashMillis = millis();
    flashOn = !flashOn;
    digitalWrite(LED_PIN, flashOn ? HIGH : LOW);
    if (flashOn) tone(BUZZER_PIN, BUZZER_FREQ_HZ);
    else noTone(BUZZER_PIN);
  }
}

// ---- OLED rendering: only called when something actually changes ----
void renderDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (!started) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("SET MINUTES");

    display.setTextSize(4);
    int digits = (selectedMinutes >= 10) ? 2 : 1;
    int x = (SCREEN_WIDTH - digits * 24) / 2;
    display.setCursor(x, 22);
    display.print(selectedMinutes);

    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print("Press dial to start");
  } else if (timeUp) {
    const char* title;
    const char* sub;
    if (onBreak) {
      title = "BREAK OVER";
      sub = "Press: next session";
    } else if (session < SESSIONS_PER_CYCLE) {
      title = "BREAK TIME";
      sub = "Press to start break";
    } else {
      title = "ALL DONE!";
      sub = "Press to reset";
    }
    display.setTextSize(2);
    display.setCursor(4, 20);
    display.print(title);
    display.setTextSize(1);
    display.setCursor(0, 48);
    display.print(sub);
  } else {
    display.setTextSize(1);
    display.setCursor(0, 0);
    if (onBreak) {
      display.print("BREAK");
    } else {
      display.print("FOCUS ");
      display.print(session);
      display.print("/");
      display.print(SESSIONS_PER_CYCLE);
    }

    int mins = remainingSeconds / 60;
    int secs = remainingSeconds % 60;
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);

    display.setTextSize(3);
    display.setCursor(19, 20);
    display.print(buf);

    display.setTextSize(1);
    display.setCursor(44, 54);
    display.print(running ? "running" : "paused");
  }

  display.display();
}
