#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

const char* WIFI_SSID   = "Coldspot";
const char* WIFI_PASS   = "not4youbuddy";
const char* SERVER_BASE = "http://10.247.139.7:5000";

const unsigned long POST_INTERVAL_MS = 3000;
const unsigned long POLL_INTERVAL_MS = 2000;

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


int rowPins[ROWS] = {5, 6, 9, 10};
int colPins[COLS] = {39, 38, 17, 16, 15, 14, 8, 36, 35, 37};

#define ROW_ON  LOW
#define ROW_OFF HIGH
#define COL_ON  HIGH
#define COL_OFF LOW

#define PIXEL_TIME_US 40

constexpr size_t BUF_BITS  = ROWS * COLS;        // 40
constexpr size_t BUF_BYTES = (BUF_BITS + 7) / 8; // 5
uint8_t pattern_buf[BUF_BYTES];

inline bool getLED(int row, int col) {
  int bit = row * COLS + col;
  return (pattern_buf[bit >> 3] >> (bit & 7)) & 0x01;
}

inline void setLEDfast(int row, int col, bool state) {
  int bit = row * COLS + col;
  uint8_t mask = 1u << (bit & 7);
  if (state) pattern_buf[bit >> 3] |=  mask;
  else       pattern_buf[bit >> 3] &= ~mask;
}

enum PatternMode {
  MODE_OFF,
  MODE_BLINK,
  MODE_CHASE,
  MODE_FLICKER,
  MODE_ALTERNATE,
  MODE_SNAKE,
  MODE_DIAGONAL,
  MODE_FILL,
  MODE_SCROLL_TEXT,
  MODE_ALERT_BARS,
  MODE_RAINBOW
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

void connectToWiFi();
bool ensureWiFi();
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
void runAlertBars();
void runRainbow();

float readTemperatureF();


struct FontChar {
  char c;
  byte rows[4];
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
          case MODE_ALERT_BARS:   runAlertBars(); break;
          case MODE_RAINBOW:      runRainbow(); break;
        }
        xSemaphoreGive(stateMutex);
      }
    }

    vTaskDelay(1);
  }
}

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

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}


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

  stateMutex = xSemaphoreCreateMutex();
  if (stateMutex == nullptr) {
    Serial.println("FATAL: failed to create mutex");
    while (1) delay(1000);
  }
  connectToWiFi();

  // Pin matrix to core 1, network to core 0.
  xTaskCreatePinnedToCore(
    matrixTask, "matrix", 4096, nullptr,
    2,  // priority: higher than network so refresh stays smooth
    &matrixTaskHandle, 1);

  xTaskCreatePinnedToCore(
    networkTask, "network", 8192, nullptr,
    1, &networkTaskHandle, 0);

  Serial.println("Tasks started: matrix on core 1, network on core 0");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

void connectToWiFi() {
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true); 
  WiFi.persistent(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
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

bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("[WiFi] Disconnected, attempting reconnect...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    vTaskDelay(pdMS_TO_TICKS(100));  // yield, don't busy-wait
  }

  bool ok = (WiFi.status() == WL_CONNECTED);
  Serial.printf("[WiFi] Reconnect %s\n", ok ? "OK" : "failed");
  return ok;
}

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
  if (!ensureWiFi()) return;

  float tempF = readTemperatureF();
  if (isnan(tempF)) {
    Serial.println("Bad thermistor reading, skipping POST");
    return;
  }

  HTTPClient http;
  http.setReuse(true); 
  http.setTimeout(3000); 
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

void pollCommand() {
  if (!ensureWiFi()) return;

  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(3000);
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
  if (s == "alert_bars")  return MODE_ALERT_BARS;
  if (s == "rainbow")     return MODE_RAINBOW;
  return currentMode;
}

void applyCommand(const char* patternStr, const char* textStr) {
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
  // Single memset clears all 40 bits in 5 bytes — one cache line.
  memset(pattern_buf, 0x00, sizeof(pattern_buf));
}

void fillPattern() {
  // 0xFF sets all 8 bits per byte. The 40-bit matrix fits exactly into
  // 5 bytes so there are no padding bits to worry about.
  memset(pattern_buf, 0xFF, sizeof(pattern_buf));
}

void setLED(int row, int col, bool state) {
  if (row >= 0 && row < ROWS && col >= 0 && col < COLS) {
    setLEDfast(row, col, state);
  }
}

// One-LED-at-a-time scan. Robust: works even if power supply is weak.
void refreshMatrix() {
  for (int c = 0; c < COLS; c++) {
    for (int r = 0; r < ROWS; r++) {
      if (getLED(r, c)) {
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
      setLEDfast(r, c, ((r + c + animStep) % 2 == 0));
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
      setLEDfast(r, windowStart + displayCol, bit);
    }
  }

  scrollCol++;
  if (scrollCol >= totalTextCols) scrollCol = 0;
}

void runAlertBars() {
  clearPattern();

  const int windowStart = 2;
  const int windowWidth = 6;
  // Heights chosen to feel like a rising meter that pegs at max.
  const int heights[6] = {1, 2, 3, 4, 4, 4};

  for (int i = 0; i < windowWidth; i++) {
    int col = windowStart + i;
    int h   = heights[i];

    // Last column is the "peak" -- blink it so the alert reads as urgent.
    bool drawThisCol = true;
    if (i == windowWidth - 1) {
      drawThisCol = (animStep % 2 == 0);
    }
    if (!drawThisCol) continue;

    // Light h pixels from the bottom: rows (ROWS-1) down to (ROWS-h).
    for (int k = 0; k < h; k++) {
      int row = (ROWS - 1) - k;
      setLEDfast(row, col, true);
    }
  }
  animStep++;
}

void runRainbow() {
  clearPattern();
  const int seq[6] = {0, 1, 2, 7, 8, 9};
  int col = seq[animStep % 6];
  for (int r = 0; r < ROWS; r++) {
    setLEDfast(r, col, true);
  }
  animStep++;
}