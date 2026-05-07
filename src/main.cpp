// =============================================================================
//  ESP32 Unified Firmware
//  - Wi-Fi connect
//  - POST thermistor reading every 3s to /api/update_sensor
//  - Poll /api/get_command every 2s for pattern + scroll text
//  - Drive a 10x4 LED matrix with multiple patterns
//  - 6x4 scrolling text uses the middle 6 columns of the same matrix
//
//  Wiring assumption (matches the snippets you provided):
//    Columns = anodes (HIGH = active)
//    Rows    = cathodes (LOW = active)
//    LED ON  = column HIGH AND row LOW
//
//  IMPORTANT PIN NOTE:
//    GPIO 34/35/36/37/39 are INPUT-ONLY on most classic ESP32 boards and
//    cannot drive an LED column. The pin map below mirrors your original
//    snippet so you can compile, but you should swap any input-only pins
//    for real output-capable GPIOs (e.g. 4, 18, 19, 21, 22, 23, 25, 26, 27).
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

// ---------------------------------------------------------------------------
//  Wi-Fi / server config
// ---------------------------------------------------------------------------
const char* WIFI_SSID   = "Coldspot";
const char* WIFI_PASS   = "not4youbuddy";
const char* SERVER_BASE = "http://10.247.139.7:5000";

const unsigned long POST_INTERVAL_MS = 3000;  // sensor POST cadence
const unsigned long POLL_INTERVAL_MS = 2000;  // command poll cadence

unsigned long lastPostMs = 0;
unsigned long lastPollMs = 0;

// ---------------------------------------------------------------------------
//  Thermistor
// ---------------------------------------------------------------------------
const int   ThermistorPin = A0;
const float R1            = 10000.0;
const float ADC_MAX       = 4095.0;
const float c1 = 1.009249522e-03;
const float c2 = 2.378405444e-04;
const float c3 = 2.019202697e-07;

// ---------------------------------------------------------------------------
//  LED Matrix (10 columns x 4 rows)
// ---------------------------------------------------------------------------
#define ROWS 4
#define COLS 10

// Rows: top -> bottom
int rowPins[ROWS] = {5, 6, 9, 10};

// Columns: left -> right
// (See pin warning at top of file.)
int colPins[COLS] = {39, 38, 17, 16, 15, 14, 8, 36, 35, 37};

#define ROW_ON  LOW
#define ROW_OFF HIGH
#define COL_ON  HIGH
#define COL_OFF LOW

// Per-pixel scan time (microseconds). Higher = brighter, lower = less flicker.
#define PIXEL_TIME_US 40

// Frame buffer
bool pattern_buf[ROWS][COLS];

// ---------------------------------------------------------------------------
//  Pattern state
// ---------------------------------------------------------------------------
enum PatternMode {
  MODE_OFF,
  MODE_BLINK,        // blink whole display
  MODE_CHASE,        // single column sweeping L->R
  MODE_FLICKER,      // random pixels
  MODE_ALTERNATE,    // checkerboard toggle
  MODE_SNAKE,        // snake fill
  MODE_DIAGONAL,     // diagonal wave
  MODE_FILL,         // all on
  MODE_SCROLL_TEXT   // 6x4 scrolling text in middle 6 columns
};

PatternMode currentMode = MODE_BLINK;
unsigned long lastAnimMs = 0;
int animStep = 0;

// Scrolling text
char scrollMessage[64] = "HELLO ESP32 ";
int scrollCol = 0;

// ---------------------------------------------------------------------------
//  Concurrency: matrix runs on core 1, networking runs on core 0.
//  The mutex guards writes to pattern_buf / currentMode / scrollMessage.
//  The render loop reads pattern_buf without locking (a torn read is at
//  worst one bad pixel for one frame, which is invisible).
// ---------------------------------------------------------------------------
SemaphoreHandle_t stateMutex = nullptr;
TaskHandle_t      matrixTaskHandle = nullptr;
TaskHandle_t      networkTaskHandle = nullptr;

// ---------------------------------------------------------------------------
//  Forward declarations
// ---------------------------------------------------------------------------
void connectToWiFi();
void postSensorData();
void pollCommand();
void applyCommand(const char* patternStr, const char* textStr);
PatternMode parseMode(const String& s);

void refreshMatrix();
void allOff();
void clearPattern();
void fillPattern();
void setLED(int row, int col, bool state);

void runBlink();
void runChase();
void runFlicker();
void runAlternate();
void runSnake();
void runDiagonal();
void runScrollText();

float readTemperatureF();

