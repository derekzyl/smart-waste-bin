/**
 * ESP32-CAM Material Detection - COMPLETE WIRELESS VERSION
 * 
 * Features:
 * - ESP-NOW wireless communication with ESP32 Main
 * - GPIO trigger input (backup/redundant)
 * - UART TX output (backup)
 * - WiFi backend integration
 * - Boot-safe pin configuration
 * 
 * Pin Configuration:
 * - UART TX: GPIO 14 (SD CLK - safe if SD unused)
 * - Trigger: GPIO 13 (SD D3 - boot-safe)
 * 
 * Communication Priority:
 * 1. ESP-NOW (wireless) - Primary
 * 2. GPIO Trigger - Backup
 * 3. UART TX - Backup response
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ==================== CAMERA PINS ====================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ==================== COMMUNICATION PINS ====================
// UART TX - GPIO 14 (SD CLK - safe if SD unused)
#define UART_TX_PIN       14

// Trigger Input - GPIO 13 (SD D3 - boot-safe)
#define TRIGGER_PIN       13

#define UART_BAUD         115200

// Built-in flash LED
#define FLASH_LED_PIN     4

// ==================== GLOBAL VARIABLES ====================
const char* ssid = "cybergenii";
const char* password = "12341234";
const char* backend_url = "https://xenophobic-netta-cybergenii-1584fde7.koyeb.app";
const char* local_access_password = "SmartBin2025";

WebSocketsServer webSocket(81);
AsyncWebServer server(80);

// ==================== ESP-NOW VARIABLES ====================
// ESP32 Main MAC Address - WILL BE AUTO-DETECTED
uint8_t mainControllerMAC[6];
bool mainControllerRegistered = false;
bool espNowInitialized = false;

// ESP-NOW Message Structures (must match ESP32 Main)
typedef struct {
  char command[32];
  unsigned long timestamp;
} CommandMessage;

typedef struct {
  char material[32];
  float confidence;
  unsigned long timestamp;
} DetectionResult;

// ==================== STATE VARIABLES ====================
volatile bool detectionRequested = false;
String lastDetectedMaterial = "UNKNOWN";
float lastConfidence = 0.0;
unsigned long lastDetectionTime = 0;
unsigned long bootTime = 0;

// Statistics
int totalDetections = 0;
int wirelessTriggers = 0;
int gpioTriggers = 0;
int wirelessResponses = 0;
int uartResponses = 0;

// ==================== FUNCTION DECLARATIONS ====================
void setupCamera();
void setupWiFi();
void setupESPNow();
void setupWebServer();
void setupUART();
void IRAM_ATTR triggerISR();
void detectMaterial();
void sendToBackend(uint8_t* image, size_t len);
void sendUARTResult(String material, float confidence);
void sendESPNowResult(String material, float confidence);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
bool authenticateRequest(AsyncWebServerRequest *request);
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len);
void onDataSent(const uint8_t *mac, esp_now_send_status_t status);
void flashLED(int times);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  bootTime = millis();
  
  Serial.println("\n\n=================================");
  Serial.println("ESP32-CAM Material Detection");
  Serial.println("ESP-NOW Wireless + Backup GPIO/UART");
  Serial.println("=================================");
  
  // Initialize flash LED
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);
  
  // Flash LED on boot
  flashLED(3);
  
  // Initialize Camera FIRST
  setupCamera();
  
  // Initialize WiFi
  setupWiFi();
  
  // Print MAC address for pairing
  Serial.print("\n📍 ESP32-CAM MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.println("   → Add this to ESP32 Main's camMacAddress[]");
  
  // Initialize ESP-NOW
  setupESPNow();
  
  // Initialize UART (backup)
  setupUART();
  
  // Initialize Web Server
  setupWebServer();
  
  // Initialize WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  // Setup trigger pin (GPIO 13 - boot-safe)
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TRIGGER_PIN), triggerISR, RISING);
  
  Serial.println("\n=================================");
  Serial.println("ESP32-CAM READY");
  Serial.println("=================================");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  
  Serial.println("\n📡 Communication Status:");
  Serial.println("  ESP-NOW: " + String(espNowInitialized ? "✅ Active" : "❌ Failed"));
  Serial.println("  GPIO Trigger: ✅ Active (GPIO 13)");
  Serial.println("  UART TX: ✅ Active (GPIO 14)");
  
  Serial.println("\n📍 Pin Configuration:");
  Serial.printf("  UART TX: GPIO%d → ESP32 Main GPIO15\n", UART_TX_PIN);
  Serial.printf("  Trigger: GPIO%d ← ESP32 Main GPIO4\n", TRIGGER_PIN);
  
  Serial.println("\n🎥 Waiting for detection trigger...");
  Serial.println("=================================\n");
  
  // Send ready signal
  delay(500);
  sendESPNowResult("SYSTEM_READY", 1.0);
  sendUARTResult("SYSTEM_READY", 1.0);
}

// ==================== MAIN LOOP ====================
void loop() {
  webSocket.loop();
  
  if (detectionRequested) {
    detectionRequested = false;
    
    // Debounce
    if (millis() - lastDetectionTime > 1000) {
      Serial.println("\n>>> DETECTION TRIGGERED <<<");
      detectMaterial();
      lastDetectionTime = millis();
      totalDetections++;
    }
  }
  
  delay(10);
}

// ==================== INTERRUPT HANDLER ====================
void IRAM_ATTR triggerISR() {
  detectionRequested = true;
  gpioTriggers++;
}

// ==================== ESP-NOW SETUP ====================
void setupESPNow() {
  Serial.println("\nInitializing ESP-NOW...");
  
  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("  ❌ ESP-NOW init failed");
    espNowInitialized = false;
    return;
  }
  
  espNowInitialized = true;
  Serial.println("  ✓ ESP-NOW initialized");
  
  // Register callbacks
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);
  
  Serial.println("  ✓ Callbacks registered");
  Serial.println("  → Waiting for ESP32 Main to pair...");
}

// ==================== ESP-NOW CALLBACKS ====================
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("📡 ESP-NOW: Send ✅ Success");
    wirelessResponses++;
  } else {
    Serial.println("📡 ESP-NOW: Send ❌ Failed");
    // Fallback to UART
    Serial.println("  → Using UART fallback");
  }
}

void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  Serial.println("\n📡 ESP-NOW Command Received");
  
  // Auto-register sender as peer if not already registered
  if (!mainControllerRegistered) {
    memcpy(mainControllerMAC, mac, 6);
    
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      mainControllerRegistered = true;
      Serial.print("  ✅ ESP32 Main registered: ");
      for (int i = 0; i < 6; i++) {
        Serial.printf("%02X", mac[i]);
        if (i < 5) Serial.print(":");
      }
      Serial.println();
    }
  }
  
  CommandMessage cmd;
  memcpy(&cmd, incomingData, sizeof(cmd));
  
  Serial.printf("  Command: %s\n", cmd.command);
  Serial.printf("  Timestamp: %lu ms\n", cmd.timestamp);
  
  if (strcmp(cmd.command, "DETECT") == 0) {
    detectionRequested = true;
    wirelessTriggers++;
    Serial.println("  → Triggering detection...");
  }
}

// ==================== CAMERA SETUP ====================
void setupCamera() {
  Serial.println("Initializing camera...");
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;  // 640x480
    config.jpeg_quality = 10;
    config.fb_count = 2;
    Serial.println("  ✓ PSRAM found - using VGA resolution");
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    Serial.println("  ⚠️  No PSRAM - using SVGA resolution");
  }
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("  ❌ Camera init failed: 0x%x\n", err);
    Serial.println("  Restarting in 3 seconds...");
    delay(3000);
    ESP.restart();
  }
  
  Serial.println("  ✓ Camera initialized successfully");
  
  // Sensor settings for better detection
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);     // -2 to 2
    s->set_contrast(s, 0);       // -2 to 2
    s->set_saturation(s, 0);     // -2 to 2
    s->set_special_effect(s, 0); // 0 = No Effect
    s->set_whitebal(s, 1);       // 0 = disable, 1 = enable
    s->set_awb_gain(s, 1);       // 0 = disable, 1 = enable
    s->set_wb_mode(s, 0);        // 0 to 4
    s->set_exposure_ctrl(s, 1);  // 0 = disable, 1 = enable
    s->set_aec2(s, 0);           // 0 = disable, 1 = enable
    s->set_ae_level(s, 0);       // -2 to 2
    s->set_aec_value(s, 300);    // 0 to 1200
    s->set_gain_ctrl(s, 1);      // 0 = disable, 1 = enable
    s->set_agc_gain(s, 0);       // 0 to 30
    s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
    s->set_bpc(s, 0);            // 0 = disable, 1 = enable
    s->set_wpc(s, 1);            // 0 = disable, 1 = enable
    s->set_raw_gma(s, 1);        // 0 = disable, 1 = enable
    s->set_lenc(s, 1);           // 0 = disable, 1 = enable
    s->set_hmirror(s, 0);        // 0 = disable, 1 = enable
    s->set_vflip(s, 0);          // 0 = disable, 1 = enable
    s->set_dcw(s, 1);            // 0 = disable, 1 = enable
    s->set_colorbar(s, 0);       // 0 = disable, 1 = enable
    
    Serial.println("  ✓ Camera settings optimized");
  }
}

// ==================== WIFI SETUP ====================
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
    Serial.println("\n  ✓ WiFi Connected!");
    Serial.print("  IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Signal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n  ❌ WiFi Failed - Continuing with local communication");
    Serial.println("  → ESP-NOW and GPIO/UART still work!");
  }
}

// ==================== UART SETUP ====================
void setupUART() {
  Serial2.begin(UART_BAUD, SERIAL_8N1, -1, UART_TX_PIN);
  
  Serial.println("\nUART Backup Communication:");
  Serial.printf("  ✓ TX on GPIO%d (to ESP32 Main GPIO15)\n", UART_TX_PIN);
  Serial.println("  Baud Rate: 115200");
}

// ==================== LED FLASH ====================
void flashLED(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(100);
    digitalWrite(FLASH_LED_PIN, LOW);
    delay(100);
  }
}

// ==================== MATERIAL DETECTION ====================
void detectMaterial() {
  unsigned long startTime = millis();
  
  // Flash LED during capture
  digitalWrite(FLASH_LED_PIN, HIGH);
  
  Serial.println("📸 Capturing image...");
  camera_fb_t *fb = esp_camera_fb_get();
  
  digitalWrite(FLASH_LED_PIN, LOW);
  
  if (!fb) {
    Serial.println("  ❌ Camera capture failed");
    sendESPNowResult("UNKNOWN", 0.0);
    sendUARTResult("UNKNOWN", 0.0);
    return;
  }
  
  Serial.printf("  ✓ Image captured: %d bytes\n", fb->len);
  Serial.printf("  Resolution: %dx%d\n", fb->width, fb->height);
  
  // Send to backend
  sendToBackend(fb->buf, fb->len);
  
  // Return frame buffer
  esp_camera_fb_return(fb);
  
  unsigned long elapsed = millis() - startTime;
  Serial.printf(">>> Total detection time: %lu ms\n", elapsed);
  
  // Print statistics
  Serial.println("\n📊 Statistics:");
  Serial.printf("  Total Detections: %d\n", totalDetections);
  Serial.printf("  Wireless Triggers: %d\n", wirelessTriggers);
  Serial.printf("  GPIO Triggers: %d\n", gpioTriggers);
  Serial.printf("  Wireless Responses: %d\n", wirelessResponses);
  Serial.printf("  UART Responses: %d\n", uartResponses);
  Serial.printf("  Uptime: %lu seconds\n\n", (millis() - bootTime) / 1000);
}

// ==================== BACKEND COMMUNICATION ====================
void sendToBackend(uint8_t *image, size_t len) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("  ⚠️  WiFi not connected - cannot analyze image");
    Serial.println("  → Sending UNKNOWN result");
    sendESPNowResult("UNKNOWN", 0.0);
    sendUARTResult("UNKNOWN", 0.0);
    return;
  }
  
  Serial.println("🤖 Sending to backend AI...");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  http.begin(client, String(backend_url) + "/api/detect");
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(10000);  // 10 second timeout
  
  int httpResponseCode = http.POST(image, len);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("  ✓ Backend response [%d]\n", httpResponseCode);
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      String material = doc["material"].as<String>();
      float confidence = doc["confidence"].as<float>();
      
      Serial.println("  ✅ AI Detection Result:");
      Serial.printf("    Material: %s\n", material.c_str());
      Serial.printf("    Confidence: %.2f%%\n", confidence * 100);
      
      // Send results via all channels
      sendESPNowResult(material, confidence);
      sendUARTResult(material, confidence);
      
      lastDetectedMaterial = material;
      lastConfidence = confidence;
      
      // Broadcast to WebSocket clients
      DynamicJsonDocument wsDoc(256);
      wsDoc["event"] = "detection_complete";
      wsDoc["material"] = material;
      wsDoc["confidence"] = confidence;
      wsDoc["timestamp"] = millis();
      String wsMessage;
      serializeJson(wsDoc, wsMessage);
      webSocket.broadcastTXT(wsMessage);
      
      // Flash LED to indicate success
      flashLED(2);
      
    } else {
      Serial.println("  ❌ Failed to parse backend response");
      Serial.println("  Raw: " + response);
      sendESPNowResult("UNKNOWN", 0.0);
      sendUARTResult("UNKNOWN", 0.0);
    }
  } else {
    Serial.printf("  ❌ HTTP error: %s\n", http.errorToString(httpResponseCode).c_str());
    sendESPNowResult("UNKNOWN", 0.0);
    sendUARTResult("UNKNOWN", 0.0);
  }
  
  http.end();
}

// ==================== ESP-NOW RESULT SENDING ====================
void sendESPNowResult(String material, float confidence) {
  if (!espNowInitialized) {
    Serial.println("⚠️  ESP-NOW not initialized - skipping wireless send");
    return;
  }
  
  if (!mainControllerRegistered) {
    Serial.println("⚠️  ESP32 Main not registered - skipping wireless send");
    return;
  }
  
  DetectionResult result;
  strncpy(result.material, material.c_str(), sizeof(result.material) - 1);
  result.material[sizeof(result.material) - 1] = '\0';
  result.confidence = confidence;
  result.timestamp = millis();
  
  esp_err_t sendResult = esp_now_send(mainControllerMAC, (uint8_t *)&result, sizeof(result));
  
  if (sendResult == ESP_OK) {
    Serial.printf("📡 ESP-NOW result sent: %s (%.2f%%)\n", material.c_str(), confidence * 100);
  } else {
    Serial.println("❌ ESP-NOW send failed");
  }
}

// ==================== UART RESULT SENDING ====================
void sendUARTResult(String material, float confidence) {
  DynamicJsonDocument doc(256);
  doc["material"] = material;
  doc["confidence"] = confidence;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial2.println(jsonString);
  uartResponses++;
  
  Serial.println("📡 UART TX: " + jsonString);
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

// ==================== WEB SERVER ====================
void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='10'>";  // Auto-refresh every 10s
    html += "<style>body{font-family:monospace;max-width:800px;margin:20px auto;padding:20px;background:#f5f5f5;}";
    html += "h1{color:#333;border-bottom:2px solid #4CAF50;}";
    html += ".card{background:white;padding:15px;margin:10px 0;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
    html += ".status{color:green;font-weight:bold;font-size:1.2em;}";
    html += ".stat{display:inline-block;margin:10px 20px 10px 0;}";
    html += ".label{color:#666;font-size:0.9em;}</style></head><body>";
    html += "<h1>🎥 ESP32-CAM Material Detection</h1>";
    
    html += "<div class='card'>";
    html += "<div class='status'>✅ System Operational</div>";
    html += "<div class='label'>Uptime: " + String((millis() - bootTime) / 1000) + " seconds</div>";
    html += "</div>";
    
    html += "<div class='card'>";
    html += "<h3>📡 Communication Status</h3>";
    html += "<div>ESP-NOW: " + String(espNowInitialized ? "✅ Active" : "❌ Inactive") + "</div>";
    html += "<div>Main Controller: " + String(mainControllerRegistered ? "✅ Paired" : "⏳ Waiting") + "</div>";
    html += "<div>WiFi: " + String(WiFi.status() == WL_CONNECTED ? "✅ Connected" : "❌ Disconnected") + "</div>";
    html += "<div>IP: " + WiFi.localIP().toString() + "</div>";
    html += "</div>";
    
    html += "<div class='card'>";
    html += "<h3>🔍 Last Detection</h3>";
    html += "<div><strong>Material:</strong> " + lastDetectedMaterial + "</div>";
    html += "<div><strong>Confidence:</strong> " + String(lastConfidence * 100, 1) + "%</div>";
    html += "<div class='label'>Time: " + String((millis() - lastDetectionTime) / 1000) + "s ago</div>";
    html += "</div>";
    
    html += "<div class='card'>";
    html += "<h3>📊 Statistics</h3>";
    html += "<div class='stat'><strong>Total:</strong> " + String(totalDetections) + "</div>";
    html += "<div class='stat'><strong>Wireless Triggers:</strong> " + String(wirelessTriggers) + "</div>";
    html += "<div class='stat'><strong>GPIO Triggers:</strong> " + String(gpioTriggers) + "</div>";
    html += "<div class='stat'><strong>Wireless Responses:</strong> " + String(wirelessResponses) + "</div>";
    html += "<div class='stat'><strong>UART Responses:</strong> " + String(uartResponses) + "</div>";
    html += "</div>";
    
    html += "<div class='card'>";
    html += "<h3>📍 Pin Configuration</h3>";
    html += "<div>UART TX: GPIO 14 → ESP32 Main GPIO 15</div>";
    html += "<div>Trigger: GPIO 13 ← ESP32 Main GPIO 4</div>";
    html += "<div class='label'>✅ Boot-safe configuration</div>";
    html += "</div>";
    
    html += "<div class='card'>";
    html += "<h3>🔌 API Endpoints</h3>";
    html += "<ul>";
    html += "<li><strong>GET</strong> /capture - View camera image (auth)</li>";
    html += "<li><strong>GET</strong> /api/material - Last detection result</li>";
    html += "<li><strong>POST</strong> /api/detect - Trigger detection (auth)</li>";
    html += "<li><strong>GET</strong> /api/stats - System statistics</li>";
    html += "</ul>";
    html += "</div>";
    
    html += "<div class='card label'>";
    html += "📍 MAC Address: " + WiFi.macAddress();
    html += "</div>";
    
    html += "</body></html>";
    request->send(200, "text/html", html);
  });
  
  server.on("/capture", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!authenticateRequest(request)) {
      request->send(401, "text/plain", "Unauthorized - Add header: Authorization: Bearer SmartBin2025");
      return;
    }
    
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      request->send(500, "text/plain", "Camera capture failed");
      return;
    }
    
    request->send_P(200, "image/jpeg", (const uint8_t *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
  });
  
  server.on("/api/material", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512);
    doc["material"] = lastDetectedMaterial;
    doc["confidence"] = lastConfidence;
    doc["timestamp"] = lastDetectionTime;
    doc["wifi_connected"] = WiFi.status() == WL_CONNECTED;
    doc["espnow_enabled"] = espNowInitialized;
    doc["main_paired"] = mainControllerRegistered;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  server.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512);
    doc["total_detections"] = totalDetections;
    doc["wireless_triggers"] = wirelessTriggers;
    doc["gpio_triggers"] = gpioTriggers;
    doc["wireless_responses"] = wirelessResponses;
    doc["uart_responses"] = uartResponses;
    doc["uptime_seconds"] = (millis() - bootTime) / 1000;
    doc["espnow_enabled"] = espNowInitialized;
    doc["main_paired"] = mainControllerRegistered;
    doc["mac_address"] = WiFi.macAddress();
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  server.on("/api/detect", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!authenticateRequest(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    
    detectionRequested = true;
    request->send(200, "application/json", "{\"status\":\"triggering_detection\"}");
  });
  
  server.begin();
  Serial.println("  ✓ Web server started on port 80");
}

// ==================== WEBSOCKET ====================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("WS: Client [%u] disconnected\n", num);
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("WS: Client [%u] connected from %s\n", num, ip.toString().c_str());
        
        DynamicJsonDocument doc(512);
        doc["event"] = "connected";
        doc["material"] = lastDetectedMaterial;
        doc["confidence"] = lastConfidence;
        doc["espnow_enabled"] = espNowInitialized;
        doc["main_paired"] = mainControllerRegistered;
        String response;
        serializeJson(doc, response);
        webSocket.sendTXT(num, response);
      }
      break;
      
    case WStype_TEXT:
      {
        String message = String((char *)payload);
        DynamicJsonDocument doc(256);
        deserializeJson(doc, message);
        
        String cmd = doc["command"];
        if (cmd == "detect") {
          detectionRequested = true;
          webSocket.sendTXT(num, "{\"status\":\"detecting\"}");
        } else if (cmd == "status") {
          DynamicJsonDocument resp(512);
          resp["material"] = lastDetectedMaterial;
          resp["confidence"] = lastConfidence;
          resp["espnow_enabled"] = espNowInitialized;
          resp["main_paired"] = mainControllerRegistered;
          resp["total_detections"] = totalDetections;
          String response;
          serializeJson(resp, response);
          webSocket.sendTXT(num, response);
        } else if (cmd == "stats") {
          DynamicJsonDocument resp(512);
          resp["total_detections"] = totalDetections;
          resp["wireless_triggers"] = wirelessTriggers;
          resp["gpio_triggers"] = gpioTriggers;
          resp["wireless_responses"] = wirelessResponses;
          resp["uart_responses"] = uartResponses;
          resp["uptime"] = (millis() - bootTime) / 1000;
          String response;
          serializeJson(resp, response);
          webSocket.sendTXT(num, response);
        }
      }
      break;
      
    default:
      break;
  }
}
// ```

// ---

// # 📋 **SETUP INSTRUCTIONS**

// ## **Step 1: Get MAC Addresses**

// 1. **Upload ESP32-CAM code first**
// 2. **Open Serial Monitor** - it will print:
// ```
//    📍 ESP32-CAM MAC Address: AA:BB:CC:DD:EE:FF