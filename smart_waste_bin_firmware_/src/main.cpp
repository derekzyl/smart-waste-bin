/**
 * ESP32 Smart Waste Bin Main Controller - NewPing Version
 * 
// Communication with ESP32-CAM:
// - Trigger: GPIO 21 → ESP32-CAM GPIO 16 (was 13)
// - UART RX: GPIO 35 (RX) ← ESP32-CAM GPIO 14 (TX) (was 16)
// 
// GPIO 17 is used ONLY for ultrasonic ECHO (INPUT)
// No UART TX needed - ESP32 only receives data from CAM */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <NewPing.h>  // ← NEW: WiFi-safe ultrasonic library

// ==================== PIN DEFINITIONS ====================
// Ultrasonic Sensors
#define TRIG_PIN_PRESENCE 17
#define ECHO_PIN_PRESENCE 14

#define TRIG_PIN_ORG 22
#define ECHO_PIN_ORG 23

#define TRIG_PIN_INORG 27
#define ECHO_PIN_INORG 16

// Maximum distance for ultrasonic sensors (in cm)
#define MAX_DISTANCE 200

// Servo Motors (for bin lids)
#define SERVO_ORGANIC_PIN 18
#define SERVO_NON_ORGANIC_PIN 19

// Camera Trigger Pin - Connected to ESP32-CAM GPIO 16
#define CAM_TRIGGER_PIN 21

// LEDs
#define LED_ORG_RED_PIN 33      // Organic bin full
#define LED_ORG_GREEN_PIN 26    // Organic bin available

#define LED_INORG_RED_PIN 25    // Inorganic bin full
#define LED_INORG_GREEN_PIN 32  // Inorganic bin available

// UART Communication with ESP32-CAM (RX Only)
#define UART_RX_PIN       35   
#define UART_BAUD         115200

// ==================== NEWPING OBJECTS ====================
NewPing sonarPresence(TRIG_PIN_PRESENCE, ECHO_PIN_PRESENCE, MAX_DISTANCE);
NewPing sonarOrganic(TRIG_PIN_ORG, ECHO_PIN_ORG, MAX_DISTANCE);
NewPing sonarInorganic(TRIG_PIN_INORG, ECHO_PIN_INORG, MAX_DISTANCE);

// ==================== GLOBAL VARIABLES ====================
// WiFi Credentials
const char* ssid = "cybergenii";
const char* password = "12341234";
const char* backend_url = "https://xenophobic-netta-cybergenii-1584fde7.koyeb.app";

// Local access password
const char* local_access_password = "SmartBin2025";

// Static IP Configuration
IPAddress local_IP(192, 168, 43, 200);
IPAddress gateway(192, 168, 43, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

// Bin IDs
const char* BIN_ORGANIC_ID = "0x001";
const char* BIN_NON_ORGANIC_ID = "0x002";
const float MAX_BIN_HEIGHT_CM = 50.0; 
const float BIN_FULL_THRESHOLD_CM = 5.0; 

// Servo Objects
Servo servoOrganic;
Servo servoNonOrganic;

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
  CLOSING_BIN
};

BinState currentState = IDLE;
String selectedBin = ""; 

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
const unsigned long MOTION_TIMEOUT = 5000; 
const unsigned long BIN_OPEN_TIMEOUT = 10000; 
unsigned long lastLevelCheckTime = 0;
const unsigned long LEVEL_CHECK_INTERVAL = 2000; 
unsigned long lastBackendSyncTime = 0;
const unsigned long BACKEND_SYNC_INTERVAL = 5000;

// Presence Logic
const float PRESENCE_THRESHOLD_CM = 50.0;
const int PRESENCE_DEBOUNCE_COUNT = 3;  // ← NEW: Require 3 consecutive detections

// Material Detection
String detectedMaterial = "";
float detectedConfidence = 0.0;
bool materialDetectionComplete = false;
unsigned long materialDetectionStartTime = 0;
const unsigned long MATERIAL_DETECTION_TIMEOUT = 10000;

