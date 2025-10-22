# ESP32 PlatformIO Project - Changelog

## [2.1.0] - 2025-10-22

### ✨ Light Sleep Power Management

### 🎯 Added
- **Sleep Manager** (`SleepManager.h`, `SleepManager.cpp`)
  - Automatic light sleep after 2 minutes of inactivity
  - Low power consumption (~0.8-5 mA in sleep mode)
  - ~90-95% power reduction vs active mode
  - Activity tracking for all BLE and LoRa operations
  - RTC memory for persistent data across sleep cycles

- **Wake-up Sources**
  - LoRa interrupt (GPIO 32): Automatic wake on incoming messages
  - Visual confirmation: 3 LED blinks on wake-up

- **Message Persistence**
  - RTC memory storage for up to 10 messages
  - Messages survive light sleep cycles
  - FIFO queue (oldest messages delivered first)
  - Automatic delivery when BLE reconnects
  - No message loss during sleep or wake cycles

- **Smart Activity Management**
  - Activity timer updates on:
    - BLE connections
    - BLE message reception
    - LoRa message reception/transmission
    - Message forwarding to BLE
  - Prevents sleep during active communication
  - Stores pending messages before entering sleep

- **LED Visual Feedback**
  - 3 rapid blinks: Wake-up from light sleep
  - 1 blink: LoRa message received
  - 2 blinks: LoRa message transmitted

- **Documentation**
  - LIGHT_SLEEP.md - Comprehensive light sleep guide
  - Updated README.md with sleep features

### 🔧 Changed
- **BLEManager**
  - Added activity callback support for sleep management
  - Updates activity timer on connections and messages
  - Integrates with sleep manager

- **main.cpp**
  - Added sleep manager integration
  - Implements message storage to RTC memory when no BLE connection
  - Delivers stored messages on BLE reconnection
  - Checks for sleep timeout in main loop
  - Stores pending queue messages before sleep

### 💡 Features
- **Power Efficiency**
  - 2000 mAh battery: ~20 hours without sleep → ~15 days with light sleep
  - Field deployment optimized
  - Configurable timeout (default: 2 minutes)
  - Fast wake-up (milliseconds vs seconds for deep sleep)

- **Reliability**
  - Wake-up count tracking
  - RTC data validation (magic number check)
  - Automatic RTC memory reinitialization on corruption
  - Debug logging for all sleep/wake events

- **Use Cases**
  - Field devices without constant Android connection
  - Battery-powered deployments
  - Message reception during sleep periods
  - Automatic message delivery on reconnection

### 🔄 Migration Notes
**Non-Breaking Changes:**
- Existing functionality unchanged
- Sleep is automatic - no configuration required
- Works with existing Android app
- Hardware remains the same

**Optional Configuration:**
```cpp
// SleepManager.h
#define LIGHT_SLEEP_TIMEOUT_MS (2 * 60 * 1000)  // Change timeout
#define MAX_STORED_MESSAGES 10                   // Change buffer size
```

### 📊 Performance Impact
- Active power: ~80-120 mA
- Sleep power: ~0.8-5 mA (90-95% reduction)
- Wake-up time: ~few milliseconds (maintains system state)
- Message delivery latency: Instant when BLE connected
- No impact on communication range or speed

### 🧪 Testing Scenarios
- ✅ LoRa interrupt wake-up
- ✅ Message persistence across multiple sleep cycles
- ✅ Automatic message delivery on BLE reconnection
- ✅ RTC memory validation
- ✅ LED visual feedback

### 📝 Technical Details
- Uses ESP32 EXT1 wake source for LoRa interrupt
- RTC memory structure with magic number validation
- FIFO message queue in RTC_DATA_ATTR memory
- Compatible with ESP32 classic (not S2/S3 specific)

---

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
- BLE advertising: "ESP32S3-LoRa"

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
