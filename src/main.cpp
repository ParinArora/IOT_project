// #include <WiFi.h>
// #include <HTTPClient.h>
// #include <ArduinoJson.h>

// // Wi-Fi config
// const char* WIFI_SSID = "Coldspot";
// const char* WIFI_PASS = "not4youbuddy";
// const char* SERVER_BASE = "http://10.227.78.7:5000";
// //TIming
// unsigned long lastPostMs = 0;
// unsigned long lastPollMs = 0;

// const unsigned long POST_INTERVAL_MS = 3000;  // send sample data every 3s
// const unsigned long POLL_INTERVAL_MS = 2000;  // poll for command every 2s


// // -------------------------
// // Example command handlers
// // -------------------------
// bool handle_ping(JsonVariant data) {
//   Serial.println("Handled command: ping");
//   if (!data.isNull()) {
//     serializeJson(data, Serial);
//     Serial.println();
//   }
//   return true;
// }

// bool handle_set_mode(JsonVariant data) {
//   Serial.println("Handled command: set_mode");
//   if (!data.isNull()) {
//     serializeJson(data, Serial);
//     Serial.println();
//   }
//   return true;
// }

// bool dispatchCommand(const String& action, JsonVariant data) {
//   if (action == "ping") {
//     return handle_ping(data);
//   }

//   if (action == "set_mode") {
//     return handle_set_mode(data);
//   }

//   Serial.print("Ignoring unknown action: ");
//   Serial.println(action);
//   return false;
// }


// void connectToWiFi() {
//   Serial.print("Connecting to Wi-Fi: ");
//   Serial.println(WIFI_SSID);

//   WiFi.mode(WIFI_STA);
//   WiFi.begin(WIFI_SSID, WIFI_PASS);

//   while (WiFi.status() != WL_CONNECTED) {
//     delay(500);
//     Serial.print(".");
//   }

//   Serial.println();
//   Serial.println("Wi-Fi connected");
//   Serial.print("ESP32 IP: ");
//   Serial.println(WiFi.localIP());
// }

// // -------------------------
// // Sample POST
// // -------------------------
// void postSampleData() {
//   if (WiFi.status() != WL_CONNECTED) {
//     Serial.println("Wi-Fi disconnected, skipping POST");
//     return;
//   }

//   HTTPClient http;
//   String url = String(SERVER_BASE) + "/api/update_sensor";

//   http.begin(url);
//   http.addHeader("Content-Type", "application/json");

//   // Sample payload only
//   String payload = R"({
//     "name": "sample_sensor",
//     "value": 42
//   })";

//   int httpCode = http.POST(payload);

//   Serial.print("POST /api/update_sensor -> ");
//   Serial.println(httpCode);

//   if (httpCode > 0) {
//     String response = http.getString();
//     Serial.println(response);
//   } else {
//     Serial.println(http.errorToString(httpCode));
//   }

//   http.end();
// }

// // -------------------------
// // Poll command
// // Expected JSON example:
// //
// // {
// //   "action": "ping",
// //   "data": {
// //     "message": "hello"
// //   }
// // }
// //
// // If "action" has no matching handler, ignore it.
// // -------------------------
// void pollCommand() {
//   if (WiFi.status() != WL_CONNECTED) {
//     Serial.println("Wi-Fi disconnected, skipping poll");
//     return;
//   }

//   HTTPClient http;
//   String url = String(SERVER_BASE) + "/api/get_command";

//   http.begin(url);
//   int httpCode = http.GET();

//   Serial.print("GET /api/get_command -> ");
//   Serial.println(httpCode);

//   if (httpCode <= 0) {
//     Serial.println(http.errorToString(httpCode));
//     http.end();
//     return;
//   }

//   String response = http.getString();
//   Serial.println(response);
//   http.end();

//   StaticJsonDocument<256> doc;
//   DeserializationError err = deserializeJson(doc, response);

//   if (err) {
//     Serial.print("JSON parse error: ");
//     Serial.println(err.c_str());
//     return;
//   }

//   const char* action = doc["action"];
//   JsonVariant data = doc["data"];

//   if (action == nullptr || strlen(action) == 0) {
//     Serial.println("No action in response, ignoring");
//     return;
//   }

//   dispatchCommand(String(action), data);
// }

// void setup() {
//   Serial.begin(115200);
//   delay(1000);

//   connectToWiFi();
// }

// void loop() {
//   unsigned long now = millis();

//   if (now - lastPostMs >= POST_INTERVAL_MS) {
//     lastPostMs = now;
//     postSampleData();
//   }

//   if (now - lastPollMs >= POLL_INTERVAL_MS) {
//     lastPollMs = now;
//     pollCommand();
//   }
// }