// ==================== FUNCTION DECLARATIONS ====================
void setupWiFi();
void setupWebServer();
void setupWebSocket();
void setupUART();
void handlePresenceDetection();
void openBin(uint8_t binType);
void closeBin(uint8_t binType);
void updateBinLevels();
void updateLEDs();
void sendBinDataToBackend();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
float getDistance(NewPing &sonar);  // ← CHANGED: Uses NewPing object
void triggerCameraDetection();
void processUARTData();
bool authenticateRequest(AsyncWebServerRequest *request);
void sendWebSocketStatus(uint8_t clientNum);
void handleWebSocketMessage(uint8_t clientNum, String message);
void pollBackendCommands();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=================================");
  Serial.println("Smart Waste Bin Main Controller");
  Serial.println("Using NewPing Library for WiFi-Safe Ultrasonic");
  Serial.println("=================================");
  
  // Initialize GPIO pins (NewPing handles TRIG/ECHO internally)
  pinMode(LED_ORG_RED_PIN, OUTPUT);
  pinMode(LED_ORG_GREEN_PIN, OUTPUT);
  pinMode(LED_INORG_RED_PIN, OUTPUT);
  pinMode(LED_INORG_GREEN_PIN, OUTPUT);
  
  pinMode(CAM_TRIGGER_PIN, OUTPUT);
  digitalWrite(CAM_TRIGGER_PIN, LOW);
  
  // Initialize Servos
  servoOrganic.attach(SERVO_ORGANIC_PIN);
  servoNonOrganic.attach(SERVO_NON_ORGANIC_PIN);
  servoOrganic.write(0);
  servoNonOrganic.write(0);
  
  // Test ultrasonic sensors BEFORE WiFi
  Serial.println("\n>>> Testing Ultrasonic Sensors BEFORE WiFi <<<");
  delay(500);
  Serial.printf("  Presence: %.2f cm\n", getDistance(sonarPresence));
  delay(100);
  Serial.printf("  Organic:  %.2f cm\n", getDistance(sonarOrganic));
  delay(100);
  Serial.printf("  Inorganic: %.2f cm\n", getDistance(sonarInorganic));
  
  // Initialize WiFi
  setupWiFi();
  
  // Test ultrasonic sensors AFTER WiFi
  Serial.println("\n>>> Testing Ultrasonic Sensors AFTER WiFi <<<");
  delay(500);
  Serial.printf("  Presence: %.2f cm\n", getDistance(sonarPresence));
  delay(100);
  Serial.printf("  Organic:  %.2f cm\n", getDistance(sonarOrganic));
  delay(100);
  Serial.printf("  Inorganic: %.2f cm\n", getDistance(sonarInorganic));
  
  // Initialize UART
  setupUART();
  
  // Initialize Web Server
  setupWebServer();
  
  // Initialize WebSocket
  setupWebSocket();
  
  Serial.println("\n=================================");
  Serial.println("SYSTEM READY");
  Serial.println("=================================");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("\nCommunication Setup:");
  Serial.println("  ESP32-CAM Trigger: GPIO 21 → ESP32-CAM GPIO 16");
  Serial.println("  UART RX: GPIO 35 ← ESP32-CAM GPIO 14 (TX)");
  Serial.println("\nUltrasonic Pins (NewPing Library):");
  Serial.printf("  PRESENCE: Trigger=%d, Echo=%d\n", TRIG_PIN_PRESENCE, ECHO_PIN_PRESENCE);
  Serial.printf("  ORGANIC:  Trigger=%d, Echo=%d\n", TRIG_PIN_ORG, ECHO_PIN_ORG);
  Serial.printf("  INORGANIC: Trigger=%d, Echo=%d\n", TRIG_PIN_INORG, ECHO_PIN_INORG);
  Serial.println("  Password: " + String(local_access_password));
  Serial.println("=================================\n");
  
  updateLEDs();
}

