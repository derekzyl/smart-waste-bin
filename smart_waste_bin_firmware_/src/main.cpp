#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <HX711.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>

// ==================== PIN DEFINITIONS ====================
// Ultrasonic Sensors
#define TRIG_PIN_PRESENCE 5
#define ECHO_PIN_PRESENCE 17

#define TRIG_PIN_ORG 22
#define ECHO_PIN_ORG 23

#define TRIG_PIN_INORG 16
#define ECHO_PIN_INORG 4

// Servo Motors (for bin lids)
#define SERVO_ORGANIC_PIN 18
#define SERVO_NON_ORGANIC_PIN 19

#define CAM_TRIGGER_PIN 21




// LEDs
// Organic Bin LEDs
#define LED_ORG_RED_PIN 33   // Full
#define LED_ORG_GREEN_PIN 26 // Available

// Inorganic Bin LEDs
#define LED_INORG_RED_PIN 25   // Full
#define LED_INORG_GREEN_PIN 32 // Available

// Buzzer
#define BUZZER_PIN 15

// Keypad
#define KEYPAD_BUTTON1_PIN 23
#define KEYPAD_BUTTON2_PIN 2

// CAN (using ESP32's TWAI)
#define CAN_TX_PIN 21
#define CAN_RX_PIN 22

// ==================== GLOBAL VARIABLES ====================
// WiFi Credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* backend_url = "http://your-backend-url.com";

// Bin Configuration
const char* BIN_ORGANIC_ID = "0x001";
const char* BIN_NON_ORGANIC_ID = "0x002";
const float MAX_BIN_HEIGHT_CM = 50.0; // cm from sensor to bottom (example)
const float BIN_FULL_THRESHOLD_CM = 5.0; // cm from sensor to waste

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
  BIN_FULL_ERROR, // General error state if critical failure
  MAINTENANCE_MODE
};

BinState currentState = IDLE;
String selectedBin = ""; // Changed to String

// Level & Status Variables
float organicBinLevelCm = 0.0;
float nonOrganicBinLevelCm = 0.0;
int organicBinLevelPct = 0;
int nonOrganicBinLevelPct = 0;
bool isOrganicBinFull = false;
bool isNonOrganicBinFull = false;

// Timing
unsigned long lastMotionTime = 0;
unsigned long binOpenTime = 0;
const unsigned long MOTION_TIMEOUT = 5000; // 5 seconds
const unsigned long BIN_OPEN_TIMEOUT = 10000; // 10 seconds
const unsigned long BIN_CLOSE_DELAY = 3000; // 3 seconds
unsigned long lastLevelCheckTime = 0;
const unsigned long LEVEL_CHECK_INTERVAL = 2000; // Check levels every 2s

// Presence Logic
const float PRESENCE_THRESHOLD_CM = 50.0; // Detect user within 50cm

// Material Detection
String detectedMaterial = "";
bool materialDetectionComplete = false;
unsigned long materialDetectionStartTime = 0;

// ==================== FUNCTION DECLARATIONS ====================
void setupWiFi();
void setupCAN();
void setupWebServer();
void setupWebSocket();
void handlePresenceDetection();
void handleMaterialDetection();
void openBin(uint8_t binType);
void closeBin(uint8_t binType);
void updateBinLevels();
void updateLEDs();
void sendToBackend(String endpoint, JsonDocument& doc);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
float getDistance(int trigPin, int echoPin);
void checkKeypad();
void sendCANMessage(uint32_t id, String message);
bool receiveCANMessage(uint32_t* id, String* message);
void sendWebSocketStatus(uint8_t clientNum);
void handleWebSocketMessage(uint8_t clientNum, String message);
void sendBinDataToBackend();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize GPIO pins
  pinMode(TRIG_PIN_PRESENCE, OUTPUT);
  pinMode(ECHO_PIN_PRESENCE, INPUT);
  
  pinMode(TRIG_PIN_ORG, OUTPUT);
  pinMode(ECHO_PIN_ORG, INPUT);
  
  pinMode(TRIG_PIN_INORG, OUTPUT);
  pinMode(ECHO_PIN_INORG, INPUT);
  
  pinMode(LED_ORG_RED_PIN, OUTPUT);
  pinMode(LED_ORG_GREEN_PIN, OUTPUT);
  pinMode(LED_INORG_RED_PIN, OUTPUT);
  pinMode(LED_INORG_GREEN_PIN, OUTPUT);
  
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
  scale.set_scale(2280.f); // Calibration factor
  scale.tare();
  
  // Initialize WiFi
  setupWiFi();
  
  // Initialize CAN
  setupCAN();
  
  // Initialize Web Server
  setupWebServer();
  
  // Initialize WebSocket
  setupWebSocket();
  
  Serial.println("Smart Waste Bin System Initialized - Dual Ultrasonic Version");
  updateLEDs();
}

