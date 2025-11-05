# CAN Communication Guide

## Overview

The ESP32 and ESP32-CAM communicate via CAN (Controller Area Network) using ESP32's TWAI (Two-Wire Automotive Interface) protocol.

## Implementation Note

The current firmware uses simplified CAN message stubs. For production, you need to implement proper TWAI communication.

## Required Library

Add to `platformio.ini`:
```ini
lib_deps = 
    esp32-idf/twai
```

## CAN Message Format

### Request from ESP32 to ESP32-CAM
- **ID**: `0x100`
- **Data**: `"DETECT_MATERIAL"`
- **Purpose**: Request material detection

### Response from ESP32-CAM to ESP32
- **ID**: `0x200`
- **Data**: `"MATERIAL:ORGANIC"` or `"MATERIAL:NON_ORGANIC"` or `"MATERIAL:UNKNOWN"`
- **Purpose**: Send detection result

## Example TWAI Implementation

```cpp
#include "driver/twai.h"

void setupCAN() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        GPIO_NUM_21,  // TX
        GPIO_NUM_22,  // RX
        TWAI_MODE_NORMAL
    );
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());
}

void sendCANMessage(uint32_t id, String message) {
    twai_message_t tx_msg;
    tx_msg.identifier = id;
    tx_msg.data_length_code = message.length();
    for (int i = 0; i < message.length() && i < 8; i++) {
        tx_msg.data[i] = message[i];
    }
    tx_msg.flags = TWAI_MSG_FLAG_NONE;
    twai_transmit(&tx_msg, pdMS_TO_TICKS(1000));
}

bool receiveCANMessage(uint32_t* id, String* message) {
    twai_message_t rx_msg;
    if (twai_receive(&rx_msg, pdMS_TO_TICKS(100)) == ESP_OK) {
        *id = rx_msg.identifier;
        *message = "";
        for (int i = 0; i < rx_msg.data_length_code; i++) {
            *message += (char)rx_msg.data[i];
        }
        return true;
    }
    return false;
}
```

## Testing

1. Connect ESP32 and ESP32-CAM via CAN bus (with proper termination resistors)
2. Test message transmission/reception
3. Verify material detection communication