// ---------------------------------------------------------------------------
//  Tiny 3x4 font for scrolling text (middle 6 columns of the 10x4 matrix)
// ---------------------------------------------------------------------------
struct FontChar {
  char c;
  byte rows[4];  // 3 bits used per row
};

FontChar font[] = {
  {'0', {0b111, 0b101, 0b101, 0b111}},
  {'1', {0b010, 0b110, 0b010, 0b111}},
  {'2', {0b111, 0b001, 0b010, 0b111}},
  {'3', {0b111, 0b001, 0b011, 0b111}},
  {'4', {0b101, 0b101, 0b111, 0b001}},
  {'5', {0b111, 0b100, 0b111, 0b011}},
  {'6', {0b111, 0b100, 0b111, 0b111}},
  {'7', {0b111, 0b001, 0b010, 0b010}},
  {'8', {0b111, 0b101, 0b111, 0b111}},
  {'9', {0b111, 0b101, 0b111, 0b001}},
  {'A', {0b111, 0b101, 0b111, 0b101}},
  {'B', {0b110, 0b101, 0b110, 0b111}},
  {'C', {0b111, 0b100, 0b100, 0b111}},
  {'D', {0b110, 0b101, 0b101, 0b110}},
  {'E', {0b111, 0b100, 0b110, 0b111}},
  {'F', {0b111, 0b100, 0b110, 0b100}},
  {'G', {0b111, 0b100, 0b101, 0b111}},
  {'H', {0b101, 0b101, 0b111, 0b101}},
  {'I', {0b111, 0b010, 0b010, 0b111}},
  {'J', {0b001, 0b001, 0b101, 0b111}},
  {'K', {0b101, 0b110, 0b100, 0b101}},
  {'L', {0b100, 0b100, 0b100, 0b111}},
  {'M', {0b101, 0b111, 0b101, 0b101}},
  {'N', {0b101, 0b111, 0b111, 0b101}},
  {'O', {0b111, 0b101, 0b101, 0b111}},
  {'P', {0b111, 0b101, 0b111, 0b100}},
  {'Q', {0b111, 0b101, 0b111, 0b001}},
  {'R', {0b111, 0b101, 0b110, 0b101}},
  {'S', {0b111, 0b100, 0b111, 0b011}},
  {'T', {0b111, 0b010, 0b010, 0b010}},
  {'U', {0b101, 0b101, 0b101, 0b111}},
  {'V', {0b101, 0b101, 0b101, 0b010}},
  {'W', {0b101, 0b101, 0b111, 0b111}},
  {'X', {0b101, 0b010, 0b010, 0b101}},
  {'Y', {0b101, 0b101, 0b010, 0b010}},
  {'Z', {0b111, 0b001, 0b010, 0b111}},
  {' ', {0b000, 0b000, 0b000, 0b000}},
};
const int fontCount = sizeof(font) / sizeof(font[0]);

byte* getCharPattern(char ch) {
  if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
  for (int i = 0; i < fontCount; i++) {
    if (font[i].c == ch) return font[i].rows;
  }
  return font[fontCount - 1].rows;  // space fallback
}

// =============================================================================
//  TASKS
// =============================================================================

// Matrix task: runs on core 1. Refreshes LEDs continuously and steps the
// current animation. NEVER blocks on network calls.
void matrixTask(void* param) {
  for (;;) {
    refreshMatrix();

    unsigned long now = millis();
    if (now - lastAnimMs >= 250) {
      lastAnimMs = now;

      // Lock briefly while we mutate the frame buffer / scroll state.
      if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        switch (currentMode) {
          case MODE_OFF:         clearPattern(); break;
          case MODE_BLINK:       runBlink(); break;
          case MODE_CHASE:       runChase(); break;
          case MODE_FLICKER:     runFlicker(); break;
          case MODE_ALTERNATE:   runAlternate(); break;
          case MODE_SNAKE:       runSnake(); break;
          case MODE_DIAGONAL:    runDiagonal(); break;
          case MODE_FILL:        fillPattern(); break;
          case MODE_SCROLL_TEXT: runScrollText(); break;
        }
        xSemaphoreGive(stateMutex);
      }
    }

    // Yield briefly so the watchdog and lower-priority tasks get cycles.
    // delayMicroseconds is too short; vTaskDelay(1) yields for one tick (~1ms).
    vTaskDelay(1);
  }
}

