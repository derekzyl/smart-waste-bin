#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <HX711.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>

// ==================== PIN DEFINITIONS ====================
// Ultrasonic Sensor
#define TRIG_PIN 4
#define ECHO_PIN 5

// PIR Motion Sensor
#define PIR_PIN 2

// Servo Motors (for bin lids)
#define SERVO_ORGANIC_PIN 18
#define SERVO_NON_ORGANIC_PIN 19

// Load Cell
#define LOAD_CELL_DOUT_PIN 16
#define LOAD_CELL_SCK_PIN 17

// LEDs (RGB or individual)
#define LED_RED_PIN 25
#define LED_GREEN_PIN 26
#define LED_BLUE_PIN 27

// Buzzer
#define BUZZER_PIN 14

// Keypad (using 2 buttons for simplicity, can be expanded)
#define KEYPAD_BUTTON1_PIN 12
#define KEYPAD_BUTTON2_PIN 13

// CAN (using ESP32's TWAI - Two-Wire Automotive Interface)
#define CAN_TX_PIN 21
#define CAN_RX_PIN 22

// ==================== GLOBAL VARIABLES ====================
// WiFi Credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* backend_url = "http://your-backend-url.com";

// Bin Configuration
const uint32_t BIN_ORGANIC_ID = 0x001;
const uint32_t BIN_NON_ORGANIC_ID = 0x002;
const float MAX_BIN_CAPACITY = 10.0; // kg
const float BIN_FULL_THRESHOLD = 9.0; // kg (90% full)

// Servo Objects
Servo servoOrganic;
Servo servoNonOrganic;

// Load Cell
HX711 scale;

// Web Server
AsyncWebServer server(80);
WebSocketsServer webSocket(81);

// State Variables
enum BinState {
  IDLE,
  DETECTING_MOTION,
  ANALYZING_MATERIAL,
  OPENING_BIN,
  BIN_OPEN,
  CLOSING_BIN,
  BIN_FULL,
  MAINTENANCE_MODE
};

BinState currentState = IDLE;
  uint32_t selectedBin = 0; // BIN_ORGANIC_ID or BIN_NON_ORGANIC_ID
float organicBinWeight = 0.0;
float nonOrganicBinWeight = 0.0;
bool isOrganicBinFull = false;
bool isNonOrganicBinFull = false;
unsigned long lastMotionTime = 0;
unsigned long binOpenTime = 0;
const unsigned long MOTION_TIMEOUT = 5000; // 5 seconds
const unsigned long BIN_OPEN_TIMEOUT = 10000; // 10 seconds
const unsigned long BIN_CLOSE_DELAY = 3000; // 3 seconds

// Material Detection
String detectedMaterial = "";
bool materialDetectionComplete = false;
unsigned long materialDetectionStartTime = 0;

// ==================== FUNCTION DECLARATIONS ====================
void setupWiFi();
void setupCAN();
void setupWebServer();
void setupWebSocket();
void handleMotionDetection();
void handleMaterialDetection();
void openBin(uint8_t binType);
void closeBin(uint8_t binType);
void updateBinLevel();
void updateLEDs();
void sendToBackend(String endpoint, JsonDocument& doc);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
float getDistance();
void checkKeypad();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize GPIO pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(KEYPAD_BUTTON1_PIN, INPUT_PULLUP);
  pinMode(KEYPAD_BUTTON2_PIN, INPUT_PULLUP);
  
  // Initialize Servos
  servoOrganic.attach(SERVO_ORGANIC_PIN);
  servoNonOrganic.attach(SERVO_NON_ORGANIC_PIN);
  servoOrganic.write(0); // Close position
  servoNonOrganic.write(0); // Close position
  
  // Initialize Load Cell
  scale.begin(LOAD_CELL_DOUT_PIN, LOAD_CELL_SCK_PIN);
  scale.set_scale(2280.f); // Calibration factor (adjust based on your load cell)
  scale.tare();
  
  // Initialize WiFi
  setupWiFi();
  
  // Initialize CAN
  setupCAN();
  
  // Initialize Web Server
  setupWebServer();
  
  // Initialize WebSocket
  setupWebSocket();
  
  Serial.println("Smart Waste Bin System Initialized");
  updateLEDs();
}

