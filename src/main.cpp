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


// #include <Arduino.h>
// // ESP32 10x4 LED Matrix Multiplexing
// // Columns = positive/anodes
// // Rows = negative/cathodes
// //
// // LED ON = column HIGH, row LOW

// void refreshMatrix();
// void allOff();
// void clearPattern();
// void snakePattern();
// void fillPattern();
// void setLED(int row, int col, bool state);

// void scanningColumnPattern();
// void diagonalPattern();
// void blinkAllPattern();

// const int ROWS = 4;
// const int COLS = 10;

// // Rows: top to bottom
// int rowPins[ROWS] = {5, 6, 9, 10};


// // Columns: left to right
// // IMPORTANT:
// // GPIO 35 and 36 are input-only on most ESP32 boards.
// // GPIO 37 may also not be usable depending on your board.
// // Replace these if needed.
// int colPins[COLS] = {38, 37, 18, 17, 16, 15, 14, 8, 36, 35};

// // Display memory
// bool pattern[ROWS][COLS];

// // Animation control
// unsigned long lastAnimationTime = 0;
// int animationStep = 0;
// int mode = 0;

// void setup() {
//   // Set row pins as outputs
//   for (int r = 0; r < ROWS; r++) {
//     pinMode(rowPins[r], OUTPUT);
//     digitalWrite(rowPins[r], HIGH); // rows off
//   }

//   // Set column pins as outputs
//   for (int c = 0; c < COLS; c++) {
//     pinMode(colPins[c], OUTPUT);
//     digitalWrite(colPins[c], LOW); // columns off
//   }

//   clearPattern();
// }

// void loop() {
//   // Keep refreshing the LED matrix all the time
//   refreshMatrix();

//   // Change animation frame every 150 ms
//   if (millis() - lastAnimationTime > 300) {
//     lastAnimationTime = millis();

//     if (mode == 0) {
//       snakePattern();

//       if (animationStep > ROWS * COLS) {
//         animationStep = 0;
//         mode = 1;
//         clearPattern();
//       }
//     }

//     else if (mode == 1) {
//       scanningColumnPattern();

//       if (animationStep >= COLS) {
//         animationStep = 0;
//         mode = 2;
//         clearPattern();
//       }
//     }

//     else if (mode == 2) {
//       diagonalPattern();

//       if (animationStep > ROWS + COLS) {
//         animationStep = 0;
//         mode = 3;
//         clearPattern();
//       }
//     }

//     else if (mode == 3) {
//       blinkAllPattern();

//       if (animationStep >= 8) {
//         animationStep = 0;
//         mode = 0;
//         clearPattern();
//       }
//     }
//   }
// }

// // Refreshes the LED matrix using column multiplexing
// void refreshMatrix() {
//   for (int c = 0; c < COLS; c++) {
//     allOff();

//     // Turn on the current column
//     digitalWrite(colPins[c], HIGH);

//     // Turn on the needed rows for this column
//     for (int r = 0; r < ROWS; r++) {
//       if (pattern[r][c]) {
//         digitalWrite(rowPins[r], LOW);   // row active, LED on
//       } else {
//         digitalWrite(rowPins[r], HIGH);  // row inactive, LED off
//       }
//     }

//     // Small delay controls brightness
//     delayMicroseconds(1000);
//   }
// }

// // Turns all LEDs off
// void allOff() {
//   // Disable all columns
//   for (int c = 0; c < COLS; c++) {
//     digitalWrite(colPins[c], LOW);
//   }

//   // Disable all rows
//   for (int r = 0; r < ROWS; r++) {
//     digitalWrite(rowPins[r], HIGH);
//   }
// }

// // Clears the pattern
// void clearPattern() {
//   for (int r = 0; r < ROWS; r++) {
//     for (int c = 0; c < COLS; c++) {
//       pattern[r][c] = false;
//     }
//   }
// }

// // Fills the whole display
// void fillPattern() {
//   for (int r = 0; r < ROWS; r++) {
//     for (int c = 0; c < COLS; c++) {
//       pattern[r][c] = true;
//     }
//   }
// }

// // Sets one LED
// void setLED(int row, int col, bool state) {
//   if (row >= 0 && row < ROWS && col >= 0 && col < COLS) {
//     pattern[row][col] = state;
//   }
// }

// // Pattern 1: snake fills the display
// void snakePattern() {
//   clearPattern();

