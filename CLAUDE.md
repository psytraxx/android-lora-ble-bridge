# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A long-range messaging system enabling text messages (up to 50 characters) and GPS coordinates via 433 MHz LoRa using ESP32-S3 and Android devices. The system bridges BLE (short-range, high-bandwidth) with LoRa (long-range, low-bandwidth) for communication over 5-15 km range.

## Project Structure

- **esp32/** - ESP32/ESP32S3 firmware (C++/Arduino/PlatformIO)
- **esp32s3-debugger/** - LoRa receiver with display support (C++/Arduino/PlatformIO)
- **android/** - Android application (Java) with ViewBinding
- **protocol.md** - Binary protocol specification (v3.0 with 6-bit text encoding)

## Build Commands

### ESP32 Firmware (C++/Arduino - Legacy)

```bash
cd esp32

# Build
 ~/.platformio/penv/bin/pio run

# Flash
 ~/.platformio/penv/bin/pio run --target upload

# Monitor
 ~/.platformio/penv/bin/pio device monitor
```

### ESP32-S3 Debugger (C++/Arduino)

```bash
cd esp32s3-debugger

# Build and upload
 ~/.platformio/penv/bin/pio run --target upload

# Monitor
 ~/.platformio/penv/bin/pio device monitor
```

### Android App

```bash
cd android

# Build APK
./gradlew assembleDebug

# Install to device
./gradlew installDebug

# Run tests
./gradlew test                    # Unit tests (9 tests)
./gradlew connectedAndroidTest    # Instrumentation tests

# Clean
./gradlew clean
```

## High-Level Architecture

### Android App (Java)

**MVVM Architecture:**
- **MainActivity** - Chat UI with RecyclerView, handles user input
- **MessageViewModel** - Business logic, message sending, ACK tracking
- **BleManager** - BLE scanning, connection, GATT operations
- **GpsManager** - Location services (GPS + Network providers)
- **MessageAdapter** - RecyclerView adapter for chat display
- **Protocol** - Binary serialization (matches ESP32 protocol)

**BLE Configuration:**
- Scans for device name: "ESP32S3-LoRa"
- Service UUID: 0x1234
- TX characteristic (0x5678): Receives notifications
- RX characteristic (0x5679): Writes messages
- MTU negotiation: 512 bytes

**Key Features:**
- GPS coordinates clickable → opens Google Maps
- Auto-disconnect after 30 seconds of inactivity
- Message validation: 50 char max, 6-bit charset only

### Protocol Specification (v3.0)

**TextMessage Format:**
```
[Type:1] [Seq:1] [CharCount:1] [PackedLen:1] [PackedText:N] [HasGPS:1] [Lat:4] [Lon:4]
```
- Type: 0x01
- Max size: 5 + 38 (packed text) + 9 (GPS) = 52 bytes
- GPS is optional (hasGPS flag)

**AckMessage Format:**
```
[Type:1] [Seq:1]
```
- Type: 0x02
- Total: 2 bytes

**Character Set (64 chars for 6-bit encoding):**
- Space, A-Z (uppercase only), 0-9, punctuation: .,!?-:;'"@#$%&*()[]{}=+/<>_
- Lowercase automatically converted to uppercase
- Unsupported characters rejected

**Time on Air (SF11, BW31kHz):**
- Note: Actual values depend on SF11 + BW31kHz configuration
- Significantly longer than SF10+BW125kHz
- See protocol.md for detailed calculations

## Configuration

## Development Notes

### Critical Timing Parameters

**ACK Delay (ESP32-S3 Debugger):**
- Receiver waits 500ms before sending ACK
- Ensures sender has switched from TX to RX mode
- Location: `esp32s3-debugger/src/main.cpp` (delay after receiving message)
- Increase if ACKs are lost (try 1000ms)

**RX Mode Settle Time:**
- 50ms delay after switching to RX mode
- Allows SX1278 radio hardware to stabilize
- Location: `esp32/src/main.cpp` after `startReceiveMode()`

### Protocol Evolution

**Current: v3.0 (Oct 2025)**
- Unified text + GPS in single message
- Optional GPS (hasGps flag)
- Only 2 message types: TEXT (0x01), ACK (0x02)

**Previous: v2.0**
- Separate TextMessage and GpsMessage
- Required 100ms delay between messages
- 3 message types: TEXT (0x01), GPS (0x02), ACK (0x03)

**Not backward compatible.** All devices must use same protocol version.

### Power Optimization

**ESP32-S3:**
- CPU clock: 160 MHz (not 240 MHz max) for power savings
- SF11 LoRa: Long time-on-air (longer than SF10) = low duty cycle
- WiFi disabled during setup
- BLE advertising only when disconnected

**Battery Life:**
- 70-100 hours on 2500 mAh battery (typical usage)
- 40-50% power savings vs. default configuration

### Common Development Tasks

**Adding New Message Types:**
2. Update Android `Protocol.java` to match
3. Write unit tests for serialization (both platforms)
4. Update `protocol.md` specification
5. Increment protocol version number

**Changing LoRa Parameters:**
- Edit `shared/LoRaManager/lora_config.h`
- Rebuild firmware and reflash all devices
- Verify Time on Air in logs
- Test range with new parameters

**Debugging Message Flow:**
- ESP32: Use `espflash monitor` to see BLE/LoRa events
- Android: Use `adb logcat -s LoRaApp`
- Look for: "BLE advertising", "LoRa TX successful", "LoRa RX: received X bytes"

## Testing

**Android Tests (9 unit tests):**
- TextMessage (with/without GPS), AckMessage serialization
- 6-bit character packing/unpacking
- Round-trip encoding/decoding
- Character validation
- Run: `cd android && ./gradlew test`

## Regional Regulations


**TX Power Limits:**
- EU/Switzerland 433 MHz: 2 dBm max
- US 433 MHz: 17 dBm max
- Australia 433 MHz: 14 dBm max
- **Current firmware:** 20 dBm (complies with US/Australia, exceeds EU limit)


**Duty Cycle (EU/Switzerland: 1%):**
- 36 seconds per hour transmission time
- Note: Message airtime depends on SF11+BW31kHz (longer than previous estimates)
- Calculate using https://www.loratools.nl/#/airtime

Use proper antenna for frequency (433 MHz = ~17 cm quarter-wave).