// ==================== MAIN LOOP ====================
void loop() {
  webSocket.loop();
  
  // Check keypad for manual override
  checkKeypad();
  
  // Update bin levels
  updateBinLevel();
  
  // State Machine
  switch(currentState) {
    case IDLE:
      handleMotionDetection();
      updateLEDs();
      break;
      
    case DETECTING_MOTION:
      if (millis() - lastMotionTime > MOTION_TIMEOUT) {
        currentState = IDLE;
      } else {
        currentState = ANALYZING_MATERIAL;
        materialDetectionStartTime = millis();
        // Request material detection from ESP32-CAM via CAN
        sendCANMessage(0x100, "DETECT_MATERIAL");
      }
      break;
      
    case ANALYZING_MATERIAL:
      handleMaterialDetection();
      if (materialDetectionComplete) {
        if (detectedMaterial == "ORGANIC") {
          selectedBin = BIN_ORGANIC_ID;
        } else if (detectedMaterial == "NON_ORGANIC") {
          selectedBin = BIN_NON_ORGANIC_ID;
        }
        currentState = OPENING_BIN;
        materialDetectionComplete = false;
      }
      // Timeout after 5 seconds
      if (millis() - materialDetectionStartTime > 5000) {
        detectedMaterial = "UNKNOWN";
        selectedBin = BIN_ORGANIC_ID; // Default to organic
        currentState = OPENING_BIN;
      }
      break;
      
    case OPENING_BIN:
      if (selectedBin == BIN_ORGANIC_ID && !isOrganicBinFull) {
        openBin(0); // 0 = organic
        currentState = BIN_OPEN;
        binOpenTime = millis();
      } else if (selectedBin == BIN_NON_ORGANIC_ID && !isNonOrganicBinFull) {
        openBin(1); // 1 = non-organic
        currentState = BIN_OPEN;
        binOpenTime = millis();
      } else {
        // Bin is full, cannot open
        digitalWrite(BUZZER_PIN, HIGH);
        delay(500);
        digitalWrite(BUZZER_PIN, LOW);
        currentState = BIN_FULL;
        delay(2000);
        currentState = IDLE;
      }
      break;
      
    case BIN_OPEN:
      updateLEDs();
      // Check if motion is still detected
      if (digitalRead(PIR_PIN) == LOW || millis() - lastMotionTime > MOTION_TIMEOUT) {
        if (millis() - binOpenTime > BIN_CLOSE_DELAY) {
          currentState = CLOSING_BIN;
        }
      } else {
        lastMotionTime = millis();
      }
      break;
      
    case CLOSING_BIN:
      if (selectedBin == BIN_ORGANIC_ID) {
        closeBin(0);
      } else {
        closeBin(1);
      }
      currentState = IDLE;
      // Send data to backend
      sendBinDataToBackend();
      break;
      
    case BIN_FULL:
      updateLEDs();
      break;
      
    case MAINTENANCE_MODE:
      // Manual override mode
      break;
  }
  
  delay(50);
}

// ==================== WIFI SETUP ====================
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Connection Failed - Operating in AP Mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("SmartBin_AP", "12345678");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  }
}

// ==================== CAN SETUP ====================
void setupCAN() {
  // Initialize TWAI (CAN) on ESP32
  // Note: ESP32 uses TWAI instead of traditional CAN
  // This is a simplified implementation
  Serial.println("CAN/TWAI initialized");
}

void sendCANMessage(uint32_t id, String message) {
  // Simplified CAN message sending
  // In real implementation, use ESP32 TWAI library
  Serial.printf("CAN TX: ID=0x%03X, Message=%s\n", id, message.c_str());
}

bool receiveCANMessage(uint32_t* id, String* message) {
  // Simplified CAN message receiving
  // In real implementation, use ESP32 TWAI library
  return false;
}