// ==================== MAIN LOOP ====================
void loop() {
  webSocket.loop();
  
  // Check keypad for manual override
  checkKeypad();
  
  // Update bin levels periodically
  if (millis() - lastLevelCheckTime > LEVEL_CHECK_INTERVAL) {
    updateBinLevels();
    updateLEDs();
    lastLevelCheckTime = millis();
  }
  
  // State Machine
  switch(currentState) {
    case IDLE:
      handlePresenceDetection();
      break;
      
    case DETECTING_MOTION: // Triggered by Presence Ultrasonic
      currentState = ANALYZING_MATERIAL;
      materialDetectionStartTime = millis();
      // Request material detection from ESP32-CAM via CAN
      sendCANMessage(0x100, "DETECT_MATERIAL");
      Serial.println("Presence detected -> Identifying Material...");
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
        Serial.println("Material detection timeout");
        // Default behavior on timeout? Maybe fallback to manual or blink error
        // For now, let's reset to IDLE to avoid random opening
        currentState = IDLE; 
      }
      break;
      
    case OPENING_BIN:
      if (selectedBin == BIN_ORGANIC_ID) {
        if (!isOrganicBinFull) {
          openBin(0); // 0 = organic
          currentState = BIN_OPEN;
          binOpenTime = millis();
          lastMotionTime = millis(); // Reset motion timer
        } else {
             Serial.println("Organic bin FULL - Cannot open");
             // Blink Red LED or beep
             digitalWrite(BUZZER_PIN, HIGH); delay(200); digitalWrite(BUZZER_PIN, LOW);
             currentState = IDLE;
        }
      } else if (selectedBin == BIN_NON_ORGANIC_ID) {
        if (!isNonOrganicBinFull) {
          openBin(1); // 1 = non-organic
          currentState = BIN_OPEN;
          binOpenTime = millis();
          lastMotionTime = millis(); // Reset motion timer
        } else {
             Serial.println("Non-Organic bin FULL - Cannot open");
             digitalWrite(BUZZER_PIN, HIGH); delay(200); digitalWrite(BUZZER_PIN, LOW);
             currentState = IDLE;
        }
      } 
      break;
      
    case BIN_OPEN:
      // While bin is open, check if user is still present to keep it open
      // or if timeout has passed
      {
        float distance = getDistance(TRIG_PIN_PRESENCE, ECHO_PIN_PRESENCE);
        if (distance < PRESENCE_THRESHOLD_CM) {
            lastMotionTime = millis(); // User still there
        }
        
        if (millis() - lastMotionTime > MOTION_TIMEOUT || millis() - binOpenTime > BIN_OPEN_TIMEOUT) {
             currentState = CLOSING_BIN;
        }
      }
      break;
      
    case CLOSING_BIN:
      if (selectedBin == BIN_ORGANIC_ID) {
        closeBin(0);
      } else {
        closeBin(1);
      }
      currentState = IDLE;
      sendBinDataToBackend(); // Log event
      break;
      
    case BIN_FULL_ERROR:
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
  Serial.println("CAN/TWAI initialized");
}

void sendCANMessage(uint32_t id, String message) {
  Serial.printf("CAN TX: ID=0x%03X, Message=%s\n", id, message.c_str());
}

bool receiveCANMessage(uint32_t* id, String* message) {
  // Placeholder implementation
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
    doc["organic_level_cm"] = organicBinLevelCm;
    doc["non_organic_level_cm"] = nonOrganicBinLevelCm;
    doc["organic_full"] = isOrganicBinFull;
    doc["non_organic_full"] = isNonOrganicBinFull;
    doc["state"] = currentState;
    
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
      startWebSocketStatus(num);
      break;
    case WStype_TEXT:
      handleWebSocketMessage(num, (char*)payload);
      break;
    default:
      break;
  }
}

