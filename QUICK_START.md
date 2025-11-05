# Quick Start Guide

## 🚀 Get Running in 5 Minutes

### 1. Backend (2 minutes)
```bash
cd backend
pip install -r requirements.txt
python main.py
```
Backend runs on `http://localhost:8000`

### 2. Configure ESP32 (2 minutes)
Edit `smart_waste_bin_firmware_/src/main.cpp`:
- Set WiFi SSID and password
- Set backend URL to your computer's IP

### 3. Upload Firmware (1 minute)
```bash
cd smart_waste_bin_firmware_
pio run -e esp32dev -t upload  # Main ESP32
pio run -e esp32cam -t upload  # ESP32-CAM
```

### 4. Run Flutter App
```bash
cd smart_bin_app
flutter pub get
flutter run
```

## 📱 App Features
- View bin status and levels
- Open/close bins manually
- Real-time updates via WebSocket
- Statistics dashboard
- Settings for backend/ESP32 IP

## 🔧 Key Configuration Points

### Bin IDs
- Organic: `0x001`
- Non-Organic: `0x002`

### Default Ports
- Backend: `8000`
- ESP32 HTTP: `80`
- ESP32 WebSocket: `81`

### Pin Reference
See `README.md` for complete pin configuration.

## 🐛 Quick Troubleshooting

**Can't connect?**
- Check WiFi credentials
- Verify IP addresses
- Check firewall settings

**Material detection not working?**
- Verify ESP32-CAM is powered
- Check CAN bus connection
- Test backend API at `/api/detect`

**Bins not opening?**
- Check servo power supply
- Verify GPIO pins
- Test servos manually

## 📚 Full Documentation
- `README.md` - Complete system overview
- `SETUP_GUIDE.md` - Detailed setup instructions
- `CAN_COMMUNICATION.md` - CAN protocol details