#include <Arduino.h>
// ESP32 10x4 LED Matrix Multiplexing
// Columns = positive/anodes
// Rows = negative/cathodes
//
// LED ON = column HIGH, row LOW

void refreshMatrix();
void allOff();
void clearPattern();
void snakePattern();
void fillPattern();
void setLED(int row, int col, bool state);

void scanningColumnPattern();
void diagonalPattern();
void blinkAllPattern();

const int ROWS = 4;
const int COLS = 10;

// Rows: top to bottom
int rowPins[ROWS] = {5, 6, 9, 10};


// Columns: left to right
// IMPORTANT:
// GPIO 35 and 36 are input-only on most ESP32 boards.
// GPIO 37 may also not be usable depending on your board.
// Replace these if needed.
int colPins[COLS] = {37, 38, 18, 17, 16, 15, 14, 8, 36, 35};

// Display memory
bool pattern[ROWS][COLS];

// Animation control
unsigned long lastAnimationTime = 0;
int animationStep = 0;
int mode = 0;

void setup() {
  // Set row pins as outputs
  for (int r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH); // rows off
  }

  // Set column pins as outputs
  for (int c = 0; c < COLS; c++) {
    pinMode(colPins[c], OUTPUT);
    digitalWrite(colPins[c], LOW); // columns off
  }

  clearPattern();
}

void loop() {
  // Keep refreshing the LED matrix all the time
  refreshMatrix();

  // Change animation frame every 150 ms
  if (millis() - lastAnimationTime > 150) {
    lastAnimationTime = millis();

    if (mode == 0) {
      snakePattern();

      if (animationStep > ROWS * COLS) {
        animationStep = 0;
        mode = 1;
        clearPattern();
      }
    }

    else if (mode == 1) {
      scanningColumnPattern();

      if (animationStep >= COLS) {
        animationStep = 0;
        mode = 2;
        clearPattern();
      }
    }

    else if (mode == 2) {
      diagonalPattern();

      if (animationStep > ROWS + COLS) {
        animationStep = 0;
        mode = 3;
        clearPattern();
      }
    }

    else if (mode == 3) {
      blinkAllPattern();

      if (animationStep >= 8) {
        animationStep = 0;
        mode = 0;
        clearPattern();
      }
    }
  }
}

// Refreshes the LED matrix using column multiplexing
void refreshMatrix() {
  for (int c = 0; c < COLS; c++) {
    allOff();

    // Turn on the current column
    digitalWrite(colPins[c], HIGH);

    // Turn on the needed rows for this column
    for (int r = 0; r < ROWS; r++) {
      if (pattern[r][c]) {
        digitalWrite(rowPins[r], LOW);   // row active, LED on
      } else {
        digitalWrite(rowPins[r], HIGH);  // row inactive, LED off
      }
    }

    // Small delay controls brightness
    delayMicroseconds(1000);
  }
}

// Turns all LEDs off
void allOff() {
  // Disable all columns
  for (int c = 0; c < COLS; c++) {
    digitalWrite(colPins[c], LOW);
  }

  // Disable all rows
  for (int r = 0; r < ROWS; r++) {
    digitalWrite(rowPins[r], HIGH);
  }
}

// Clears the pattern
void clearPattern() {
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      pattern[r][c] = false;
    }
  }
}

// Fills the whole display
void fillPattern() {
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      pattern[r][c] = true;
    }
  }
}

// Sets one LED
void setLED(int row, int col, bool state) {
  if (row >= 0 && row < ROWS && col >= 0 && col < COLS) {
    pattern[row][col] = state;
  }
}

// Pattern 1: snake fills the display
void snakePattern() {
  clearPattern();

  for (int i = 0; i < animationStep; i++) {
    int row = i / COLS;
    int col;

    if (row % 2 == 0) {
      col = i % COLS;
    } else {
      col = COLS - 1 - (i % COLS);
    }

    setLED(row, col, true);
  }

  animationStep++;
}

// Pattern 2: full column moves left to right
void scanningColumnPattern() {
  clearPattern();

  for (int r = 0; r < ROWS; r++) {
    setLED(r, animationStep, true);
  }

  animationStep++;
}

// Pattern 3: diagonal wave
void diagonalPattern() {
  clearPattern();

  for (int r = 0; r < ROWS; r++) {
    int c = animationStep - r;

    if (c >= 0 && c < COLS) {
      setLED(r, c, true);
    }
  }

  animationStep++;
}

// Pattern 4: blink all LEDs
void blinkAllPattern() {
  if (animationStep % 2 == 0) {
    fillPattern();
  } else {
    clearPattern();
  }

  animationStep++;
}