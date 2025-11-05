# Smart Waste Bin - Components Reference

## Microcontrollers & Core Processing

### ESP32-WROOM-32
Main microcontroller that handles motion detection, sensor readings, servo control, WiFi communication, and system coordination.

### ESP32-CAM Module
Dedicated camera module that captures images for material classification and communicates detection results via CAN bus.

### USB-C or Micro-USB Breakout
Provides power input and programming interface for ESP32 development and firmware updates.

---

## Sensors

### HC-SR04 Ultrasonic Sensor
Measures distance to waste level using sound waves to determine bin fill percentage.

### JSN-SR04T Waterproof Ultrasonic Sensor (Alternative)
Waterproof version of HC-SR04 for outdoor installations that require protection from moisture.

### Load Cell (5kg or 10kg)
Measures the weight of waste in the bin using strain gauge technology.

### HX711 Load Cell Amplifier Module
Amplifies and converts analog load cell signals into digital data readable by the ESP32.

### PIR Motion Sensor (HC-SR501)
Detects human presence near the bin using passive infrared radiation to trigger automatic opening.

---

## Actuators & Feedback

### SG90 Micro Servo Motor
Lightweight servo motor for opening/closing bin lids weighing less than 500g.

### MG996R Servo Motor (Alternative)
Heavy-duty servo motor capable of handling bin lids up to 1.5kg with higher torque.

### 5mm LED (Red/Green/Blue)
Visual indicators that display bin status, fill level, and system state through color coding.

### Piezo Buzzer (Active 3-5V)
Provides audio feedback for bin operations, alerts, and warnings.

### Tactile Push Button (6×6mm)
Manual override buttons for opening bins when automatic system is unavailable or for maintenance.

---

## Power Management

### 18650 Li-ion Cell (3000-3500mAh)
Rechargeable battery cells providing 3.7V power source for portable operation.

### TP4056 Charging Module with Protection
Charges 18650 batteries safely with overcharge, over-discharge, and short-circuit protection.

### MT3608 Boost Converter (2A, Adjustable)
Steps up battery voltage to 5V for powering servos and other 5V components.

### XL6009 Boost Module (Alternative)
Alternative boost converter module with higher current capacity for power-hungry applications.

### AMS1117-3.3V LDO Regulator
Provides stable 3.3V power supply for ESP32 and other low-voltage components from higher voltage sources.

### Mini360 Buck Converter (Alternative)
DC-DC step-down converter alternative for efficiently reducing voltage to 3.3V.

---

## Circuit Components

### Capacitors

#### 0.1µF (100nF) Ceramic Capacitor (X7R, 50V) - 6 pcs
Decoupling capacitors that filter high-frequency noise from power supply lines to prevent interference.

#### 1µF Ceramic Capacitor (X7R, 50V) - 3 pcs
Filters noise and stabilizes voltage for analog sensor inputs.

#### 10µF Ceramic Capacitor (X5R/X7R, 25V) - 2 pcs
Provides output stability for voltage regulators and prevents oscillation.

#### 100µF Electrolytic Capacitor (Low-ESR, 25V) - 2 pcs
Bulk capacitance that smooths power supply fluctuations and handles current surges.

#### 470µF Electrolytic Capacitor (Low-ESR, 16V) - 1 pc
Optional motor surge protection capacitor that absorbs voltage spikes from servo operation.

### Resistors (1/4W, 5% tolerance)

#### 10kΩ Resistor - 5 pcs
Pull-up resistors for digital inputs like buttons and I2C communication lines.

#### 330Ω Resistor - 3 pcs
Current-limiting resistors that protect LEDs from excessive current draw.

#### 4.7kΩ Resistor - 2 pcs
I2C bus pull-up resistors for reliable communication with external modules.

### Other Passive Components

#### Ferrite Bead (600Ω @ 100MHz) - 2 pcs
Filters electromagnetic interference from power supply lines to reduce noise.

#### Schottky Diode 1N5819 (1A) - 2 pcs
Provides reverse polarity protection and flyback diode protection for servo motors.

---

## Connectors & Wiring

### JST-XH 2.54mm Connectors (2-pin, 3-pin, 4-pin)
Standardized connectors for reliable, removable connections between PCBs and components.

### DuPont Jumper Wires (M-M, M-F, F-F)
Flexible prototyping wires for connecting components during development and testing.

### 22 AWG Silicone Wire (Red/Black) - 2m each
Flexible power and ground wires with high-temperature resistance for permanent installations.

### Heat Shrink Tubing (Assorted Sizes)
Protective insulation that shrinks when heated to secure and insulate wire connections.

---

## PCB & Mounting

### Perforated PCB Board (5×7cm or 7×9cm)
Prototyping board for permanent component mounting and circuit assembly.

### M3 Standoffs and Screws
Hardware for mounting PCBs and creating secure mechanical connections in the enclosure.

### Hot Glue Gun or Double-Sided Foam Tape
Adhesive mounting options for securing sensors and components without drilling.

---

## Enclosure & Mechanical

### Servo Horn/Arm
Attaches to servo motor shaft to connect with bin lid mechanism for opening/closing action.

### M2/M3 Screws and Nuts
Hardware for assembling the physical structure and mounting components securely.

### Plastic Hinges (Small)
Allows bin lids to pivot smoothly when opened and closed by servo motors.

### Cable Glands or Grommets (6-10mm)
Provides waterproof cable entry points in the enclosure for outdoor installations.

---

## Optional Add-ons

### SIM800L GSM Module
Enables SMS alerts and remote monitoring without WiFi connection using cellular network.

### PN532 NFC/RFID Reader Module
Adds access control functionality for authorized users via NFC cards or RFID tags.

### 6V 1W Solar Panel + TP4056 Solar Charge Controller
Provides renewable energy charging for outdoor installations with sunlight exposure.

---

## Component Summary by Category

| Category | Component Count | Purpose |
|----------|----------------|---------|
| Microcontrollers | 2 | System control and processing |
| Sensors | 5 | Detection and measurement |
| Actuators | 5 | Movement and feedback |
| Power Management | 6 | Battery and power regulation |
| Passive Components | ~20 | Circuit protection and filtering |
| Connectors | Assorted | Interconnections |
| Mechanical | Assorted | Physical assembly |

---
