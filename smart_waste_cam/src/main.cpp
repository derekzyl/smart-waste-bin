/**
 * ESP32-CAM Material Detection System
 * 
 * SAFE GPIO PINS USED (Assumes SD Card is UNUSED):
 * - GPIO 14: UART TX to ESP32 Main (Use SD CLK/CMD if available)
 * - GPIO 16: Trigger input from ESP32 Main (User identified as safe)
 * 
 * AVOIDED PINS:
 * - GPIO 0: Boot mode selection (must be HIGH during boot)
 * - GPIO 1/3: Used for programming/debugging
 * - GPIO 33: Internal LED / Not exposed
 * 
 * Communication: UART (GPIO 14 TX) + GPIO Trigger (GPIO 16)
 * Responds: Instantly via UART with material detection result
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include <WebSocketsServer.h>
#include <HTTPClient.h>

// ==================== CAMERA PINS (ESP32-CAM - Pre-defined) ====================
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

// ==================== SAFE GPIO PINS FOR COMMUNICATION ====================
// Based on https://lastminuteengineers.com/esp32-cam-pinout-reference/

// UART TX - GPIO 14 - SAFE TO USE (if SD card unused)
// GPIO 16 is requested for Trigger, so we use GPIO 14 for UART TX
#define UART_TX_PIN       14

// Trigger Input - GPIO 16 - User requested "safest" pin
// Note: GPIO 16 is often PSRAM CS. If camera instability occurs, disable PSRAM.
#define TRIGGER_PIN       16

#define UART_BAUD         115200

// ==================== GLOBAL VARIABLES ====================
const char* ssid = "*";
const char* password = "........a";
const char* backend_url = "https://xenophobic-netta-cybergenii-1584fde7.koyeb.app";

// Local access password
const char* local_access_password = "SmartBin2025";

WebSocketsServer webSocket(81);
AsyncWebServer server(80);

// Material detection state
volatile bool detectionRequested = false;
String lastDetectedMaterial = "UNKNOWN";
float lastConfidence = 0.0;

// Built-in LED on GPIO 33 (same as trigger pin, but we use INPUT_PULLUP)
#define BUILTIN_LED 33

// ==================== FUNCTION DECLARATIONS ====================
void setupCamera();
void setupWiFi();
void setupWebServer();
void setupUART();
void IRAM_ATTR triggerISR();
void detectMaterial();
void sendToBackend(uint8_t* image, size_t len);
void sendUARTResult(String material, float confidence);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
bool authenticateRequest(AsyncWebServerRequest *request);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=================================");
  Serial.println("ESP32-CAM Material Detection");
  Serial.println("=================================");
  
  // Initialize Camera
  setupCamera();
  
  // Initialize WiFi
  setupWiFi();
  
  // Initialize UART for communication with ESP32 Main
  setupUART();
  
  // Initialize Web Server
  setupWebServer();
  
  // Initialize WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  // Setup trigger pin with interrupt
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TRIGGER_PIN), triggerISR, RISING);
  
  Serial.println("\n=================================");
  Serial.println("ESP32-CAM READY");
  Serial.println("=================================");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("\nCommunication Setup:");
  Serial.println("  UART TX: GPIO 16 (to ESP32 Main RX2)");
  Serial.println("  Trigger: GPIO 33 (from ESP32 Main)");
  Serial.println("  Password: " + String(local_access_password));
  Serial.println("=================================\n");
}

// ==================== MAIN LOOP ====================
void loop() {
  webSocket.loop();
  
  // Check if detection was requested via trigger pin
  if (detectionRequested) {
    detectionRequested = false;
    Serial.println("\n>>> DETECTION TRIGGERED <<<");
    detectMaterial();
  }
  
  delay(10);
}

// ==================== INTERRUPT HANDLER ====================
void IRAM_ATTR triggerISR() {
  detectionRequested = true;
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
  
  // Frame size - optimized for AI detection
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;  // 640x480 - good balance
    config.jpeg_quality = 10;
    config.fb_count = 2;
    Serial.println("  PSRAM found - using VGA resolution");
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    Serial.println("  No PSRAM - using SVGA resolution");
  }
  
  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("  ❌ Camera init failed with error 0x%x\n", err);
    return;
  }
  
  Serial.println("  ✓ Camera initialized successfully");
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
  } else {
    Serial.println("\n  ❌ WiFi Connection Failed");
  }
}

// ==================== UART SETUP ====================
void setupUART() {
  // Initialize Serial2 for UART communication with ESP32 Main
  // TX = GPIO 16 (safe pin according to pinout reference)
  // We only need TX, no RX needed
  Serial2.begin(UART_BAUD, SERIAL_8N1, -1, UART_TX_PIN);  // RX=-1 (not used), TX=16
  
  Serial.println("\nUART Communication Setup:");
  Serial.println("  ✓ TX on GPIO 16 (to ESP32 Main RX2)");
  Serial.println("  Baud Rate: 115200");
}

// ==================== UART COMMUNICATION ====================
void sendUARTResult(String material, float confidence) {
  // Send material detection result via UART
  // Format: JSON string for easy parsing
  DynamicJsonDocument doc(256);
  doc["material"] = material;
  doc["confidence"] = confidence;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  // Send with newline delimiter
  Serial2.println(jsonString);
  
  Serial.println(">>> UART TX: " + jsonString);
}

// ==================== MATERIAL DETECTION ====================
void detectMaterial() {
  unsigned long startTime = millis();
  
  // Capture image
  Serial.println("Capturing image...");
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("  ❌ Camera capture failed");
    sendUARTResult("UNKNOWN", 0.0);
    return;
  }
  
  Serial.printf("  ✓ Image captured: %d bytes\n", fb->len);
  
  // Send image to backend for detection
  sendToBackend(fb->buf, fb->len);
  
  // Return frame buffer
  esp_camera_fb_return(fb);
  
  unsigned long elapsed = millis() - startTime;
  Serial.printf(">>> Total detection time: %lu ms\n\n", elapsed);
}

// ==================== BACKEND COMMUNICATION ====================
void sendToBackend(uint8_t* image, size_t len) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("  ❌ WiFi not connected");
    sendUARTResult("UNKNOWN", 0.0);
    return;
  }
  
  Serial.println("Sending to backend AI...");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, String(backend_url) + "/api/detect");
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(8000);  // 8 second timeout
  
  int httpResponseCode = http.POST(image, len);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("  ✓ Backend response [%d]: %s\n", httpResponseCode, response.c_str());
    
    // Parse response
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      String material = doc["material"];
      float confidence = doc["confidence"];
      
      Serial.println("  ✓ AI Detection Result:");
      Serial.printf("    Material: %s\n", material.c_str());
      Serial.printf("    Confidence: %.2f%%\n", confidence * 100);
      
      // Send result INSTANTLY via UART to ESP32 Main
      sendUARTResult(material, confidence);
      
      lastDetectedMaterial = material;
      lastConfidence = confidence;
    } else {
      Serial.println("  ❌ Failed to parse backend response");
      sendUARTResult("UNKNOWN", 0.0);
    }
  } else {
    Serial.printf("  ❌ Backend error: %s\n", http.errorToString(httpResponseCode).c_str());
    sendUARTResult("UNKNOWN", 0.0);
  }
  
  http.end();
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
  // Root endpoint
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<html><body style='font-family: monospace;'>";
    html += "<h1>ESP32-CAM Material Detection</h1>";
    html += "<p><b>Local IP:</b> " + WiFi.localIP().toString() + "</p>";
    html += "<p><b>UART TX:</b> GPIO 16</p>";
    html += "<p><b>Trigger:</b> GPIO 33</p>";
    html += "<p><b>Password:</b> " + String(local_access_password) + "</p>";
    html += "<hr><h3>Endpoints:</h3>";
    html += "<ul>";
    html += "<li>/capture - Capture image (requires auth)</li>";
    html += "<li>/api/material - Get last detection</li>";
    html += "<li>/api/detect - Trigger detection (requires auth)</li>";
    html += "</ul>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });
  
  // Capture and send image (with auth)
  server.on("/capture", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!authenticateRequest(request)) {
      request->send(401, "text/plain", "Unauthorized - Password required");
      return;
    }
    
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      request->send(500, "text/plain", "Camera capture failed");
      return;
    }
    
    request->send_P(200, "image/jpeg", (const uint8_t*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
  });
  
  // Get last detected material
  server.on("/api/material", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(256);
    doc["material"] = lastDetectedMaterial;
    doc["confidence"] = lastConfidence;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Trigger detection (with auth)
  server.on("/api/detect", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!authenticateRequest(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    
    detectionRequested = true;
    request->send(200, "application/json", "{\"status\":\"detecting\"}");
  });
  
  server.begin();
  Serial.println("  ✓ Web server started on port 80");
}

// ==================== WEBSOCKET ====================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("WS: Client [%u] disconnected\n", num);
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("WS: Client [%u] connected from %s\n", num, ip.toString().c_str());
        
        // Send current status
        DynamicJsonDocument doc(256);
        doc["material"] = lastDetectedMaterial;
        doc["confidence"] = lastConfidence;
        String response;
        serializeJson(doc, response);
        webSocket.sendTXT(num, response);
      }
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        DynamicJsonDocument doc(256);
        deserializeJson(doc, message);
        
        String cmd = doc["command"];
        if (cmd == "detect") {
          detectionRequested = true;
        } else if (cmd == "status") {
          DynamicJsonDocument resp(256);
          resp["material"] = lastDetectedMaterial;
          resp["confidence"] = lastConfidence;
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


// #include <Arduino.h>
// #include <WiFi.h>
// #include <ESPAsyncWebServer.h>
// #include <ArduinoJson.h>
// #include "esp_camera.h"
// #include "esp_http_server.h"
// #include <WebSocketsServer.h>

// // ==================== CAMERA PINS (ESP32-CAM) ====================
// #define PWDN_GPIO_NUM     32
// #define RESET_GPIO_NUM    -1
// #define XCLK_GPIO_NUM      0
// #define SIOD_GPIO_NUM     26
// #define SIOC_GPIO_NUM     27
// #define Y9_GPIO_NUM       35
// #define Y8_GPIO_NUM       34
// #define Y7_GPIO_NUM       39
// #define Y6_GPIO_NUM       36
// #define Y5_GPIO_NUM       21
// #define Y4_GPIO_NUM       19
// #define Y3_GPIO_NUM       18
// #define Y2_GPIO_NUM        5
// #define VSYNC_GPIO_NUM    25
// #define HREF_GPIO_NUM     23
// #define PCLK_GPIO_NUM     22

// // ==================== GLOBAL VARIABLES ====================
// const char* ssid = "YOUR_WIFI_SSID";
// const char* password = "YOUR_WIFI_PASSWORD";
// const char* backend_url = "http://your-backend-url.com";

// WebSocketsServer webSocket(81);
// AsyncWebServer server(80);

// // Material detection state
// bool isDetecting = false;
// String lastDetectedMaterial = "UNKNOWN";

// // ==================== FUNCTION DECLARATIONS ====================
// void setupCamera();
// void setupWiFi();
// void setupWebServer();
// void setupCAN();
// void sendCANMessage(uint32_t id, String message);
// bool receiveCANMessage(uint32_t* id, String* message);
// void detectMaterial();
// void sendToBackend(uint8_t* image, size_t len);
// void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

// // ==================== SETUP ====================
// void setup() {
//   Serial.begin(115200);
//   delay(1000);
  
//   // Initialize Camera
//   setupCamera();
  
//   // Initialize WiFi
//   setupWiFi();
  
//   // Initialize CAN
//   setupCAN();
  
//   // Initialize Web Server
//   setupWebServer();
  
//   // Initialize WebSocket
//   webSocket.begin();
//   webSocket.onEvent(webSocketEvent);
  
//   Serial.println("ESP32-CAM Material Detection System Initialized");
// }

// // ==================== MAIN LOOP ====================
// void loop() {
//   webSocket.loop();
  
//   // Check for CAN messages requesting material detection
//   uint32_t canId;
//   String canMessage;
  
//   if (receiveCANMessage(&canId, &canMessage)) {
//     if (canId == 0x100 && canMessage == "DETECT_MATERIAL") {
//       Serial.println("Material detection requested");
//       isDetecting = true;
//       detectMaterial();
//     }
//   }
  
//   delay(100);
// }

// // ==================== CAMERA SETUP ====================
// void setupCamera() {
//   camera_config_t config;
//   config.ledc_channel = LEDC_CHANNEL_0;
//   config.ledc_timer = LEDC_TIMER_0;
//   config.pin_d0 = Y2_GPIO_NUM;
//   config.pin_d1 = Y3_GPIO_NUM;
//   config.pin_d2 = Y4_GPIO_NUM;
//   config.pin_d3 = Y5_GPIO_NUM;
//   config.pin_d4 = Y6_GPIO_NUM;
//   config.pin_d5 = Y7_GPIO_NUM;
//   config.pin_d6 = Y8_GPIO_NUM;
//   config.pin_d7 = Y9_GPIO_NUM;
//   config.pin_xclk = XCLK_GPIO_NUM;
//   config.pin_pclk = PCLK_GPIO_NUM;
//   config.pin_vsync = VSYNC_GPIO_NUM;
//   config.pin_href = HREF_GPIO_NUM;
//   config.pin_sscb_sda = SIOD_GPIO_NUM;
//   config.pin_sscb_scl = SIOC_GPIO_NUM;
//   config.pin_pwdn = PWDN_GPIO_NUM;
//   config.pin_reset = RESET_GPIO_NUM;
//   config.xclk_freq_hz = 20000000;
//   config.pixel_format = PIXFORMAT_JPEG;
  
//   // Frame size
//   if(psramFound()){
//     config.frame_size = FRAMESIZE_VGA;
//     config.jpeg_quality = 10;
//     config.fb_count = 2;
//   } else {
//     config.frame_size = FRAMESIZE_SVGA;
//     config.jpeg_quality = 12;
//     config.fb_count = 1;
//   }
  
//   // Initialize camera
//   esp_err_t err = esp_camera_init(&config);
//   if (err != ESP_OK) {
//     Serial.printf("Camera init failed with error 0x%x", err);
//     return;
//   }
  
//   Serial.println("Camera initialized successfully");
// }

// // ==================== WIFI SETUP ====================
// void setupWiFi() {
//   WiFi.mode(WIFI_STA);
//   WiFi.begin(ssid, password);
  
//   Serial.print("Connecting to WiFi");
//   int attempts = 0;
//   while (WiFi.status() != WL_CONNECTED && attempts < 20) {
//     delay(500);
//     Serial.print(".");
//     attempts++;
//   }
  
//   if (WiFi.status() == WL_CONNECTED) {
//     Serial.println("\nWiFi Connected!");
//     Serial.print("IP Address: ");
//     Serial.println(WiFi.localIP());
//   } else {
//     Serial.println("\nWiFi Connection Failed");
//   }
// }

// // ==================== CAN SETUP ====================
// void setupCAN() {
//   // Initialize TWAI (CAN) on ESP32
//   Serial.println("CAN/TWAI initialized");
// }

// void sendCANMessage(uint32_t id, String message) {
//   // Simplified CAN message sending
//   Serial.printf("CAN TX: ID=0x%03X, Message=%s\n", id, message.c_str());
// }

// bool receiveCANMessage(uint32_t* id, String* message) {
//   // Simplified CAN message receiving
//   // In real implementation, use ESP32 TWAI library
//   return false;
// }

// // ==================== MATERIAL DETECTION ====================
// void detectMaterial() {
//   // Capture image
//   camera_fb_t * fb = esp_camera_fb_get();
//   if (!fb) {
//     Serial.println("Camera capture failed");
//     isDetecting = false;
//     return;
//   }
  
//   Serial.printf("Captured image: %d bytes\n", fb->len);
  
//   // Send image to backend for detection
//   sendToBackend(fb->buf, fb->len);
  
//   // Return frame buffer
//   esp_camera_fb_return(fb);
  
//   isDetecting = false;
// }

// // ==================== BACKEND COMMUNICATION ====================
// void sendToBackend(uint8_t* image, size_t len) {
//   if (WiFi.status() != WL_CONNECTED) {
//     Serial.println("WiFi not connected, cannot send to backend");
//     // Send CAN message with default response
//     sendCANMessage(0x200, "MATERIAL:UNKNOWN");
//     return;
//   }
  
//   HTTPClient http;
//   http.begin(String(backend_url) + "/api/detect");
//   http.addHeader("Content-Type", "image/jpeg");
  
//   int httpResponseCode = http.POST(image, len);
  
//   if (httpResponseCode > 0) {
//     String response = http.getString();
//     Serial.printf("Backend response: %d - %s\n", httpResponseCode, response.c_str());
    
//     // Parse response
//     DynamicJsonDocument doc(1024);
//     DeserializationError error = deserializeJson(doc, response);
    
//     if (!error) {
//       String material = doc["material"];
//       float confidence = doc["confidence"];
      
//       Serial.printf("Detected material: %s (confidence: %.2f)\n", material.c_str(), confidence);
      
//       // Send result via CAN
//       String canMessage = "MATERIAL:" + material;
//       sendCANMessage(0x200, canMessage);
      
//       lastDetectedMaterial = material;
//     } else {
//       Serial.println("Failed to parse backend response");
//       sendCANMessage(0x200, "MATERIAL:UNKNOWN");
//     }
//   } else {
//     Serial.printf("Backend error: %s\n", http.errorToString(httpResponseCode).c_str());
//     sendCANMessage(0x200, "MATERIAL:UNKNOWN");
//   }
  
//   http.end();
// }

// // ==================== WEB SERVER SETUP ====================
// void setupWebServer() {
//   // Root endpoint
//   server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
//     request->send(200, "text/html", "<html><body><h1>ESP32-CAM Material Detection</h1></body></html>");
//   });
  
//   // Capture and send image
//   server.on("/capture", HTTP_GET, [](AsyncWebServerRequest *request){
//     camera_fb_t * fb = esp_camera_fb_get();
//     if (!fb) {
//       request->send(500, "text/plain", "Camera capture failed");
//       return;
//     }
    
//     request->send_P(200, "image/jpeg", (const uint8_t*)fb->buf, fb->len);
//     esp_camera_fb_return(fb);
//   });
  
//   // Get last detected material
//   server.on("/api/material", HTTP_GET, [](AsyncWebServerRequest *request){
//     DynamicJsonDocument doc(256);
//     doc["material"] = lastDetectedMaterial;
//     doc["detecting"] = isDetecting;
    
//     String response;
//     serializeJson(doc, response);
//     request->send(200, "application/json", response);
//   });
  
//   // Trigger detection
//   server.on("/api/detect", HTTP_POST, [](AsyncWebServerRequest *request){
//     isDetecting = true;
//     detectMaterial();
//     request->send(200, "application/json", "{\"status\":\"detecting\"}");
//   });
  
//   server.begin();
// }

// // ==================== WEBSOCKET ====================
// void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
//   switch(type) {
//     case WStype_DISCONNECTED:
//       Serial.printf("Client [%u] disconnected\n", num);
//       break;
      
//     case WStype_CONNECTED:
//       Serial.printf("Client [%u] connected\n", num);
//       break;
      
//     case WStype_TEXT:
//       // Handle commands
//       break;
      
//     default:
//       break;
//   }
// }

