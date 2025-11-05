# Smart Waste Bin - Setup Guide

## Prerequisites

- Python 3.8+ (for backend)
- Flutter SDK (for mobile app)
- PlatformIO (for ESP32 firmware)
- Arduino IDE or VS Code with PlatformIO extension

## Step-by-Step Setup

### 1. Hardware Assembly

1. **Connect ESP32 Main Controller:**
   - PIR sensor to GPIO 2
   - Ultrasonic sensor (TRIG=GPIO 4, ECHO=GPIO 5)
   - Servo motors (Organic=GPIO 18, Non-Organic=GPIO 19)
   - Load cell via HX711 (DOUT=GPIO 16, SCK=GPIO 17)
   - LEDs (Red=GPIO 25, Green=GPIO 26, Blue=GPIO 27)
   - Buzzer to GPIO 14
   - Keypad buttons (GPIO 12, 13)
   - CAN bus (TX=GPIO 21, RX=GPIO 22)

2. **Connect ESP32-CAM:**
   - Camera module (standard ESP32-CAM pins)
   - CAN bus (TX=GPIO 21, RX=GPIO 22)

3. **Power Supply:**
   - Connect 18650 batteries via TP4056 charger
   - Use boost converter to 5V for servos
   - Use LDO to 3.3V for ESP32

### 2. Firmware Configuration

#### ESP32 Main Controller

1. Edit `smart_waste_bin_firmware_/src/main.cpp`:
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   const char* backend_url = "http://YOUR_IP:8000";
   ```

2. Calibrate load cell:
   ```cpp
   scale.set_scale(2280.f); // Adjust this value based on your load cell
   ```

3. Adjust servo angles:
   ```cpp
   servoOrganic.write(90); // Open position (adjust as needed)
   servoOrganic.write(0);  // Close position (adjust as needed)
   ```

4. Upload firmware:
   ```bash
   cd smart_waste_bin_firmware_
   pio run -e esp32dev -t upload
   ```

#### ESP32-CAM

1. Edit `smart_waste_bin_firmware_/src/esp32cam_main.cpp`:
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   const char* backend_url = "http://YOUR_IP:8000";
   ```

2. Upload firmware:
   ```bash
   pio run -e esp32cam -t upload
   ```

### 3. Backend Setup

1. Install dependencies:
   ```bash
   cd backend
   pip install -r requirements.txt
   ```

2. (Optional) For better material detection, train an ML model:
   - Collect training images of organic/non-organic waste
   - Train a TensorFlow/PyTorch model
   - Replace the simple classifier in `backend/main.py`

3. Run backend:
   ```bash
   python main.py
   ```

4. Test API:
   - Open browser: `http://localhost:8000/docs`
   - Test endpoints using Swagger UI

### 4. Flutter App Setup

1. Install dependencies:
   ```bash
   cd smart_bin_app
   flutter pub get
   ```

2. Configure settings:
   - Open app → Settings
   - Enter backend URL (default: `http://192.168.1.100:8000`)
   - (Optional) Enter ESP32 IP for direct connection

3. Run app:
   ```bash
   flutter run
   ```

### 5. Network Configuration

1. **Find ESP32 IP addresses:**
   - Check router admin panel
   - Or check serial monitor during ESP32 boot

2. **Update backend URL:**
   - In firmware: Set `backend_url` to your computer's IP
   - In Flutter app: Set backend URL in settings

3. **Firewall:**
   - Allow port 8000 (backend)
   - Allow port 80 (ESP32 HTTP)
   - Allow port 81 (ESP32 WebSocket)

## Testing Checklist

### Hardware Tests

- [ ] PIR sensor detects motion
- [ ] Ultrasonic sensor measures distance
- [ ] Load cell reads weight correctly
- [ ] Servos open/close smoothly
- [ ] LEDs light up correctly
- [ ] Buzzer sounds
- [ ] Keypad buttons work

### Firmware Tests

- [ ] ESP32 connects to WiFi
- [ ] ESP32-CAM connects to WiFi
- [ ] CAN communication works (ESP32 ↔ ESP32-CAM)
- [ ] HTTP endpoints respond
- [ ] WebSocket connection works

### Backend Tests

- [ ] Material detection API works
- [ ] Bin status updates correctly
- [ ] Statistics endpoint returns data

### App Tests

- [ ] App connects to backend
- [ ] App connects to ESP32 directly
- [ ] Bin status displays correctly
- [ ] Open/close commands work
- [ ] Statistics display correctly

## Troubleshooting

### ESP32 Won't Connect to WiFi
- Check SSID and password
- Check WiFi signal strength
- Try ESP32 AP mode (check serial monitor)

### Material Detection Not Working
- Check ESP32-CAM is powered correctly
- Verify camera initialization
- Check CAN bus connection
- Test backend API directly

### App Can't Connect
- Verify backend is running
- Check IP addresses are correct
- Check firewall settings
- Try ping from device to backend

### Servos Not Moving
- Check power supply (servos need 5V)
- Verify GPIO pins
- Check servo angles (may need adjustment)
- Test servos individually

## Calibration

### Load Cell Calibration

1. Place known weight on load cell
2. Read raw value from serial monitor
3. Calculate scale factor:
   ```cpp
   scale_factor = known_weight / raw_value
   scale.set_scale(scale_factor);
   ```

### Ultrasonic Sensor Calibration

1. Measure actual bin height
2. Adjust distance mapping in `updateBinLevel()`:
   ```cpp
   float level = map(distance, min_distance, max_distance, 100, 0);
   ```

### Servo Calibration

1. Test servo angles manually
2. Find open/close positions
3. Update in `openBin()` and `closeBin()`

## Production Considerations

1. **Security:**
   - Use HTTPS for backend
   - Implement authentication
   - Encrypt sensitive data

2. **Database:**
   - Replace in-memory storage with database
   - Implement data persistence

3. **ML Model:**
   - Train proper material detection model
   - Optimize for edge deployment (TensorFlow Lite)

4. **Power Management:**
   - Implement deep sleep for ESP32
   - Add battery monitoring
   - Consider solar charging

5. **Monitoring:**
   - Add logging system
   - Implement error reporting
   - Set up alerts for full bins

