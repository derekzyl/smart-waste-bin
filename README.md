# Smart Waste Bin - IoT Waste Management System

A comprehensive IoT-based smart waste bin system that automatically detects material type (organic/non-organic) and manages waste segregation using ESP32 microcontrollers, ESP32-CAM for material detection, and a Flutter mobile app for control and monitoring.

## 🎯 Features

- **Motion Detection**: Automatically detects when someone approaches the bin
- **Material Classification**: Uses ESP32-CAM to classify waste as organic or non-organic
- **Automatic Bin Opening**: Opens the appropriate bin based on material type
- **Bin Level Monitoring**: Tracks bin fill level using ultrasonic sensors and load cells
- **Full Bin Protection**: Prevents opening when bins are full (except via app or keypad)
- **LED Indicators**: Visual feedback for bin status and fill level
- **Dual Communication**: WiFi communication with backend and direct ESP32 communication
- **Mobile App Control**: Full UI control via Flutter app
- **Backend API**: FastAPI backend for material detection and data management
- **Unique Bin IDs**: Each bin has a unique identifier for easy tracking

## 📋 System Architecture

```
┌─────────────────┐
│   Flutter App   │
│  (Mobile/Web)   │
└────────┬────────┘
         │
         ├───────────────────┐
         │                   │
    ┌────▼─────┐      ┌──────▼──────┐
    │  Backend │      │   ESP32     │
    │ (FastAPI)│      │   (Main)    │
    └──────────┘      └──────┬──────┘
                             │
                    ┌────────┴────────┐
                    │                 │
              ┌─────▼─────┐    ┌─────▼─────┐
              │ ESP32-CAM │    │  Sensors  │
              │ (Material │    │ & Actuators│
              │ Detection)│    └───────────┘
              └───────────┘
```

## 🔧 Hardware Components

### Core Components
- **ESP32-WROOM-32** (Main Controller)
- **ESP32-CAM** (Material Detection)
- **HC-SR04 Ultrasonic Sensor** (Distance/Bin Level)
- **PIR Motion Sensor** (HC-SR501)
- **Load Cell 5-10kg + HX711** (Weight Measurement)
- **SG90/MG996R Servo Motors** (Lid Control)
- **LEDs** (Red/Green/Blue for Status)
- **Piezo Buzzer** (Audio Feedback)
- **Tactile Buttons** (Keypad)

### Power Management
- **18650 Li-ion Cells** (2x for 7.4V)
- **TP4056 Charging Module**
- **MT3608 Boost Converter**
- **AMS1117-3.3V LDO**

See `smart_bin_design/` for PCB schematics.

## 📁 Project Structure

```
smart-waste-bin/
├── smart_waste_bin_firmware_/     # ESP32 Firmware
│   ├── src/
│   │   ├── main.cpp               # Main ESP32 firmware
│   │   └── esp32cam_main.cpp      # ESP32-CAM firmware
│   └── platformio.ini             # PlatformIO configuration
├── backend/                        # FastAPI Backend
│   ├── main.py                    # Backend API server
│   ├── requirements.txt           # Python dependencies
│   └── README.md                  # Backend documentation
├── smart_bin_app/                 # Flutter Mobile App
│   ├── lib/
│   │   ├── main.dart             # App entry point
│   │   ├── models/               # Data models
│   │   ├── services/             # API services
│   │   ├── providers/            # State management
│   │   ├── screens/              # UI screens
│   │   └── widgets/              # Reusable widgets
│   └── pubspec.yaml              # Flutter dependencies
└── smart_bin_design/              # KiCad PCB Design
```

## 🚀 Getting Started

### 1. ESP32 Firmware Setup

1. Install PlatformIO:
   ```bash
   pip install platformio
   ```

2. Configure WiFi credentials in `smart_waste_bin_firmware_/src/main.cpp`:
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   const char* backend_url = "http://your-backend-url.com";
   ```

3. Upload firmware to ESP32:
   ```bash
   cd smart_waste_bin_firmware_
   pio run -e esp32dev -t upload
   ```

4. Upload ESP32-CAM firmware:
   ```bash
   pio run -e esp32cam -t upload
   ```

### 2. Backend Setup

1. Install Python dependencies:
   ```bash
   cd backend
   pip install -r requirements.txt
   ```

2. Run the backend server:
   ```bash
   python main.py
   ```
   
   Or with uvicorn:
   ```bash
   uvicorn main:app --host 0.0.0.0 --port 8000 --reload
   ```

3. The API will be available at `http://localhost:8000`
   - API Docs: `http://localhost:8000/docs`

### 3. Flutter App Setup

1. Install Flutter dependencies:
   ```bash
   cd smart_bin_app
   flutter pub get
   ```

2. Configure backend URL in the app settings (or default: `http://192.168.1.100:8000`)

3. Run the app:
   ```bash
   flutter run
   ```

## 📡 Communication Protocols

### ESP32 ↔ ESP32-CAM (CAN/TWAI)
- ESP32 sends: `DETECT_MATERIAL` request
- ESP32-CAM responds: `MATERIAL:ORGANIC` or `MATERIAL:NON_ORGANIC`

### ESP32 ↔ Backend (HTTP)
- POST `/api/bins/update` - Update bin status
- GET `/api/bins` - Get all bins status

### ESP32 ↔ Flutter App (WebSocket)
- Real-time bin status updates
- Commands: `open_organic`, `open_non_organic`, `close_*`, `get_status`

### ESP32-CAM ↔ Backend (HTTP)
- POST `/api/detect` - Material detection with image upload

## 🔌 Pin Configuration

### ESP32 Main Controller
- **PIR**: GPIO 2
- **Ultrasonic**: TRIG=GPIO 4, ECHO=GPIO 5
- **Servos**: Organic=GPIO 18, Non-Organic=GPIO 19
- **Load Cell**: DOUT=GPIO 16, SCK=GPIO 17
- **LEDs**: Red=GPIO 25, Green=GPIO 26, Blue=GPIO 27
- **Buzzer**: GPIO 14
- **Keypad**: Button1=GPIO 12, Button2=GPIO 13
- **CAN**: TX=GPIO 21, RX=GPIO 22

## 🎮 Usage

### Automatic Mode
1. Person approaches bin (PIR detects motion)
2. ESP32-CAM captures image and classifies material
3. Appropriate bin opens automatically
4. Bin closes after motion timeout

### Manual Mode (App/Keypad)
- Use Flutter app to open/close bins manually
- Use keypad buttons for quick access
- Maintenance mode available via app

### Bin Full Protection
- When bin reaches 90% capacity, it cannot be opened automatically
- Bins can still be opened via app or keypad for maintenance
- LED indicators show bin status (Red=Full, Yellow=High, Green=Normal)

## 🔐 Security & Configuration

- WiFi credentials stored in firmware 
- Backend API endpoints can be secured with authentication
- Bin IDs are unique identifiers for tracking

## 🧪 Testing

1. **Hardware Testing**: Test each sensor individually before full integration
2. **Material Detection**: Test with various waste types to improve classification
3. **Network Testing**: Verify WiFi connectivity and backend communication
4. **App Testing**: Test all app features on both WiFi and direct ESP32 connection

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## 📄 License

This project is open source and available for educational and commercial use.

## 🙏 Acknowledgments

- ESP32 platform
- FastAPI framework
- Flutter framework
- OpenCV for image processing