// ==================== MAIN LOOP ====================
void loop() {
  webSocket.loop();
  
  // Process incoming UART data from ESP32-CAM
  processUARTData();
  
  // Update bin levels periodically
  if (millis() - lastLevelCheckTime > LEVEL_CHECK_INTERVAL) {
    updateBinLevels();
    updateLEDs();
    lastLevelCheckTime = millis();
  }
  
  // Sync with backend periodically
  if (millis() - lastBackendSyncTime > BACKEND_SYNC_INTERVAL) {
    sendBinDataToBackend();
    pollBackendCommands();
    lastBackendSyncTime = millis();
  }
  
  // State Machine
  switch(currentState) {
    case IDLE:
      handlePresenceDetection();
      break;
      
    case DETECTING_MOTION:
      currentState = ANALYZING_MATERIAL;
      materialDetectionStartTime = millis();
      
      // Trigger ESP32-CAM via GPIO pin
      triggerCameraDetection();
      Serial.println("\n>>> PRESENCE DETECTED - Triggering camera <<<");
      break;
      
    case ANALYZING_MATERIAL:
      // Wait for UART response from ESP32-CAM
      if (materialDetectionComplete) {
        // Decision based on detected material
        if (detectedMaterial == "ORGANIC") {
          selectedBin = BIN_ORGANIC_ID;
          Serial.println(">>> Material: ORGANIC - Opening organic bin");
        } else if (detectedMaterial == "NON_ORGANIC" || detectedMaterial == "INORGANIC") {
          selectedBin = BIN_NON_ORGANIC_ID;
          Serial.println(">>> Material: NON_ORGANIC - Opening inorganic bin");
        } else {
          Serial.println(">>> Material: UNKNOWN - Returning to IDLE");
          currentState = IDLE;
          break;
        }
        
        currentState = OPENING_BIN;
        materialDetectionComplete = false;
      }
      
      // Timeout protection
      if (millis() - materialDetectionStartTime > MATERIAL_DETECTION_TIMEOUT) {
        Serial.println(">>> Material detection TIMEOUT - Returning to IDLE");
        currentState = IDLE;
        materialDetectionComplete = false;
      }
      break;
      
    case OPENING_BIN:
      if (selectedBin == BIN_ORGANIC_ID) {
        if (!isOrganicBinFull) {
          openBin(0);
          currentState = BIN_OPEN;
          binOpenTime = millis();
          lastMotionTime = millis(); 
        } else {
          Serial.println("❌ Organic bin FULL - Cannot open");
          currentState = IDLE;
        }
      } else if (selectedBin == BIN_NON_ORGANIC_ID) {
        if (!isNonOrganicBinFull) {
          openBin(1);
          currentState = BIN_OPEN;
          binOpenTime = millis();
          lastMotionTime = millis(); 
        } else {
          Serial.println("❌ Non-Organic bin FULL - Cannot open");
          currentState = IDLE;
        }
      } 
      break;
      
    case BIN_OPEN:
      {
        float distance = getDistance(sonarPresence);
        if (distance > 0 && distance < PRESENCE_THRESHOLD_CM) {
          lastMotionTime = millis(); 
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
      Serial.println(">>> Returning to IDLE\n");
      currentState = IDLE;
      sendBinDataToBackend(); 
      break;
  }
  
  delay(50);
}

// ==================== WIFI SETUP ====================
void setupWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  
  // Using DHCP for reliable connectivity
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n  ✓ WiFi Connected!");
    Serial.print("  IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("  DNS: ");
    Serial.println(WiFi.dnsIP());
  } else {
    Serial.println("\n  ❌ WiFi Failed - Starting AP Mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("SmartBin_AP", "12345678");
    Serial.print("  AP IP: ");
    Serial.println(WiFi.softAPIP());
  }
}

// ==================== UART SETUP ====================
void setupUART() {
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, -1);
  
  Serial.println("\nUART Communication Setup:");
  Serial.println("  ✓ RX on GPIO 35 (from ESP32-CAM)");
  Serial.println("  Baud Rate: 115200");
}

// ==================== UART COMMUNICATION ====================
void triggerCameraDetection() {
  digitalWrite(CAM_TRIGGER_PIN, HIGH);
  delay(10);
  digitalWrite(CAM_TRIGGER_PIN, LOW);
  
  Serial.println("  Camera trigger pulse sent (GPIO 21 → CAM GPIO 16)");
}

void processUARTData() {
  if (Serial2.available()) {
    String jsonString = Serial2.readStringUntil('\n');
    
    if (jsonString.length() > 0) {
      Serial.println("\n>>> UART DATA RECEIVED <<<");
      Serial.println("  Raw: " + jsonString);
      
      DynamicJsonDocument doc(512);
      DeserializationError error = deserializeJson(doc, jsonString);
      
      if (!error) {
        detectedMaterial = doc["material"].as<String>();
        detectedConfidence = doc["confidence"].as<float>();
        
        Serial.println("  Parsed Result:");
        Serial.printf("    Material: %s\n", detectedMaterial.c_str());
        Serial.printf("    Confidence: %.2f%%\n", detectedConfidence * 100);
        
        materialDetectionComplete = true;
        
        // Broadcast to WebSocket clients
        DynamicJsonDocument wsDoc(256);
        wsDoc["event"] = "material_detected";
        wsDoc["material"] = detectedMaterial;
        wsDoc["confidence"] = detectedConfidence;
        String message;
        serializeJson(wsDoc, message);
        webSocket.broadcastTXT(message);
      } else {
        Serial.println("  ❌ Failed to parse UART JSON");
      }
    }
  }
}

// ==================== AUTHENTICATION ====================
bool authenticateRequest(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Authorization")) {
    return false;
  }
  
  String auth = request->header("Authorization");
  if (auth.startsWith("Bearer ")) {
    String pwd = auth.substring(7);
    return pwd == local_access_password;
  }
  
  return false;
}