void startWebSocketStatus(uint8_t clientNum) {
    sendWebSocketStatus(clientNum);
}

void sendWebSocketStatus(uint8_t clientNum) {
  DynamicJsonDocument doc(1024);
  doc["organic_level"] = organicBinLevelCm;
  doc["non_organic_level"] = nonOrganicBinLevelCm;
  doc["organic_full"] = isOrganicBinFull;
  doc["non_organic_full"] = isNonOrganicBinFull;
  doc["state"] = currentState;
  
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

// ==================== PRESENCE DETECTION ====================
void handlePresenceDetection() {
  float distance = getDistance(TRIG_PIN_PRESENCE, ECHO_PIN_PRESENCE);
  
  // Filter 0 readings (invalid) and threshold
  if (distance > 0 && distance < PRESENCE_THRESHOLD_CM) {
      // Debounce? Maybe ensure it's close for a few readings.
      // For simplicity, just trigger:
      currentState = DETECTING_MOTION;
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
void updateBinLevels() {
  // Organic
  float distOrg = getDistance(TRIG_PIN_ORG, ECHO_PIN_ORG);
  if (distOrg > 0) {
      organicBinLevelCm = distOrg;
      organicBinLevelPct = map(constrain(distOrg, BIN_FULL_THRESHOLD_CM, MAX_BIN_HEIGHT_CM), MAX_BIN_HEIGHT_CM, BIN_FULL_THRESHOLD_CM, 0, 100);
      
      if (organicBinLevelPct >= 90) {
          isOrganicBinFull = true;
      } else {
          isOrganicBinFull = false;
      }
  }
  
  // Non-Organic
  float distInorg = getDistance(TRIG_PIN_INORG, ECHO_PIN_INORG);
  if (distInorg > 0) {
      nonOrganicBinLevelCm = distInorg;
      nonOrganicBinLevelPct = map(constrain(distInorg, BIN_FULL_THRESHOLD_CM, MAX_BIN_HEIGHT_CM), MAX_BIN_HEIGHT_CM, BIN_FULL_THRESHOLD_CM, 0, 100);
      
      if (nonOrganicBinLevelPct >= 90) {
          isNonOrganicBinFull = true;
      } else {
          isNonOrganicBinFull = false;
      }
  }
}

float getDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH);
  float distance = (duration * 0.034) / 2; // Speed of sound in cm
  return distance;
}

// ==================== LED CONTROL ====================
void updateLEDs() {
    // Organic Indicators
    if (isOrganicBinFull) {
        digitalWrite(LED_ORG_RED_PIN, HIGH);
        digitalWrite(LED_ORG_GREEN_PIN, LOW);
    } else {
        digitalWrite(LED_ORG_RED_PIN, LOW);
        digitalWrite(LED_ORG_GREEN_PIN, HIGH);
    }
    
    // Inorganic Indicators
    if (isNonOrganicBinFull) {
        digitalWrite(LED_INORG_RED_PIN, HIGH);
        digitalWrite(LED_INORG_GREEN_PIN, LOW);
    } else {
        digitalWrite(LED_INORG_RED_PIN, LOW);
        digitalWrite(LED_INORG_GREEN_PIN, HIGH);
    }
}

// ==================== KEYPAD CONTROL ====================
void checkKeypad() {
  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 200;
  
  if (millis() - lastDebounceTime > debounceDelay) {
    if (digitalRead(KEYPAD_BUTTON1_PIN) == LOW) { // Organic Button
      // Toggle or Open? Let's say open for 5 sec
      if (!isOrganicBinFull) {
        openBin(0);
        delay(3000);
        closeBin(0);
      }
      lastDebounceTime = millis();
    }
    
    if (digitalRead(KEYPAD_BUTTON2_PIN) == LOW) { // Inorganic Button
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
  doc["organic_level"] = organicBinLevelPct;
  doc["non_organic_level"] = nonOrganicBinLevelPct;
  // Send dummy weight or calculated weight if needed, but we prefer level
  doc["organic_weight"] = (organicBinLevelPct / 100.0) * 10.0; // Simulated weight
  doc["non_organic_weight"] = (nonOrganicBinLevelPct / 100.0) * 10.0; // Simulated weight
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
