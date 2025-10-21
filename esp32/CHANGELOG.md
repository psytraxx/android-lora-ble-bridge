# ESP32 PlatformIO Project - Changelog

## [2.0.0] - 2025-10-21

### ✨ Complete Rewrite
Completely rewrote the ESP32 firmware to match ESP32-S3 Rust functionality, transforming it from a simple GPS sender to a full BLE-LoRa bridge.

### 🎯 Added
- **BLE Manager** (`BLEManager.h`, `BLEManager.cpp`)
  - GATT server implementation
  - Service UUID: 0x1234
  - TX Characteristic: 0x5678 (notifications to phone)
  - RX Characteristic: 0x5679 (writes from phone)
  - Connection state management
  - Auto-reconnect functionality
  - Message serialization/deserialization for BLE

- **Protocol Implementation** (`Protocol.h`, `Protocol.cpp`)
  - 6-bit text encoding for bandwidth optimization
  - Character set: Uppercase, numbers, punctuation (64 chars)
  - Automatic lowercase to uppercase conversion
  - pack_text() / unpack_text() with bit manipulation
  - Message types: Text (0x01), GPS (0x02), ACK (0x03)
  - Serialize/deserialize for all message types
  - Maximum text length: 50 characters

- **Message Handling**
  - Text messages with 6-bit encoding
  - GPS messages (lat/lon as i32)
  - ACK messages for acknowledgment
  - Automatic ACK generation for received messages

- **Documentation**
  - README.md - Comprehensive project documentation
  - IMPLEMENTATION.md - Detailed implementation notes
  - QUICKSTART.md - Quick start guide for users
  - protocol_test.cpp - Unit tests for protocol

### 🔧 Changed
- **platformio.ini**
  - Changed environment name: `esp32wroom-sender` → `esp32dev`
  - Added build flags for TX power and frequency configuration
  - Added monitor_speed = 115200
  - Updated configuration for long-range LoRa

- **lora_config.h**
  - Changed frequency: 433.775 MHz → 433.92 MHz
  - Changed bandwidth: 62.5 kHz → 125 kHz
  - Changed spreading factor: SF12 → SF10 (better balance)
  - Changed TX power: 20 dBm → 14 dBm (default, configurable)
  - Removed packet header definitions (not used in new protocol)
  - Added support for build flag configuration

- **LoRaManager.h**
  - Added `getRssi()` method
  - Added `getSnr()` method
  - Updated `getConfigurationString()` formatting
  - Improved documentation

- **main.cpp**
  - Complete rewrite from GPS sender to BLE-LoRa bridge
  - Removed GPS manager dependencies
  - Removed LED manager (can be added back if needed)
  - Added BLE event processing
  - Added LoRa continuous receive mode
  - Implemented bidirectional message routing
  - Added automatic ACK handling
  - Added RSSI/SNR reporting
  - Improved serial logging

### 🗑️ Removed
- **Old functionality**
  - GPS manager integration (was incomplete)
  - Packet header system (replaced with new protocol)
  - LED manager integration (can be re-added if needed)
  - Old packet format

### 🔄 Migration from 1.x

**Breaking Changes:**
- Complete protocol change - not compatible with old format
- Pin assignments remain the same
- Build commands unchanged
- New BLE service - requires updated Android app

**What to update:**
1. Use new Android app compatible with protocol 2.0
2. Rebuild and upload firmware
3. No hardware changes needed

### 📊 Performance
- Text encoding: 25% more efficient (50 chars: 42 bytes vs 56 bytes)
- Time on Air (SF10): ~550 ms for max message
- Range: 5-10 km typical, up to 15+ km ideal
- BLE advertising: "ESP32-LoRa"

### 🔧 Configuration Options
```ini
# platformio.ini
-DLORA_TX_POWER_DBM=14          # TX power (2-20 dBm)
-DLORA_TX_FREQUENCY=433920000   # Frequency in Hz
```

### 🐛 Bug Fixes
- Fixed LoRa configuration for optimal long-range performance
- Proper error handling in message serialization
- Correct BLE callback implementation
- Fixed continuous receive mode after TX

### 🧪 Testing
- Added protocol unit tests
- Verified interoperability with ESP32-S3 Rust firmware
- Tested with Android app
- Confirmed message encoding/decoding
- Validated ACK functionality

### 📝 Notes
- **Compatibility**: 100% protocol compatible with ESP32-S3 Rust firmware
- **Interoperability**: Can communicate with ESP32-S3 devices
- **Android App**: Use app version 2.0+ for compatibility
- **Architecture**: Loop-based (vs async in Rust version)

### 🎯 Feature Parity with ESP32-S3 Rust

| Feature | Status |
|---------|--------|
| BLE GATT Server | ✅ |
| LoRa Radio | ✅ |
| 6-bit Text Encoding | ✅ |
| Text Messages | ✅ |
| GPS Messages | ✅ |
| ACK Messages | ✅ |
| Automatic ACK | ✅ |
| RSSI/SNR | ✅ |
| Configurable TX Power | ✅ |
| Configurable Frequency | ✅ |
| Message Buffering | ⚠️ Simplified |
| Async Tasks | ⚠️ Loop-based |

### 👥 Credits
- Based on ESP32-S3 Rust firmware design
- Uses Sandeep Mistry's LoRa library
- Uses ESP32 Arduino BLE library

---

## [1.0.0] - Previous Version

### Initial Implementation
- Basic LoRa sender
- GPS integration (incomplete)
- LED manager
- Old packet format
- No BLE functionality

---

**Full Changelog**: [View on GitHub](../../CHANGELOG.md)