//   for (int i = 0; i < animationStep; i++) {
//     int row = i / COLS;
//     int col;

//     if (row % 2 == 0) {
//       col = i % COLS;
//     } else {
//       col = COLS - 1 - (i % COLS);
//     }

//     setLED(row, col, true);
//   }

//   animationStep++;
// }

// // Pattern 2: full column moves left to right
// void scanningColumnPattern() {
//   clearPattern();

//   for (int r = 0; r < ROWS; r++) {
//     setLED(r, animationStep, true);
//   }

//   animationStep++;
// }

// // Pattern 3: diagonal wave
// void diagonalPattern() {
//   clearPattern();

//   for (int r = 0; r < ROWS; r++) {
//     int c = animationStep - r;

//     if (c >= 0 && c < COLS) {
//       setLED(r, c, true);
//     }
//   }

//   animationStep++;
// }

// // Pattern 4: blink all LEDs
// void blinkAllPattern() {
//   if (animationStep % 2 == 0) {
//     fillPattern();
//   } else {
//     clearPattern();
//   }

//   animationStep++;
// }




// #include <Arduino.h>

// // 4x6 LED matrix used as two 3x4 characters
// // Works on ESP32 / ESP8266 Arduino style code

// #define ROWS 4
// #define COLS 6

// // Change these pins to match your wiring
// // Rows = 4 horizontal lines
// int rowPins[ROWS] = {5, 6, 9, 10};

// // Columns = 6 vertical lines
// // If you have a bigger matrix and want the middle 6 columns,
// // connect these to the middle 6 column pins.
// int colPins[COLS] = {18, 17, 16, 15, 14, 8};

// // Polarity settings
// // Common setup: row HIGH, column LOW turns LED on
// #define ROW_ON  HIGH
// #define ROW_OFF LOW
// #define COL_ON  LOW
// #define COL_OFF HIGH

// bool buffer[ROWS][COLS];

// struct FontChar {
//   char c;
//   byte rows[4];
// };

// FontChar font[] = {
//   {'0', {B111, B101, B101, B111}},
//   {'1', {B010, B110, B010, B111}},
//   {'2', {B111, B001, B010, B111}},
//   {'3', {B111, B001, B011, B111}},
//   {'4', {B101, B101, B111, B001}},
//   {'5', {B111, B100, B111, B011}},
//   {'6', {B111, B100, B111, B111}},
//   {'7', {B111, B001, B010, B010}},
//   {'8', {B111, B101, B111, B111}},
//   {'9', {B111, B101, B111, B001}},

//   {'A', {B111, B101, B111, B101}},
//   {'B', {B110, B101, B110, B111}},
//   {'C', {B111, B100, B100, B111}},
//   {'D', {B110, B101, B101, B110}},
//   {'E', {B111, B100, B110, B111}},
//   {'F', {B111, B100, B110, B100}},
//   {'G', {B111, B100, B101, B111}},
//   {'H', {B101, B101, B111, B101}},
//   {'I', {B111, B010, B010, B111}},
//   {'J', {B001, B001, B101, B111}},
//   {'K', {B101, B110, B100, B101}},
//   {'L', {B100, B100, B100, B111}},
//   {'M', {B101, B111, B101, B101}},
//   {'N', {B101, B111, B111, B101}},
//   {'O', {B111, B101, B101, B111}},
//   {'P', {B111, B101, B111, B100}},
//   {'Q', {B111, B101, B111, B001}},
//   {'R', {B111, B101, B110, B101}},
//   {'S', {B111, B100, B111, B011}},
//   {'T', {B111, B010, B010, B010}},
//   {'U', {B101, B101, B101, B111}},
//   {'V', {B101, B101, B101, B010}},
//   {'W', {B101, B101, B111, B111}},
//   {'X', {B101, B010, B010, B101}},
//   {'Y', {B101, B101, B010, B010}},
//   {'Z', {B111, B001, B010, B111}},

//   {' ', {B000, B000, B000, B000}},
// };

// int fontCount = sizeof(font) / sizeof(font[0]);

// byte* getCharPattern(char ch) {
//   if (ch >= 'a' && ch <= 'z') {
//     ch = ch - 'a' + 'A';
//   }

//   for (int i = 0; i < fontCount; i++) {
//     if (font[i].c == ch) {
//       return font[i].rows;
//     }
//   }