// ==================== WEB SERVER SETUP ====================
void setupWebServer() {
  // Root endpoint
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<html><body><h1>Smart Waste Bin API</h1><p>Use WebSocket on port 81 for real-time data</p></body></html>");
  });
  
  // Get bin status
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    doc["organic_level"] = organicBinWeight;
    doc["non_organic_level"] = nonOrganicBinWeight;
    doc["organic_full"] = isOrganicBinFull;
    doc["non_organic_full"] = isNonOrganicBinFull;
    doc["state"] = currentState;
    doc["bin_organic_id"] = BIN_ORGANIC_ID;
    doc["bin_non_organic_id"] = BIN_NON_ORGANIC_ID;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Open bin manually
  server.on("/api/open", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("bin", true)) {
      String binParam = request->getParam("bin", true)->value();
      if (binParam == "organic" && !isOrganicBinFull) {
        openBin(0);
        request->send(200, "application/json", "{\"status\":\"opened\",\"bin\":\"organic\"}");
      } else if (binParam == "non_organic" && !isNonOrganicBinFull) {
        openBin(1);
        request->send(200, "application/json", "{\"status\":\"opened\",\"bin\":\"non_organic\"}");
      } else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Bin full or invalid\"}");
      }
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing bin parameter\"}");
    }
  });
  
  // Close bin manually
  server.on("/api/close", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("bin", true)) {
      String binParam = request->getParam("bin", true)->value();
      if (binParam == "organic") {
        closeBin(0);
        request->send(200, "application/json", "{\"status\":\"closed\",\"bin\":\"organic\"}");
      } else if (binParam == "non_organic") {
        closeBin(1);
        request->send(200, "application/json", "{\"status\":\"closed\",\"bin\":\"non_organic\"}");
      }
    } else {
      request->send(400, "application/json", "{\"status\":\"error\"}");
    }
  });
  
  // Maintenance mode
  server.on("/api/maintenance", HTTP_POST, [](AsyncWebServerRequest *request){
    if (currentState == MAINTENANCE_MODE) {
      currentState = IDLE;
      request->send(200, "application/json", "{\"status\":\"normal_mode\"}");
    } else {
      currentState = MAINTENANCE_MODE;
      request->send(200, "application/json", "{\"status\":\"maintenance_mode\"}");
    }
  });
  
  server.begin();
}

// ==================== WEBSOCKET SETUP ====================
void setupWebSocket() {
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("Client [%u] disconnected\n", num);
      break;
      
    case WStype_CONNECTED:
      Serial.printf("Client [%u] connected from %s\n", num, payload);
      // Send initial status
      sendWebSocketStatus(num);
      break;
      
    case WStype_TEXT:
      handleWebSocketMessage(num, (char*)payload);
      break;
      
    default:
      break;
  }
}

void sendWebSocketStatus(uint8_t clientNum) {
  DynamicJsonDocument doc(1024);
  doc["organic_level"] = organicBinWeight;
  doc["non_organic_level"] = nonOrganicBinWeight;
  doc["organic_full"] = isOrganicBinFull;
  doc["non_organic_full"] = isNonOrganicBinFull;
  doc["state"] = currentState;
  doc["bin_organic_id"] = BIN_ORGANIC_ID;
  doc["bin_non_organic_id"] = BIN_NON_ORGANIC_ID;
  
  String response;
  serializeJson(doc, response);
  webSocket.sendTXT(clientNum, response);
}

void handleWebSocketMessage(uint8_t clientNum, String message) {
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, message);
  
  String command = doc["command"];
  
  if (command == "open_organic" && !isOrganicBinFull) {
    openBin(0);
  } else if (command == "open_non_organic" && !isNonOrganicBinFull) {
    openBin(1);
  } else if (command == "close_organic") {
    closeBin(0);
  } else if (command == "close_non_organic") {
    closeBin(1);
  } else if (command == "get_status") {
    sendWebSocketStatus(clientNum);
  }
}

// ==================== MOTION DETECTION ====================
void handleMotionDetection() {
  if (digitalRead(PIR_PIN) == HIGH) {
    lastMotionTime = millis();
    if (currentState == IDLE) {
      currentState = DETECTING_MOTION;
      Serial.println("Motion detected!");
    }
  }
}

// ==================== MATERIAL DETECTION ====================
void handleMaterialDetection() {
  uint32_t canId;
  String canMessage;
  
  if (receiveCANMessage(&canId, &canMessage)) {
    if (canId == 0x200) { // Response from ESP32-CAM
      if (canMessage.startsWith("MATERIAL:")) {
        detectedMaterial = canMessage.substring(9);
        materialDetectionComplete = true;
        Serial.printf("Material detected: %s\n", detectedMaterial.c_str());
      }
    }
  }
}

