# Detection flow, app display, and ESP-NOW

## 1. Does the app show when someone opens a bin and the material?

**Yes**, when the conditions below are met.

### When the app is connected to the device (Settings → ESP32 IP set)

- The main ESP32 sends **WebSocket** messages to the app: `DETECTION_EVENT` with `action` and `material`/`confidence`.
- The app shows a **banner** at the top of the home screen:
  - **"Analyzing material..."** (orange, with spinner) while the bin is analyzing.
  - **"Organic (95%) — Opening bin"** or **"Non-organic (95%) — Opening bin"** (green) when a bin is opening, with the detected material and confidence.
  - **"Closing bin"** (grey) when the lid is closing.
- The banner hides automatically after about 4 seconds.
- So you do see **when** a bin is opening and **what material** was detected (organic vs non-organic and confidence %).

### When the app is not connected to a device (simulation / backend only)

- The app polls **`/api/detections?limit=1`** every 3 seconds and can show the **last detection** from the backend (material/confidence).
- When you tap **Open** (Organic or Non-organic), the app shows the same **"… — Opening bin"** banner for that choice.
- So in simulation you still see “opening” and “material” for the last detection and for manual open.

---

## 2. Why ESP-NOW might not work between ESP32-CAM and main devkit

ESP-NOW works only when both radios are on the **same 2.4 GHz channel** and addressing is correct. Typical causes of failure:

### A. WiFi channel mismatch (most common)

- **Main devkit**: Connects to WiFi (e.g. "cybergenii") and gets a channel (e.g. 6). It adds the CAM as peer with `channel = 0` (“use my current channel”).
- **ESP32-CAM**: Also uses STA mode. If it **connects to the same AP**, it usually gets the same channel and ESP-NOW can work. If it **doesn’t connect** (wrong password, no network, timeout), it may stay on a default channel (e.g. 1). Then:
  - Main sends on channel 6, CAM listens on channel 1 → **CAM never receives** → no trigger, no pairing.
  - So the first packet (Main → CAM) never arrives, and the CAM never learns the Main’s MAC to send the result back.

**What to do:** Ensure both devices use the same channel. E.g.:

- Confirm both connect to the same WiFi and check `WiFi.channel()` in Serial on both.
- Or set a fixed channel on both (e.g. `WiFi.begin(ssid, password, channel)` and use that same channel in `esp_now_peer_info_t.peer_info.channel` when adding the peer).

### B. Wrong ESP32-CAM MAC on the main

- The **main** has a **hardcoded** CAM MAC: `A0:DD:6C:AF:09:30` (in `main.cpp`).
- If your ESP32-CAM has a different MAC (different board, reflashed module), the main is sending to the wrong address and the CAM never receives.

**What to do:** On the ESP32-CAM, print its MAC at boot (e.g. `Serial.println(WiFi.macAddress());`). Update `camMacAddress[]` in the main firmware to that MAC.

### C. CAM never receives the first packet (so it never “pairs”)

- The **CAM** does **not** know the main’s MAC at compile time. It learns it only when it **receives** an ESP-NOW command (e.g. "DETECT") from the main and then adds that sender as a peer.
- If the main’s ESP-NOW send **fails** (channel/MAC as above), the CAM never receives, so:
  - `mainControllerRegistered` stays false,
  - When the CAM later runs detection (e.g. via **GPIO trigger**), it tries to send the result to an uninitialized or wrong MAC and **ESP-NOW send fails**.

So ESP-NOW can appear to “not work” in both directions: Main→CAM (trigger) and CAM→Main (result), often due to that first Main→CAM packet never arriving.

### D. Power / antenna

- ESP32-CAM can be power-hungry. Brown-out or unstable power can cause WiFi/ESP-NOW to fail.
- Poor antenna or metal enclosure can reduce range and make links unreliable.

### Summary

- **Most likely:** Channel mismatch (CAM not on same channel as main) or wrong CAM MAC on the main.
- **Check:** Serial on both sides: main “ESP-NOW Send Status: ✅/❌”, CAM “ESP-NOW Command Received” (or not), and `WiFi.channel()` on both. Fix channel and MAC first; then re-test.

---

## 3. How does polling work when an object is detected?

High-level flow:

1. **Presence**
   - Main ESP32 sees someone (ultrasonic presence sensor).
   - State goes **IDLE → DETECTING_MOTION**.

2. **Trigger camera**
   - Next loop: **DETECTING_MOTION → ANALYZING_MATERIAL**.
   - Main sends **"DETECT"** to the CAM via **ESP-NOW** and toggles **GPIO** (backup).
   - Main broadcasts **"ANALYZING"** on WebSocket so the app can show “Analyzing material...”.

3. **Camera and backend**
   - CAM captures image (triggered by ESP-NOW or GPIO).
   - CAM sends image to backend **POST /api/detect** (HTTPS).
   - Backend runs the classifier (e.g. Gemini), returns ORGANIC or NON_ORGANIC/INORGANIC, and **queues an OPEN command** in the DB for bin `0x001` (organic) or `0x002` (non-organic).

4. **Polling (main ESP32)**
   - While in **ANALYZING_MATERIAL**, the main ESP32 polls **every 100 ms** with a **single** request:
     - `GET /api/bins/commands` (returns pending commands for both bins).
   - Uses **HTTPS** (WiFiClientSecure).
   - Backend returns any pending commands for that bin and **removes** them from the queue (poll-once semantics).

5. **When OPEN is in the response**
   - Main parses the JSON (e.g. `command: "OPEN"`, `params: { material, confidence }`).
   - Main sets:
     - `currentState = OPENING_BIN`
     - `selectedBin = 0x001` or `0x002`
     - `openFromBackendCommand = true` (so the bin opens even if level sensors say “full”).
   - Main broadcasts **"OPENING"** on WebSocket with material and confidence so the app can show “Organic (95%) — Opening bin” (or non-organic).

6. **Opening the bin**
   - On the next loop iteration, the state machine runs **OPENING_BIN**:
     - Calls `openBin(0)` or `openBin(1)` (servo).
     - State goes to **BIN_OPEN** (lid stays open for a while, then auto-closes).

So **polling** is: the main MCU repeatedly asks the backend “any OPEN/CLOSE for 0x001 / 0x002?” every 250 ms while it’s waiting for the result. As soon as the backend has classified the image and queued OPEN, the next poll returns that command and the main opens the bin and notifies the app. No ESP-NOW or UART is required for this path.

---

## 4. Where does detection-to-open time go? (Latency)

| Phase | Typical time | Notes |
|-------|----------------|------|
| Presence to trigger | 0–100 ms | Presence check every 100 ms. |
| Main to CAM trigger | 1–50 ms | ESP-NOW or GPIO. |
| **CAM capture + upload** | **0.5–2 s** | JPEG encode + HTTPS POST. |
| **Backend inference** | **1–4+ s** | Cloud API; usually the **largest** part. |
| Backend to queue | &lt; 50 ms | DB write. |
| Main polls and gets OPEN | 0–100 ms | Poll every 100 ms; single HTTPS GET. |
| Servo open | 50–200 ms | Physical. |

Most lag is **backend inference** and **image upload**. To reduce further: smaller image, faster model, or local inference.