//   return font[fontCount - 1].rows; // space if unknown
// }

// void clearBuffer() {
//   for (int r = 0; r < ROWS; r++) {
//     for (int c = 0; c < COLS; c++) {
//       buffer[r][c] = false;
//     }
//   }
// }

// void drawChar(char ch, int startCol) {
//   byte* pattern = getCharPattern(ch);

//   for (int r = 0; r < 4; r++) {
//     for (int c = 0; c < 3; c++) {
//       bool pixelOn = bitRead(pattern[r], 2 - c);
//       int matrixCol = startCol + c;

//       if (matrixCol >= 0 && matrixCol < COLS) {
//         buffer[r][matrixCol] = pixelOn;
//       }
//     }
//   }
// }

// void setTwoChars(char leftChar, char rightChar) {
//   clearBuffer();

//   drawChar(leftChar, 0); // left 3x4 character
//   drawChar(rightChar, 3); // right 3x4 character
// }

// void allRowsOff() {
//   for (int r = 0; r < ROWS; r++) {
//     digitalWrite(rowPins[r], ROW_OFF);
//   }
// }

// void allColsOff() {
//   for (int c = 0; c < COLS; c++) {
//     digitalWrite(colPins[c], COL_OFF);
//   }
// }

// void refreshMatrix() {
//   for (int r = 0; r < ROWS; r++) {
//     allRowsOff();
//     allColsOff();

//     for (int c = 0; c < COLS; c++) {
//       digitalWrite(colPins[c], buffer[r][c] ? COL_ON : COL_OFF);
//     }

//     digitalWrite(rowPins[r], ROW_ON);
//     delayMicroseconds(1000);
//   }
// }

// void setup() {
//   for (int r = 0; r < ROWS; r++) {
//     pinMode(rowPins[r], OUTPUT);
//   }

//   for (int c = 0; c < COLS; c++) {
//     pinMode(colPins[c], OUTPUT);
//   }

//   allRowsOff();
//   allColsOff();

//   setTwoChars('A', '1');
// }

// void loop() {
//   static unsigned long lastUpdate = 0;
//   static int index = 0;

//   // Text shown two characters at a time
//   const char message[] = "HELLO 123 ABC XYZ ";
//   int messageLength = sizeof(message) - 1;

//   if (millis() - lastUpdate > 700) {
//     lastUpdate = millis();

//     char leftChar = message[index];
//     char rightChar = message[(index + 1) % messageLength];

//     setTwoChars(leftChar, rightChar);

//     index++;
//     if (index >= messageLength) {
//       index = 0;
//     }
//   }

//   // Must run continuously for multiplexing
//   refreshMatrix();
// }

#include <Arduino.h>
#define ROWS 4
#define COLS 6

// Keep your exact assignment
int colPins[COLS] = {18, 17, 16, 15, 14, 8};
int rowPins[ROWS] = {5, 6, 9, 10};

// Based on your reference code:
// rows are active LOW
#define ROW_ON  LOW
#define ROW_OFF HIGH

// Columns act like sink side
#define COL_ON  HIGH
#define COL_OFF LOW

// One-pixel scan timing.
// If too dim: increase to 80 or 100.
// If flickering/too "perma": decrease to 20 or 30.
#define PIXEL_TIME_US 40

bool buffer[ROWS][COLS];

struct FontChar {
  char c;
  byte rows[4];
};

FontChar font[] = {
  {'0', {B111, B101, B101, B111}},
  {'1', {B010, B110, B010, B111}},
  {'2', {B111, B001, B010, B111}},
  {'3', {B111, B001, B011, B111}},
  {'4', {B101, B101, B111, B001}},
  {'5', {B111, B100, B111, B011}},
  {'6', {B111, B100, B111, B111}},
  {'7', {B111, B001, B010, B010}},
  {'8', {B111, B101, B111, B111}},
  {'9', {B111, B101, B111, B001}},

  {'A', {B111, B101, B111, B101}},
  {'B', {B110, B101, B110, B111}},
  {'C', {B111, B100, B100, B111}},
  {'D', {B110, B101, B101, B110}},
  {'E', {B111, B100, B110, B111}},
  {'F', {B111, B100, B110, B100}},
  {'G', {B111, B100, B101, B111}},
  {'H', {B101, B101, B111, B101}},
  {'I', {B111, B010, B010, B111}},
  {'J', {B001, B001, B101, B111}},
  {'K', {B101, B110, B100, B101}},
  {'L', {B100, B100, B100, B111}},
  {'M', {B101, B111, B101, B101}},
  {'N', {B101, B111, B111, B101}},
  {'O', {B111, B101, B101, B111}},
  {'P', {B111, B101, B111, B100}},
  {'Q', {B111, B101, B111, B001}},
  {'R', {B111, B101, B110, B101}},
  {'S', {B111, B100, B111, B011}},
  {'T', {B111, B010, B010, B010}},
  {'U', {B101, B101, B101, B111}},
  {'V', {B101, B101, B101, B010}},
  {'W', {B101, B101, B111, B111}},
  {'X', {B101, B010, B010, B101}},
  {'Y', {B101, B101, B010, B010}},
  {'Z', {B111, B001, B010, B111}},
  {' ', {B000, B000, B000, B000}},
};