// ==================== BIN CONTROL ====================
void openBin(uint8_t binType) {
  if (binType == 0) { // Organic
    servoOrganic.write(90); // Open position
    Serial.println("Organic bin opened");
  } else { // Non-organic
    servoNonOrganic.write(90); // Open position
    Serial.println("Non-organic bin opened");
  }
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
}

void closeBin(uint8_t binType) {
  if (binType == 0) { // Organic
    servoOrganic.write(0); // Close position
    Serial.println("Organic bin closed");
  } else { // Non-organic
    servoNonOrganic.write(0); // Close position
    Serial.println("Non-organic bin closed");
  }
}

// ==================== BIN LEVEL MONITORING ====================
void updateBinLevel() {
  // Read load cell (simplified - in real implementation, you'd have separate load cells)
  float weight = scale.get_units(5);
  
  // For demonstration, we'll use ultrasonic sensor to estimate bin level
  float distance = getDistance();
  float level = map(distance, 5, 50, 100, 0); // Adjust based on your bin dimensions
  level = constrain(level, 0, 100);
  
  // Estimate weight based on level (simplified)
  organicBinWeight = (level / 100.0) * MAX_BIN_CAPACITY;
  nonOrganicBinWeight = (level / 100.0) * MAX_BIN_CAPACITY;
  
  // Check if bins are full
  isOrganicBinFull = organicBinWeight >= BIN_FULL_THRESHOLD;
  isNonOrganicBinFull = nonOrganicBinWeight >= BIN_FULL_THRESHOLD;
  
  if (isOrganicBinFull || isNonOrganicBinFull) {
    currentState = BIN_FULL;
  }
}

float getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH);
  float distance = (duration * 0.034) / 2; // Speed of sound in cm
  
  return distance;
}

// ==================== LED CONTROL ====================
void updateLEDs() {
  if (currentState == BIN_FULL) {
    // Red blinking
    digitalWrite(LED_RED_PIN, HIGH);
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_BLUE_PIN, LOW);
  } else if (currentState == BIN_OPEN) {
    // Green solid
    digitalWrite(LED_RED_PIN, LOW);
    digitalWrite(LED_GREEN_PIN, HIGH);
    digitalWrite(LED_BLUE_PIN, LOW);
  } else if (isOrganicBinFull || isNonOrganicBinFull) {
    // Yellow/Orange (Red + Green)
    digitalWrite(LED_RED_PIN, HIGH);
    digitalWrite(LED_GREEN_PIN, HIGH);
    digitalWrite(LED_BLUE_PIN, LOW);
  } else {
    // Blue (normal operation)
    digitalWrite(LED_RED_PIN, LOW);
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_BLUE_PIN, HIGH);
  }
}

// ==================== KEYPAD CONTROL ====================
void checkKeypad() {
  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 200;
  
  if (millis() - lastDebounceTime > debounceDelay) {
    if (digitalRead(KEYPAD_BUTTON1_PIN) == LOW) {
      // Button 1: Open organic bin (if not full)
      if (!isOrganicBinFull) {
        openBin(0);
        delay(3000);
        closeBin(0);
      }
      lastDebounceTime = millis();
    }
    
    if (digitalRead(KEYPAD_BUTTON2_PIN) == LOW) {
      // Button 2: Open non-organic bin (if not full)
      if (!isNonOrganicBinFull) {
        openBin(1);
        delay(3000);
        closeBin(1);
      }
      lastDebounceTime = millis();
    }
  }
}

// ==================== BACKEND COMMUNICATION ====================
void sendBinDataToBackend() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  
  DynamicJsonDocument doc(1024);
  doc["bin_organic_id"] = BIN_ORGANIC_ID;
  doc["bin_non_organic_id"] = BIN_NON_ORGANIC_ID;
  doc["organic_weight"] = organicBinWeight;
  doc["non_organic_weight"] = nonOrganicBinWeight;
  doc["organic_full"] = isOrganicBinFull;
  doc["non_organic_full"] = isNonOrganicBinFull;
  doc["timestamp"] = millis();
  
  String json;
  serializeJson(doc, json);
  
  // Send HTTP POST to backend
  WiFiClient client;
  HTTPClient http;
  
  http.begin(client, String(backend_url) + "/api/bins/update");
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.POST(json);
  if (httpResponseCode > 0) {
    Serial.printf("Backend response: %d\n", httpResponseCode);
  } else {
    Serial.printf("Backend error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  
  http.end();
}