// ==================== WEB SERVER SETUP ====================
void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<html><body style='font-family: monospace;'>";
    html += "<h1>Smart Waste Bin Controller</h1>";
    html += "<p><b>Local IP:</b> " + WiFi.localIP().toString() + "</p>";
    html += "<p><b>WebSocket:</b> ws://" + WiFi.localIP().toString() + ":81</p>";
    html += "<hr><h3>Communication:</h3>";
    html += "<p>Trigger: GPIO 21 → ESP32-CAM GPIO 16</p>";
    html += "<p>UART RX: GPIO 35 ← ESP32-CAM GPIO 14</p>";
    html += "<p><b>Ultrasonic (NewPing):</b> Presence(T17/E14), Organic(T22/E23), Inorganic(T27/E16)</p>";
    html += "<hr><h3>API Endpoints:</h3>";
    html += "<ul>";
    html += "<li>GET /api/status - Get bin status</li>";
    html += "<li>POST /api/open - Open bin (auth required)</li>";
    html += "<li>POST /api/close - Close bin (auth required)</li>";
    html += "</ul>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });
  
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    doc["organic_level_cm"] = organicBinLevelCm;
    doc["non_organic_level_cm"] = nonOrganicBinLevelCm;
    doc["organic_level_pct"] = organicBinLevelPct;
    doc["non_organic_level_pct"] = nonOrganicBinLevelPct;
    doc["organic_full"] = isOrganicBinFull;
    doc["non_organic_full"] = isNonOrganicBinFull;
    doc["state"] = currentState;
    doc["detected_material"] = detectedMaterial;
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  server.on("/api/open", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!authenticateRequest(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    
    if (request->hasParam("bin", true)) {
      String binParam = request->getParam("bin", true)->value();
      if (binParam == "organic" && !isOrganicBinFull) {
        openBin(0);
        request->send(200, "application/json", "{\"status\":\"opened\",\"bin\":\"organic\"}");
      } else if (binParam == "non_organic" && !isNonOrganicBinFull) {
        openBin(1);
        request->send(200, "application/json", "{\"status\":\"opened\",\"bin\":\"non_organic\"}");
      } else {
        request->send(400, "application/json", "{\"error\":\"Bin full or invalid\"}");
      }
    } else {
      request->send(400, "application/json", "{\"error\":\"Missing bin parameter\"}");
    }
  });
  
  server.on("/api/close", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!authenticateRequest(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    
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
      request->send(400, "application/json", "{\"error\":\"error\"}");
    }
  });
  
  server.begin();
  Serial.println("  ✓ Web server started on port 80");
}

// ==================== WEBSOCKET SETUP ====================
void setupWebSocket() {
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("  ✓ WebSocket server started on port 81");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("WS: Client [%u] disconnected\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("WS: Client [%u] connected from %s\n", num, ip.toString().c_str());
        sendWebSocketStatus(num);
      }
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
  doc["organic_level_pct"] = organicBinLevelPct;
  doc["non_organic_level_pct"] = nonOrganicBinLevelPct;
  doc["organic_full"] = isOrganicBinFull;
  doc["non_organic_full"] = isNonOrganicBinFull;
  doc["state"] = currentState;
  doc["detected_material"] = detectedMaterial;
  String response;
  serializeJson(doc, response);
  webSocket.sendTXT(clientNum, response);
}

void handleWebSocketMessage(uint8_t clientNum, String message) {
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, message);
  
  String command = doc["command"];
  String pwd = doc["password"];
  
  if (pwd != local_access_password) {
    DynamicJsonDocument error(256);
    error["error"] = "Unauthorized";
    String response;
    serializeJson(error, response);
    webSocket.sendTXT(clientNum, response);
    return;
  }
  
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
  // Debounced presence detection
  static int consecutiveDetections = 0;
  
  float distance = getDistance(sonarPresence);
  
  if (distance > 0 && distance < PRESENCE_THRESHOLD_CM) {
    consecutiveDetections++;
    if (consecutiveDetections >= PRESENCE_DEBOUNCE_COUNT) {
      currentState = DETECTING_MOTION;
      consecutiveDetections = 0;
    }
  } else {
    consecutiveDetections = 0;
  }
}

// ==================== BIN CONTROL ====================
void openBin(uint8_t binType) {
  if (binType == 0) { 
    servoOrganic.write(90); 
    Serial.println("  ✓ Organic bin OPENED");
  } else { 
    servoNonOrganic.write(90); 
    Serial.println("  ✓ Non-organic bin OPENED");
  }
}

void closeBin(uint8_t binType) {
  if (binType == 0) { 
    servoOrganic.write(0); 
    Serial.println("  ✓ Organic bin CLOSED");
  } else { 
    servoNonOrganic.write(0); 
    Serial.println("  ✓ Non-organic bin CLOSED");
  }
}

