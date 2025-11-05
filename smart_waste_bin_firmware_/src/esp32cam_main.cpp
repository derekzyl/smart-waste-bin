#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include <WebSocketsServer.h>

// ==================== CAMERA PINS (ESP32-CAM) ====================
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

// ==================== GLOBAL VARIABLES ====================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* backend_url = "http://your-backend-url.com";

WebSocketsServer webSocket(81);
AsyncWebServer server(80);

// Material detection state
bool isDetecting = false;
String lastDetectedMaterial = "UNKNOWN";

// ==================== FUNCTION DECLARATIONS ====================
void setupCamera();
void setupWiFi();
void setupWebServer();
void setupCAN();
void sendCANMessage(uint32_t id, String message);
bool receiveCANMessage(uint32_t* id, String* message);
void detectMaterial();
void sendToBackend(uint8_t* image, size_t len);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize Camera
  setupCamera();
  
  // Initialize WiFi
  setupWiFi();
  
  // Initialize CAN
  setupCAN();
  
  // Initialize Web Server
  setupWebServer();
  
  // Initialize WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  Serial.println("ESP32-CAM Material Detection System Initialized");
}

// ==================== MAIN LOOP ====================
void loop() {
  webSocket.loop();
  
  // Check for CAN messages requesting material detection
  uint32_t canId;
  String canMessage;
  
  if (receiveCANMessage(&canId, &canMessage)) {
    if (canId == 0x100 && canMessage == "DETECT_MATERIAL") {
      Serial.println("Material detection requested");
      isDetecting = true;
      detectMaterial();
    }
  }
  
  delay(100);
}

// ==================== CAMERA SETUP ====================
void setupCamera() {
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Frame size
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  
  Serial.println("Camera initialized successfully");
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
    Serial.println("\nWiFi Connection Failed");
  }
}

// ==================== CAN SETUP ====================
void setupCAN() {
  // Initialize TWAI (CAN) on ESP32
  Serial.println("CAN/TWAI initialized");
}

void sendCANMessage(uint32_t id, String message) {
  // Simplified CAN message sending
  Serial.printf("CAN TX: ID=0x%03X, Message=%s\n", id, message.c_str());
}

bool receiveCANMessage(uint32_t* id, String* message) {
  // Simplified CAN message receiving
  // In real implementation, use ESP32 TWAI library
  return false;
}

// ==================== MATERIAL DETECTION ====================
void detectMaterial() {
  // Capture image
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    isDetecting = false;
    return;
  }
  
  Serial.printf("Captured image: %d bytes\n", fb->len);
  
  // Send image to backend for detection
  sendToBackend(fb->buf, fb->len);
  
  // Return frame buffer
  esp_camera_fb_return(fb);
  
  isDetecting = false;
}

// ==================== BACKEND COMMUNICATION ====================
void sendToBackend(uint8_t* image, size_t len) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot send to backend");
    // Send CAN message with default response
    sendCANMessage(0x200, "MATERIAL:UNKNOWN");
    return;
  }
  
  HTTPClient http;
  http.begin(String(backend_url) + "/api/detect");
  http.addHeader("Content-Type", "image/jpeg");
  
  int httpResponseCode = http.POST(image, len);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("Backend response: %d - %s\n", httpResponseCode, response.c_str());
    
    // Parse response
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      String material = doc["material"];
      float confidence = doc["confidence"];
      
      Serial.printf("Detected material: %s (confidence: %.2f)\n", material.c_str(), confidence);
      
      // Send result via CAN
      String canMessage = "MATERIAL:" + material;
      sendCANMessage(0x200, canMessage);
      
      lastDetectedMaterial = material;
    } else {
      Serial.println("Failed to parse backend response");
      sendCANMessage(0x200, "MATERIAL:UNKNOWN");
    }
  } else {
    Serial.printf("Backend error: %s\n", http.errorToString(httpResponseCode).c_str());
    sendCANMessage(0x200, "MATERIAL:UNKNOWN");
  }
  
  http.end();
}

// ==================== WEB SERVER SETUP ====================
void setupWebServer() {
  // Root endpoint
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<html><body><h1>ESP32-CAM Material Detection</h1></body></html>");
  });
  
  // Capture and send image
  server.on("/capture", HTTP_GET, [](AsyncWebServerRequest *request){
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
    doc["detecting"] = isDetecting;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Trigger detection
  server.on("/api/detect", HTTP_POST, [](AsyncWebServerRequest *request){
    isDetecting = true;
    detectMaterial();
    request->send(200, "application/json", "{\"status\":\"detecting\"}");
  });
  
  server.begin();
}

// ==================== WEBSOCKET ====================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("Client [%u] disconnected\n", num);
      break;
      
    case WStype_CONNECTED:
      Serial.printf("Client [%u] connected\n", num);
      break;
      
    case WStype_TEXT:
      // Handle commands
      break;
      
    default:
      break;
  }
}