int fontCount = sizeof(font) / sizeof(font[0]);

void allOff() {
  for (int c = 0; c < COLS; c++) {
    digitalWrite(colPins[c], COL_OFF);
  }

  for (int r = 0; r < ROWS; r++) {
    digitalWrite(rowPins[r], ROW_OFF);
  }
}

void clearBuffer() {
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      buffer[r][c] = false;
    }
  }
}

byte* getCharPattern(char ch) {
  if (ch >= 'a' && ch <= 'z') {
    ch = ch - 'a' + 'A';
  }

  for (int i = 0; i < fontCount; i++) {
    if (font[i].c == ch) {
      return font[i].rows;
    }
  }

  return font[fontCount - 1].rows;
}

void drawChar(char ch, int startCol) {
  byte* pattern = getCharPattern(ch);

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < 3; c++) {
      int realCol = startCol + c;

      if (realCol >= 0 && realCol < COLS) {
        buffer[r][realCol] = bitRead(pattern[r], 2 - c);
      }
    }
  }
}

void setTwoChars(char leftChar, char rightChar) {
  clearBuffer();
  drawChar(leftChar, 0);
  drawChar(rightChar, 3);
}

// Important function:
// Only one LED is ever enabled at a time.
void refreshOneLedAtATime() {
  for (int c = 0; c < COLS; c++) {
    for (int r = 0; r < ROWS; r++) {
      allOff();

      if (buffer[r][c]) {
        digitalWrite(rowPins[r], ROW_ON);
        digitalWrite(colPins[c], COL_ON);
        delayMicroseconds(PIXEL_TIME_US);
      } else {
        delayMicroseconds(2);
      }

      allOff();
    }
  }
}

void setup() {
  for (int c = 0; c < COLS; c++) {
    pinMode(colPins[c], OUTPUT);
  }

  for (int r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
  }

  allOff();

  setTwoChars('A', '1');
}

void drawScrollingText(const char* message, int scrollCol) {
  clearBuffer();

  int len = strlen(message);

  // 3 columns for character + 1 blank column spacing
  int charWidthWithSpace = 4;
  int totalTextCols = len * charWidthWithSpace;

  for (int displayCol = 0; displayCol < COLS; displayCol++) {
    int sourceCol = scrollCol + displayCol;

    // Wrap around message
    sourceCol = sourceCol % totalTextCols;

    int charIndex = sourceCol / charWidthWithSpace;
    int charCol = sourceCol % charWidthWithSpace;

    // charCol 0,1,2 = actual character pixels
    // charCol 3 = blank spacing column
    if (charCol == 3) {
      for (int r = 0; r < ROWS; r++) {
        buffer[r][displayCol] = false;
      }
    } else {
      byte* pattern = getCharPattern(message[charIndex]);

      for (int r = 0; r < ROWS; r++) {
        buffer[r][displayCol] = bitRead(pattern[r], 2 - charCol);
      }
    }
  }
}

void loop() {
  static unsigned long lastScroll = 0;
  static int scrollCol = 0;

  const char message[] = "HELLO 123 ABC XYZ   ";
  int len = strlen(message);
  int totalTextCols = len * 4; // 3 columns for character + 1 blank column spacing

  if (millis() - lastScroll >= 650) {
    lastScroll = millis();

    drawScrollingText(message, scrollCol);

    scrollCol++;

    if (scrollCol >= totalTextCols) {
      scrollCol = 0;
    }
  }

  refreshOneLedAtATime();
}