// ==================== BIN LEVEL MONITORING ====================
void updateBinLevels() {
  float distOrg = getDistance(sonarOrganic);
  if (distOrg > 0) {
    organicBinLevelCm = distOrg;
    organicBinLevelPct = map(constrain(distOrg, BIN_FULL_THRESHOLD_CM, MAX_BIN_HEIGHT_CM), 
                             MAX_BIN_HEIGHT_CM, BIN_FULL_THRESHOLD_CM, 0, 100);
    isOrganicBinFull = (organicBinLevelPct >= 90);
  }
  
  float distInorg = getDistance(sonarInorganic);
  if (distInorg > 0) {
    nonOrganicBinLevelCm = distInorg;
    nonOrganicBinLevelPct = map(constrain(distInorg, BIN_FULL_THRESHOLD_CM, MAX_BIN_HEIGHT_CM), 
                                MAX_BIN_HEIGHT_CM, BIN_FULL_THRESHOLD_CM, 0, 100);
    isNonOrganicBinFull = (nonOrganicBinLevelPct >= 90);
  }
}

// ==================== NEWPING DISTANCE FUNCTION ====================
float getDistance(NewPing &sonar) {
  delay(50);  // Wait between pings for sensor settling
  
  unsigned int distance = sonar.ping_cm();  // NewPing handles WiFi interference
  
  if (distance == 0) {
    // 0 means out of range or no echo
    return -1;
  }
  
  // Validate reading is reasonable
  if (distance > MAX_DISTANCE) {
    return -1;
  }
  
  return (float)distance;
}

// ==================== LED CONTROL ====================
void updateLEDs() {
  if (isOrganicBinFull) {
    digitalWrite(LED_ORG_RED_PIN, HIGH);
    digitalWrite(LED_ORG_GREEN_PIN, LOW);
  } else {
    digitalWrite(LED_ORG_RED_PIN, LOW);
    digitalWrite(LED_ORG_GREEN_PIN, HIGH);
  }
  
  if (isNonOrganicBinFull) {
    digitalWrite(LED_INORG_RED_PIN, HIGH);
    digitalWrite(LED_INORG_GREEN_PIN, LOW);
  } else {
    digitalWrite(LED_INORG_RED_PIN, LOW);
    digitalWrite(LED_INORG_GREEN_PIN, HIGH);
  }
}

// ==================== BACKEND COMMUNICATION ====================
void sendBinDataToBackend() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  DynamicJsonDocument doc(1024);
  doc["bin_organic_id"] = BIN_ORGANIC_ID;
  doc["bin_non_organic_id"] = BIN_NON_ORGANIC_ID;
  doc["organic_level"] = organicBinLevelPct;
  doc["non_organic_level"] = nonOrganicBinLevelPct;
  doc["organic_weight"] = (organicBinLevelPct / 100.0) * 10.0; 
  doc["non_organic_weight"] = (nonOrganicBinLevelPct / 100.0) * 10.0; 
  doc["organic_full"] = isOrganicBinFull;
  doc["non_organic_full"] = isNonOrganicBinFull;
  doc["timestamp"] = millis();
  
  String json;
  serializeJson(doc, json);
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  http.begin(client, String(backend_url) + "/api/bins/update");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  
  int httpResponseCode = http.POST(json);
  if (httpResponseCode > 0) {
    Serial.printf("Backend sync: %d\n", httpResponseCode);
  } else {
    Serial.printf("Backend sync failed: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

void pollBackendCommands() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  String url = String(backend_url) + "/api/bins/" + BIN_ORGANIC_ID + "/commands";
  http.begin(client, url);
  http.setTimeout(3000);
  
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, payload);
    
    JsonArray commands = doc["commands"].as<JsonArray>();
    for (JsonObject cmd : commands) {
      String commandName = cmd["command"].as<String>();
      if (commandName == "OPEN" && !isOrganicBinFull) openBin(0);
      else if (commandName == "CLOSE") closeBin(0);
    }
  }
  http.end();
  
  url = String(backend_url) + "/api/bins/" + BIN_NON_ORGANIC_ID + "/commands";
  http.begin(client, url);
  httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, payload);
    
    JsonArray commands = doc["commands"].as<JsonArray>();
    for (JsonObject cmd : commands) {
      String commandName = cmd["command"].as<String>();
      if (commandName == "OPEN" && !isNonOrganicBinFull) openBin(1);
      else if (commandName == "CLOSE") closeBin(1);
    }
  }
  http.end();
}