// Network task: runs on core 0 alongside the Wi-Fi stack. Allowed to block
// on HTTP calls because the matrix task is on the other core.
void networkTask(void* param) {
  for (;;) {
    unsigned long now = millis();

    if (now - lastPostMs >= POST_INTERVAL_MS) {
      lastPostMs = now;
      postSensorData();
    }

    if (now - lastPollMs >= POLL_INTERVAL_MS) {
      lastPollMs = now;
      pollCommand();
    }

    // Sleep 50ms between checks. We don't need to be punctual here.
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// =============================================================================
//  SETUP / LOOP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  // Matrix pins
  for (int r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], ROW_OFF);
  }
  for (int c = 0; c < COLS; c++) {
    pinMode(colPins[c], OUTPUT);
    digitalWrite(colPins[c], COL_OFF);
  }
  clearPattern();

  // ADC for thermistor
  analogReadResolution(12);
  analogSetPinAttenuation(ThermistorPin, ADC_11db);

  // Mutex must exist before either task starts.
  stateMutex = xSemaphoreCreateMutex();
  if (stateMutex == nullptr) {
    Serial.println("FATAL: failed to create mutex");
    while (1) delay(1000);
  }

  // Wi-Fi connect runs on the default loopTask before we hand off.
  connectToWiFi();

  // Pin matrix to core 1, network to core 0.
  // Stack sizes: 4KB is enough for the matrix loop; network needs 8KB
  // because HTTPClient + JSON parsing chews through stack.
  xTaskCreatePinnedToCore(
    matrixTask, "matrix", 4096, nullptr,
    2,  // priority: higher than network so refresh stays smooth
    &matrixTaskHandle, 1);

  xTaskCreatePinnedToCore(
    networkTask, "network", 8192, nullptr,
    1, &networkTaskHandle, 0);

  Serial.println("Tasks started: matrix on core 1, network on core 0");
}

// loop() runs on core 1 alongside matrixTask. We don't need it for anything,
// so just let it sleep — all real work happens in the two tasks above.
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// =============================================================================
//  Wi-Fi
// =============================================================================
void connectToWiFi() {
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    // Matrix task isn't running yet, so the display will be dark during
    // initial connect. That's fine — only takes a few seconds.
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWi-Fi connect failed (will keep trying in background)");
  }
}

// =============================================================================
//  Sensor
// =============================================================================
float readTemperatureF() {
  int Vo = analogRead(ThermistorPin);
  if (Vo <= 0 || Vo >= (int)ADC_MAX) return NAN;

  float R2 = R1 * (ADC_MAX / (float)Vo - 1.0);
  float logR2 = log(R2);
  float T = 1.0 / (c1 + c2 * logR2 + c3 * logR2 * logR2 * logR2);
  float Tc = T - 273.15;
  return (Tc * 9.0) / 5.0 + 32.0;
}

void postSensorData() {
  if (WiFi.status() != WL_CONNECTED) return;

  float tempF = readTemperatureF();
  if (isnan(tempF)) {
    Serial.println("Bad thermistor reading, skipping POST");
    return;
  }

  HTTPClient http;
  http.begin(String(SERVER_BASE) + "/api/update_sensor");
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> doc;
  doc["name"]  = "thermistor_F";
  doc["value"] = tempF;

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  Serial.printf("POST sensor (%.2f F) -> %d\n", tempF, code);
  http.end();
}

// =============================================================================
//  Command poll
//  Server returns: { "pattern": "<name>", "text": "<optional>" }
// =============================================================================
void pollCommand() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(String(SERVER_BASE) + "/api/get_command");
  int code = http.GET();

  if (code != 200) {
    Serial.printf("GET command -> %d\n", code);
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, body)) {
    Serial.println("JSON parse error on command");
    return;
  }

  const char* pat  = doc["pattern"]  | "";
  const char* text = doc["text"]     | "";
  applyCommand(pat, text);
}

PatternMode parseMode(const String& s) {
  if (s == "off")         return MODE_OFF;
  if (s == "blink")       return MODE_BLINK;
  if (s == "chase")       return MODE_CHASE;
  if (s == "flicker")     return MODE_FLICKER;
  if (s == "alternate")   return MODE_ALTERNATE;
  if (s == "snake")       return MODE_SNAKE;
  if (s == "diagonal")    return MODE_DIAGONAL;
  if (s == "fill")        return MODE_FILL;
  if (s == "scroll_text") return MODE_SCROLL_TEXT;
  return currentMode;  // unknown -> keep current
}

