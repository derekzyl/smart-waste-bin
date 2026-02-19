/**
 * ESP32 Smart Waste Bin - ESP-NOW CONNECTION FIX
 * ESP32-CAM MAC: A0:DD:6C:AF:09:30
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Adafruit_PWMServoDriver.h> // REPLACES ESP32Servo
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ==================== PIN DEFINITIONS ====================
#define TRIG_PIN_PRESENCE 32
#define ECHO_PIN_PRESENCE 34
#define TRIG_PIN_ORG 33
#define ECHO_PIN_ORG 35
#define TRIG_PIN_INORG 13
#define ECHO_PIN_INORG 36



// PCA9685 SERVO CONFIG
#define SERVOMIN  150 // This is the 'minimum' pulse length count (out of 4096)
#define SERVOMAX  600 // This is the 'maximum' pulse length count (out of 4096)
#define USMIN  600 // This is the rounded 'minimum' microsecond length based on the minimum pulse of 150
#define USMAX  2400 // This is the rounded 'maximum' microsecond length based on the maximum pulse of 600
#define SERVO_FREQ 50 // Analog servos run at ~50 Hz updates

// Channels on PCA9685 (0 to 15)
#define PCA_CHANNEL_ORG 0
#define PCA_CHANNEL_INORG 1

// I2C Pins for PCA9685
#define I2C_SDA 21
#define I2C_SCL 22

#define CAM_TRIGGER_PIN 4

#define LED_ORG_RED_PIN 25
#define LED_ORG_GREEN_PIN 26
#define LED_INORG_RED_PIN 27
#define LED_INORG_GREEN_PIN 14

#define UART_RX_PIN 15
#define UART_BAUD 115200

// ==================== CONFIGURATION ====================
const char* ssid = "cybergenii";
const char* password = "12341234";
const char* backend_url = "https://xenophobic-netta-cybergenii-1584fde7.koyeb.app";
const char* local_access_password = "SmartBin2025";

const char* BIN_ORGANIC_ID = "0x001";
const char* BIN_NON_ORGANIC_ID = "0x002";
const float MAX_BIN_HEIGHT_CM = 91.44;  // 3 feet
const float BIN_FULL_THRESHOLD_CM = 10.0;
const float SENSOR_OFFSET_CM = 5.0;

// ==================== ESP-NOW CONFIGURATION ====================
// ESP32-CAM MAC Address: A0:DD:6C:AF:09:30
uint8_t camMacAddress[] = { 
  0xA0, 0xDD, 0x6C, 0xAF, 0x09, 0x30 
};

bool espNowInitialized = false;
bool camPeerAdded = false;
int espNowSendCount = 0;
int espNowReceiveCount = 0;
int espNowFailCount = 0;

// ESP-NOW Message Structures
typedef struct {
  char command[32];
  unsigned long timestamp;
} CommandMessage;

typedef struct {
  char material[32];
  float confidence;
  unsigned long timestamp;
} DetectionResult;

// ==================== GLOBAL VARIABLES ====================
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(); // Default address 0x40

// ==================== NEWPING SENSOR SETUP ====================
#include <NewPing.h>

#define MAX_DISTANCE 200 // Maximum distance we want to ping for (in centimeters).

// Sensor Objects
NewPing sonarPresence(TRIG_PIN_PRESENCE, ECHO_PIN_PRESENCE, MAX_DISTANCE);
NewPing sonarOrganic(TRIG_PIN_ORG, ECHO_PIN_ORG, MAX_DISTANCE);
NewPing sonarInorganic(TRIG_PIN_INORG, ECHO_PIN_INORG, MAX_DISTANCE);

// Helper to get median distance (filters noise)
float getFilteredDistance(NewPing &sonar) {
  unsigned int uS = sonar.ping_median(5); // Take 5 readings and return median
  float dist = sonar.convert_cm(uS);
  if (dist == 0) return -1.0; // Timeout or out of range
  return dist;
}

AsyncWebServer server(80);
WebSocketsServer webSocket(81);

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

float organicBinLevelCm = 0.0;
float nonOrganicBinLevelCm = 0.0;
int organicBinLevelPct = 0;
int nonOrganicBinLevelPct = 0;
bool isOrganicBinFull = false;
bool isNonOrganicBinFull = false;

unsigned long lastMotionTime = 0;
unsigned long binOpenTime = 0;
const unsigned long MOTION_TIMEOUT = 5000;
const unsigned long BIN_OPEN_TIMEOUT = 10000;
unsigned long lastLevelCheckTime = 0;
const unsigned long LEVEL_CHECK_INTERVAL = 2000;

unsigned long lastBackendSyncTime = 0;
const unsigned long BACKEND_SYNC_INTERVAL = 5000;
unsigned long lastCommandPollTime = 0;
const unsigned long COMMAND_POLL_INTERVAL = 800;   // Normal: poll every 800ms
const unsigned long COMMAND_POLL_FAST_MS = 100;    // When analyzing: poll every 100ms for low latency

const float PRESENCE_THRESHOLD_CM = 50.0;
const int PRESENCE_DEBOUNCE_COUNT = 3;

String detectedMaterial = "";
float detectedConfidence = 0.0;
bool materialDetectionComplete = false;
unsigned long materialDetectionStartTime = 0;
const unsigned long MATERIAL_DETECTION_TIMEOUT = 15000;  // Increased to 15s

bool presenceDetectedForChase = false;
unsigned long lastChaseUpdate = 0;
const unsigned long CHASE_SPEED = 100;
int chasePosition = 0;

// When true, OPENING_BIN will open the bin even if level sensors say "full" (backend command)
bool openFromBackendCommand = false;
const int LED_PINS[] = {LED_ORG_GREEN_PIN, LED_ORG_RED_PIN, LED_INORG_GREEN_PIN, LED_INORG_RED_PIN};
const int NUM_LEDS = 4;

// ==================== FUNCTION DECLARATIONS ====================
void setupWiFi();
void setupESPNow();
void setupWebServer();
void setupWebSocket();
void setupUART();
void openBin(uint8_t binType);
void closeBin(uint8_t binType);
void updateBinLevels();
void updateLEDs();
void sendBinDataToBackend();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

void triggerCameraDetection();
void processUARTData();
bool authenticateRequest(AsyncWebServerRequest *request);
void sendWebSocketStatus(uint8_t clientNum);
void handleWebSocketMessage(uint8_t clientNum, String message);
void runLEDChaseMode();
void testAllLEDs();
void testAllSensors();
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len);
void onDataSent(const uint8_t *mac, esp_now_send_status_t status);
void sendESPNowCommand(const char* cmd);
void sendESPNowCommand(const char* cmd);
void testESPNowConnection();
void pollBackendCommands();
int angleToPulse(int angle);
void setServoAngle(uint8_t channel, int angle);
void broadcastDetectionEvent(String material, float confidence, String action);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n=================================");
  Serial.println("ESP32 Main Controller");
  Serial.println("ESP-NOW + Waterproof Sensors");
  Serial.println("=================================");

  // Initialize pins
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

  pinMode(CAM_TRIGGER_PIN, OUTPUT);
  digitalWrite(CAM_TRIGGER_PIN, LOW);

  pinMode(CAM_TRIGGER_PIN, OUTPUT);
  digitalWrite(CAM_TRIGGER_PIN, LOW);

  // Initialize I2C for PCA9685
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Initialize PCA9685
  Serial.println("Initializing PCA9685 Servo Controller on 0x40...");
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);  // Analog servos run at ~50 Hz updates
  delay(10);

  // Reset Servos (Close)
  setServoAngle(PCA_CHANNEL_ORG, 0);
  setServoAngle(PCA_CHANNEL_INORG, 0);

  // LED TEST
  Serial.println("\n>>> LED TEST <<<");
  testAllLEDs();

  // Initialize WiFi FIRST (required for ESP-NOW)
  setupWiFi();

  // CRITICAL: WiFi must be initialized before ESP-NOW
  if (WiFi.status() == WL_CONNECTED || WiFi.getMode() == WIFI_STA) {
    setupESPNow();
  } else {
    Serial.println("⚠️  WiFi not initialized - ESP-NOW may fail");
    // Force WiFi STA mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    setupESPNow();
  }

  // SENSOR TEST
  Serial.println("\n>>> SENSOR TEST <<<");
  testAllSensors();

  // Test ESP-NOW connection
  Serial.println("\n>>> ESP-NOW CONNECTION TEST <<<");
  testESPNowConnection();

  setupUART();
  setupWebServer();
  setupWebSocket();

  Serial.println("\n=================================");
  Serial.println("SYSTEM READY");
  Serial.println("=================================");
  
  Serial.print("ESP32 Main MAC: ");
  Serial.println(WiFi.macAddress());
  
  Serial.print("ESP32-CAM MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", camMacAddress[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  
  Serial.println("\n📡 ESP-NOW Status:");
  Serial.println("  Initialized: " + String(espNowInitialized ? "✅" : "❌"));
  Serial.println("  Peer Added: " + String(camPeerAdded ? "✅" : "❌"));
  Serial.println("  WiFi Channel: " + String(WiFi.channel()));
  
  Serial.println("\n🔍 Waiting for presence detection...");
  Serial.println("=================================\n");

  updateLEDs();
}

// ==================== ESP-NOW SETUP ====================
void setupESPNow() {
  Serial.println("\nInitializing ESP-NOW...");
  
  // Ensure WiFi is in STA mode
  if (WiFi.getMode() != WIFI_STA) {
    Serial.println("  Setting WiFi to STA mode...");
    WiFi.mode(WIFI_STA);
    delay(100);
  }

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("  ❌ ESP-NOW init FAILED");
    espNowInitialized = false;
    return;
  }

  espNowInitialized = true;
  Serial.println("  ✓ ESP-NOW initialized");

  // Register callbacks
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);
  Serial.println("  ✓ Callbacks registered");

  // Add ESP32-CAM as peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, camMacAddress, 6);
  peerInfo.channel = 0;  // Use current WiFi channel (auto)
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("  ❌ Failed to add ESP32-CAM peer");
    camPeerAdded = false;
  } else {
    Serial.println("  ✓ ESP32-CAM peer added successfully");
    Serial.print("  Peer MAC: ");
    for (int i = 0; i < 6; i++) {
      Serial.printf("%02X", camMacAddress[i]);
      if (i < 5) Serial.print(":");
    }
    Serial.println();
    camPeerAdded = true;
  }
}

// ==================== ESP-NOW CALLBACKS ====================
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.print("\n📡 ESP-NOW Send Status: ");
  
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("✅ SUCCESS");
    espNowSendCount++;
  } else {
    Serial.println("❌ FAILED");
    espNowFailCount++;
    
    // Detailed error info
    Serial.println("  Possible causes:");
    Serial.println("  - ESP32-CAM not powered on");
    Serial.println("  - ESP32-CAM WiFi not initialized");
    Serial.println("  - Different WiFi channels");
    Serial.println("  - Out of range");
  }
  
  Serial.printf("  Stats: Sent=%d, Failed=%d\n", espNowSendCount, espNowFailCount);
}

void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  espNowReceiveCount++;
  
  Serial.println("\n📡 ESP-NOW Data Received!");
  Serial.print("  From MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  Serial.printf("  Length: %d bytes\n", len);

  if (len == sizeof(DetectionResult)) {
    DetectionResult result;
    memcpy(&result, incomingData, sizeof(result));

    detectedMaterial = String(result.material);
    detectedConfidence = result.confidence;

    Serial.println("  ✅ Parsed Detection Result:");
    Serial.printf("    Material: %s\n", detectedMaterial.c_str());
    Serial.printf("    Confidence: %.2f%%\n", detectedConfidence * 100);

    materialDetectionComplete = true;
  } else {
    Serial.printf("  ⚠️  Unexpected data length: %d (expected %d)\n", len, sizeof(DetectionResult));
  }
}

void sendESPNowCommand(const char* cmd) {
  if (!espNowInitialized) {
    Serial.println("⚠️  ESP-NOW not initialized - cannot send");
    return;
  }
  
  if (!camPeerAdded) {
    Serial.println("⚠️  ESP32-CAM peer not added - cannot send");
    return;
  }

  CommandMessage message;
  strncpy(message.command, cmd, sizeof(message.command) - 1);
  message.command[sizeof(message.command) - 1] = '\0';
  message.timestamp = millis();

  Serial.printf("\n📡 Sending ESP-NOW command: %s\n", cmd);
  
  esp_err_t result = esp_now_send(camMacAddress, (uint8_t *)&message, sizeof(message));

  if (result == ESP_OK) {
    Serial.println("  ✓ Command queued for sending");
  } else {
    Serial.printf("  ❌ Send failed with error: %d\n", result);
    
    // Try GPIO fallback
    Serial.println("  → Using GPIO fallback");
    digitalWrite(CAM_TRIGGER_PIN, HIGH);
    delay(100);
    digitalWrite(CAM_TRIGGER_PIN, LOW);
  }
}

void testESPNowConnection() {
  if (!espNowInitialized || !camPeerAdded) {
    Serial.println("  ⚠️  ESP-NOW not ready - skipping test");
    return;
  }
  
  Serial.println("  Sending test message to ESP32-CAM...");
  sendESPNowCommand("TEST");
  
  Serial.println("  Waiting 2 seconds for response...");
  delay(2000);
  
  if (espNowReceiveCount > 0) {
    Serial.println("  ✅ ESP-NOW connection working!");
  } else {
    Serial.println("  ⚠️  No response from ESP32-CAM");
    Serial.println("  Check:");
    Serial.println("    - ESP32-CAM is powered on");
    Serial.println("    - ESP32-CAM code is running");
    Serial.println("    - Both devices on same WiFi channel");
  }
}



// ==================== WEBSOCKET BROADCAST ====================
void broadcastDetectionEvent(String material, float confidence, String action) {
  // Create JSON message for App
  DynamicJsonDocument doc(512);
  doc["type"] = "DETECTION_EVENT";
  doc["material"] = material;
  doc["confidence"] = confidence;
  doc["action"] = action; // "OPENING" or "CLOSING"

  String jsonString;
  serializeJson(doc, jsonString);
  
  // Broadcast to all connected WebSocket clients (The App)
  webSocket.broadcastTXT(jsonString);
  Serial.println("📡 WebSocket Broadcast: " + jsonString);
}


// ==================== MAIN LOOP ====================
void loop() {
  webSocket.loop();
  processUARTData();

  // Presence detection (100ms for faster reaction)
  static unsigned long lastPresenceCheck = 0;
  if (millis() - lastPresenceCheck >= 100) {
    float distance = getFilteredDistance(sonarPresence); // Uses median filter

    if (distance > 0 && distance < PRESENCE_THRESHOLD_CM) {
      if (!presenceDetectedForChase) {
        presenceDetectedForChase = true;
        Serial.println("\n🎯 PRESENCE DETECTED!");
        Serial.printf("   Distance: %.2f cm\n", distance);
        
        // Trigger camera
        if (currentState == IDLE) {
          currentState = DETECTING_MOTION;
        }
      }
    } else {
      if (presenceDetectedForChase) {
        presenceDetectedForChase = false;
        Serial.println("\n✋ Presence lost\n");
        for (int i = 0; i < NUM_LEDS; i++) {
          digitalWrite(LED_PINS[i], LOW);
        }
      }
    }

    lastPresenceCheck = millis();
  }

  // LED chase
  if (presenceDetectedForChase) {
    runLEDChaseMode();
  } else {
    updateLEDs();
  }

  // Update bin levels
  if (millis() - lastLevelCheckTime > LEVEL_CHECK_INTERVAL) {
    updateBinLevels();
    
    static int readingCount = 0;
    if (readingCount++ % 10 == 0) {
      Serial.println("\n📊 Sensor Readings:");
      Serial.printf("   Presence:  %.2f cm\n", getFilteredDistance(sonarPresence));
      Serial.printf("   Organic:   %.2f cm (%d%%)\n", organicBinLevelCm, organicBinLevelPct);
      Serial.printf("   Inorganic: %.2f cm (%d%%)\n", nonOrganicBinLevelCm, nonOrganicBinLevelPct);
      Serial.printf("   ESP-NOW: Sent=%d, Received=%d, Failed=%d\n", 
                    espNowSendCount, espNowReceiveCount, espNowFailCount);
    }

    lastLevelCheckTime = millis();
  }

  // Poll for commands from Backend - aggressive when waiting for cloud result
  unsigned long pollInterval = (currentState == ANALYZING_MATERIAL)
    ? COMMAND_POLL_FAST_MS
    : COMMAND_POLL_INTERVAL;
  if (millis() - lastCommandPollTime > pollInterval) {
    pollBackendCommands();
    lastCommandPollTime = millis();
  }

  // State Machine
  switch (currentState) {
    case IDLE:
      // Allow unsolicited results (e.g. from Web UI or manual trigger)
      if (materialDetectionComplete) {
        Serial.println("\n>>> Unsolicited detection result received!");
        
        if (detectedMaterial == "ORGANIC") {
          selectedBin = BIN_ORGANIC_ID;
          Serial.println(">>> Opening organic bin");
          currentState = OPENING_BIN;
        } else if (detectedMaterial == "NON_ORGANIC" || detectedMaterial == "INORGANIC") {
          selectedBin = BIN_NON_ORGANIC_ID;
          Serial.println(">>> Opening inorganic bin");
          currentState = OPENING_BIN;
        } else {
          Serial.println(">>> UNKNOWN material - ignoring");
        }
        
        materialDetectionComplete = false;
      }
      break;

    case DETECTING_MOTION:
      currentState = ANALYZING_MATERIAL;
      materialDetectionStartTime = millis();
      triggerCameraDetection();
      broadcastDetectionEvent("", 0.0f, "ANALYZING");
      break;

    case ANALYZING_MATERIAL:
      if (materialDetectionComplete) {
        if (detectedMaterial == "ORGANIC") {
          selectedBin = BIN_ORGANIC_ID;
          Serial.println(">>> Opening organic bin");
        } else if (detectedMaterial == "NON_ORGANIC" || detectedMaterial == "INORGANIC") {
          selectedBin = BIN_NON_ORGANIC_ID;
          Serial.println(">>> Opening inorganic bin");
        } else {
          Serial.println(">>> UNKNOWN material");
          currentState = IDLE;
          break;
        }

        broadcastDetectionEvent(detectedMaterial, detectedConfidence, "OPENING");
        currentState = OPENING_BIN;
        materialDetectionComplete = false;
      }

      if (millis() - materialDetectionStartTime > MATERIAL_DETECTION_TIMEOUT) {
        Serial.println(">>> Detection TIMEOUT");
        currentState = IDLE;
        materialDetectionComplete = false;
      }
      break;

    case OPENING_BIN: {
      // Backend/cloud command: always open (bypass full check so detection always opens bin)
      bool allowOpen = openFromBackendCommand ||
        (selectedBin == BIN_ORGANIC_ID && !isOrganicBinFull) ||
        (selectedBin == BIN_NON_ORGANIC_ID && !isNonOrganicBinFull);
      openFromBackendCommand = false;

      if (selectedBin == BIN_ORGANIC_ID) {
        if (allowOpen) {
          Serial.println(">>> Executing OPEN: organic bin (servo)");
          openBin(0);
          currentState = BIN_OPEN;
          binOpenTime = millis();
          lastMotionTime = millis();
        } else {
          Serial.println("❌ ORGANIC BIN FULL - Staying Closed");
          currentState = IDLE;
        }
      } else if (selectedBin == BIN_NON_ORGANIC_ID) {
        if (allowOpen) {
          Serial.println(">>> Executing OPEN: inorganic bin (servo)");
          openBin(1);
          currentState = BIN_OPEN;
          binOpenTime = millis();
          lastMotionTime = millis();
        } else {
          Serial.println("❌ INORGANIC BIN FULL - Staying Closed");
          currentState = IDLE;
        }
      } else {
        Serial.println("❌ Unknown bin selected");
        currentState = IDLE;
      }
      break;
    }

    case BIN_OPEN:
      {
        // Extend open time if motion detected (someone is still throwing trash)
        float distance = getFilteredDistance(sonarPresence);
        if (distance > 0 && distance < PRESENCE_THRESHOLD_CM) {
          lastMotionTime = millis();
          Serial.println("  User present - extending open time");
        }

        // Auto Close Logic
        // Close if:
        // 1. No motion for MOTION_TIMEOUT (5s)
        // 2. OR Absolute max time of BIN_OPEN_TIMEOUT (10s) reached
        unsigned long timeSinceMotion = millis() - lastMotionTime;
        unsigned long timeSinceOpen = millis() - binOpenTime;
        
        if (timeSinceMotion > MOTION_TIMEOUT || timeSinceOpen > BIN_OPEN_TIMEOUT) {
          Serial.println(">>> Auto-closing bin...");
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
      broadcastDetectionEvent(
        selectedBin == BIN_ORGANIC_ID ? "ORGANIC" : "NON_ORGANIC",
        0.0f,
        "CLOSING"
      );
      Serial.println(">>> Bin Closed. Returning to IDLE\n");
      currentState = IDLE;
      break;
  }

  delay(10);
}

// ==================== CAMERA TRIGGER ====================
void triggerCameraDetection() {
  Serial.println("\n>>> TRIGGERING CAMERA <<<");
  
  // ESP-NOW (primary)
  sendESPNowCommand("DETECT");
  
  // GPIO (backup)
  digitalWrite(CAM_TRIGGER_PIN, HIGH);
  delay(100);
  digitalWrite(CAM_TRIGGER_PIN, LOW);
  
  Serial.println("  ✓ Trigger sent via ESP-NOW + GPIO");
}

// ==================== BIN LEVEL MONITORING ====================


// ==================== SENSOR TEST ====================
void testAllSensors() {
  Serial.println("  Testing sensors (NewPing Median Filter):\n");
  
  Serial.println("  PRESENCE:");
  float dist = getFilteredDistance(sonarPresence);
  Serial.printf("    Distance: %.2f cm\n", dist);
  
  Serial.println("\n  ORGANIC BIN (AJ-SR04M / SR04M-2):");
  dist = getFilteredDistance(sonarOrganic);
  if (dist == -1) Serial.println("    Result: Timeout / Blind Spot");
  else Serial.printf("    Result: %.2f cm\n", dist);
  
  Serial.println("\n  INORGANIC BIN (AJ-SR04M / SR04M-2):");
  dist = getFilteredDistance(sonarInorganic);
  if (dist == -1) Serial.println("    Result: Timeout / Blind Spot");
  else Serial.printf("    Result: %.2f cm\n", dist);
  
  Serial.println("\n  ✓ Sensor test complete\n");
}

// ==================== BIN LEVEL MONITORING ====================
void updateBinLevels() {
  // Organic
  float distOrg = getFilteredDistance(sonarOrganic);
  if (distOrg > 0) {
    organicBinLevelCm = distOrg;
    float maxDist = MAX_BIN_HEIGHT_CM + SENSOR_OFFSET_CM;
    float minDist = SENSOR_OFFSET_CM;
    organicBinLevelPct = map(constrain(distOrg, minDist, maxDist),
                             maxDist, minDist, 0, 100);
    isOrganicBinFull = (organicBinLevelPct >= 90);
  } else {
    // Keep internal state or mark as error? 
    // For now, don't update if reading is invalid to avoid flickering
  }

  // Organic
  float distInorg = getFilteredDistance(sonarInorganic);
  if (distInorg > 0) {
    nonOrganicBinLevelCm = distInorg;
    float maxDist = MAX_BIN_HEIGHT_CM + SENSOR_OFFSET_CM;
    float minDist = SENSOR_OFFSET_CM;
    nonOrganicBinLevelPct = map(constrain(distInorg, minDist, maxDist),
                                maxDist, minDist, 0, 100);
    isNonOrganicBinFull = (nonOrganicBinLevelPct >= 90);
  }
}

// ==================== BIN CONTROL (PCA9685) ====================
void setServoAngle(uint8_t channel, int angle) {
  // Map angle (0-180) to pulse length (SERVOMIN-SERVOMAX)
  int pulse = map(angle, 0, 180, SERVOMIN, SERVOMAX);
  pwm.setPWM(channel, 0, pulse);
}

void openBin(uint8_t binType) {
  if (binType == 0) {
    setServoAngle(PCA_CHANNEL_ORG, 90); // 90 degrees
    Serial.println("  ✓ Organic bin OPENED (PCA Ch 0)");
  } else {
    setServoAngle(PCA_CHANNEL_INORG, 90); // 90 degrees
    Serial.println("  ✓ Inorganic bin OPENED (PCA Ch 1)");
  }
}

void closeBin(uint8_t binType) {
  if (binType == 0) {
    setServoAngle(PCA_CHANNEL_ORG, 0); // 0 degrees
    Serial.println("  ✓ Organic bin CLOSED (PCA Ch 0)");
  } else {
    setServoAngle(PCA_CHANNEL_INORG, 0); // 0 degrees
    Serial.println("  ✓ Inorganic bin CLOSED (PCA Ch 1)");
  }
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

void runLEDChaseMode() {
  if (millis() - lastChaseUpdate >= CHASE_SPEED) {
    for (int i = 0; i < NUM_LEDS; i++) {
      digitalWrite(LED_PINS[i], LOW);
    }
    digitalWrite(LED_PINS[chasePosition], HIGH);
    chasePosition = (chasePosition + 1) % NUM_LEDS;
    lastChaseUpdate = millis();
  }
}

void testAllLEDs() {
  for (int i = 0; i < NUM_LEDS; i++) {
    digitalWrite(LED_PINS[i], HIGH);
    delay(200);
    digitalWrite(LED_PINS[i], LOW);
  }
}

void testServos() {
  Serial.println("  Testing Organic Servo (PCA Ch 0)...");
  setServoAngle(PCA_CHANNEL_ORG, 90);  // Open
  delay(1000);
  setServoAngle(PCA_CHANNEL_ORG, 0);   // Close
  delay(500);
  
  Serial.println("  Testing Inorganic Servo (PCA Ch 1)...");
  setServoAngle(PCA_CHANNEL_INORG, 90); // Open
  delay(1000);
  setServoAngle(PCA_CHANNEL_INORG, 0);  // Close
  delay(500);
  
  Serial.println("  ✓ Servo diagnostic complete");
}

// ==================== UART ====================
void setupUART() {
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, -1);
  Serial.println("\n  ✓ UART initialized (GPIO 15)");
}

void processUARTData() {
  if (Serial2.available()) {
    String jsonString = Serial2.readStringUntil('\n');
    if (jsonString.length() > 0) {
      DynamicJsonDocument doc(512);
      if (deserializeJson(doc, jsonString) == DeserializationError::Ok) {
        detectedMaterial = doc["material"].as<String>();
        detectedConfidence = doc["confidence"].as<float>();
        materialDetectionComplete = true;
        Serial.println("\n📡 UART: Material = " + detectedMaterial);
      }
    }
  }
}

// ==================== WIFI ====================
void setupWiFi() {
  Serial.println("\nConnecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n  ✓ WiFi Connected");
    Serial.print("  IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Channel: ");
    Serial.println(WiFi.channel());
  } else {
    Serial.println("\n  ⚠️  WiFi Failed - using AP mode");
  }
}

// ==================== COMMAND POLLING (SINGLE REQUEST = LOW LATENCY) ====================
void pollBackendCommands() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  // Single endpoint for both bins = one HTTPS round-trip instead of two
  String url = String(backend_url) + "/api/bins/commands";
  http.begin(client, url);
  http.setTimeout(5000);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    if (httpCode > 0) Serial.printf("  ⚠️ GET /api/bins/commands HTTP %d\n", httpCode);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  if (payload.length() < 5) return;  // "[]" or "{}" only = no commands

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.printf("  ⚠️ JSON parse error: %s (len=%d)\n", error.c_str(), payload.length());
    return;
  }

  JsonArray commands = doc["commands"];
  if (!commands || commands.size() == 0) return;

  JsonObject cmd = commands[0];
  if (cmd.isNull()) return;

  int cmdId = cmd["id"].as<int>();
  String binId = cmd["bin_id"].as<String>();
  binId.trim();
  String commandType = cmd["command"].as<String>();
  commandType.trim();

  // Normalize bin_id
  if (binId != BIN_ORGANIC_ID && binId != BIN_NON_ORGANIC_ID) {
    if (binId == "0x001" || binId == "001") {
      binId = BIN_ORGANIC_ID;
    } else if (binId == "0x002" || binId == "002") {
      binId = BIN_NON_ORGANIC_ID;
    } else {
      return;
    }
  }

  if (commandType == "OPEN") {
    selectedBin = binId;
    currentState = OPENING_BIN;
    openFromBackendCommand = true;
    JsonObject params = cmd["params"];
    String mat = params["material"].as<String>();
    mat.trim();
    if (mat.length() == 0) mat = (binId == BIN_ORGANIC_ID) ? "Organic" : "Inorganic";
    float conf = params["confidence"].as<float>();
    if (conf <= 0.0f) conf = 0.95f;
    broadcastDetectionEvent(mat, conf, "OPENING");
  } else if (commandType == "CLOSE") {
    selectedBin = binId;
    currentState = CLOSING_BIN;
  } else {
    return;
  }

  // Ack so backend removes this command (re-delivered until ack'd)
  if (cmdId > 0) {
    String ackUrl = String(backend_url) + "/api/bins/commands/ack";
    http.begin(client, ackUrl);
    http.addHeader("Content-Type", "application/json");
    String ackBody = "{\"ids\":[" + String(cmdId) + "]}";
    http.POST(ackBody);
    http.end();
  }
}  
// WiFi.mode(WIFI_AP_STA);  // Both AP and STA for ESP-NOW
//     WiFi.softAP("SmartBin_AP", "12345678");
//   }
// }

// ==================== WEB SERVER ====================
bool authenticateRequest(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Authorization")) return false;
  String auth = request->header("Authorization");
  return auth.startsWith("Bearer ") && auth.substring(7) == local_access_password;
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<h1>Smart Waste Bin</h1>";
    html += "<p>Organic: " + String(organicBinLevelPct) + "%</p>";
    html += "<p>Inorganic: " + String(nonOrganicBinLevelPct) + "%</p>";
    html += "<p>ESP-NOW Sent: " + String(espNowSendCount) + "</p>";
    html += "<p>ESP-NOW Received: " + String(espNowReceiveCount) + "</p>";
    html += "<p>ESP-NOW Failed: " + String(espNowFailCount) + "</p>";
    request->send(200, "text/html", html);
  });

  server.begin();
}

void setupWebSocket() {
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {}
void sendWebSocketStatus(uint8_t clientNum) {}
void handleWebSocketMessage(uint8_t clientNum, String message) {}
void sendBinDataToBackend() {}