void applyCommand(const char* patternStr, const char* textStr) {
  // Take the mutex before mutating shared state. Wait up to 100ms; if we
  // can't get it the matrix task is busy, just skip this update — we'll
  // get the same command again on the next poll.
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    Serial.println("applyCommand: mutex timeout, skipping");
    return;
  }

  // --- Pattern: only touch state if the mode actually changed ---
  PatternMode newMode = parseMode(String(patternStr));
  bool modeChanged = (newMode != currentMode);

  if (modeChanged) {
    currentMode = newMode;
    animStep = 0;
    scrollCol = 0;
    clearPattern();
    Serial.printf("Mode -> %s\n", patternStr);
  }
  // else: same mode, leave animStep/scrollCol/buffer untouched

  // --- Scroll text: only touch state if the text actually changed ---
  if (textStr != nullptr) {
    size_t textLen = strlen(textStr);
    if (textLen > 0 && textLen < sizeof(scrollMessage)
        && strcmp(textStr, scrollMessage) != 0) {
      strncpy(scrollMessage, textStr, sizeof(scrollMessage) - 1);
      scrollMessage[sizeof(scrollMessage) - 1] = '\0';
      scrollCol = 0;
      Serial.printf("Text -> %s\n", scrollMessage);
    }
  }

  xSemaphoreGive(stateMutex);
}

// =============================================================================
//  Matrix primitives
// =============================================================================
void allOff() {
  for (int c = 0; c < COLS; c++) digitalWrite(colPins[c], COL_OFF);
  for (int r = 0; r < ROWS; r++) digitalWrite(rowPins[r], ROW_OFF);
}

void clearPattern() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      pattern_buf[r][c] = false;
}

void fillPattern() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      pattern_buf[r][c] = true;
}

void setLED(int row, int col, bool state) {
  if (row >= 0 && row < ROWS && col >= 0 && col < COLS) {
    pattern_buf[row][col] = state;
  }
}

// One-LED-at-a-time scan. Robust: works even if power supply is weak.
void refreshMatrix() {
  for (int c = 0; c < COLS; c++) {
    for (int r = 0; r < ROWS; r++) {
      if (pattern_buf[r][c]) {
        allOff();
        digitalWrite(rowPins[r], ROW_ON);
        digitalWrite(colPins[c], COL_ON);
        delayMicroseconds(PIXEL_TIME_US);
      }
    }
  }
  allOff();
}

// =============================================================================
//  Pattern implementations
// =============================================================================
void runBlink() {
  if (animStep % 2 == 0) fillPattern();
  else clearPattern();
  animStep++;
}

void runChase() {
  clearPattern();
  for (int r = 0; r < ROWS; r++) setLED(r, animStep % COLS, true);
  animStep++;
}

void runFlicker() {
  clearPattern();
  // Light ~25% of pixels at random
  int n = (ROWS * COLS) / 4;
  for (int i = 0; i < n; i++) {
    setLED(random(ROWS), random(COLS), true);
  }
  animStep++;
}

void runAlternate() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      pattern_buf[r][c] = ((r + c + animStep) % 2 == 0);
  animStep++;
}

void runSnake() {
  if (animStep > ROWS * COLS) {
    animStep = 0;
    clearPattern();
    return;
  }
  clearPattern();
  for (int i = 0; i < animStep; i++) {
    int row = i / COLS;
    int col = (row % 2 == 0) ? (i % COLS) : (COLS - 1 - (i % COLS));
    setLED(row, col, true);
  }
  animStep++;
}

void runDiagonal() {
  if (animStep > ROWS + COLS) {
    animStep = 0;
  }
  clearPattern();
  for (int r = 0; r < ROWS; r++) {
    int c = animStep - r;
    if (c >= 0 && c < COLS) setLED(r, c, true);
  }
  animStep++;
}

// Scrolling text -- displayed in the middle 6 columns (cols 2..7) so it looks
// like the original 6x4 matrix, while leaving cols 0..1 and 8..9 dark.
void runScrollText() {
  clearPattern();

  int len = strlen(scrollMessage);
  if (len == 0) return;

  const int charWidthWithSpace = 4;   // 3 px char + 1 px gap
  int totalTextCols = len * charWidthWithSpace;

  const int windowStart = 2;
  const int windowWidth = 6;

  for (int displayCol = 0; displayCol < windowWidth; displayCol++) {
    int sourceCol = (scrollCol + displayCol) % totalTextCols;
    int charIndex = sourceCol / charWidthWithSpace;
    int charCol   = sourceCol % charWidthWithSpace;

    if (charCol == 3) continue;  // gap column

    byte* p = getCharPattern(scrollMessage[charIndex]);
    for (int r = 0; r < ROWS; r++) {
      bool bit = bitRead(p[r], 2 - charCol);
      pattern_buf[r][windowStart + displayCol] = bit;
    }
  }

  scrollCol++;
  if (scrollCol >= totalTextCols) scrollCol = 0;
}