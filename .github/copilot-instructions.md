# GitHub Copilot Instructions

## Project Overview

This is a long-range messaging system bridging BLE (Android ↔ ESP32) with LoRa radio (ESP32 ↔ ESP32) for 5-15 km communication. The system uses custom 6-bit character encoding to minimize LoRa airtime and supports deep sleep for power optimization.

**Key Architecture:**
- **Android App** (Kotlin/Compose): BLE client, GPS source, message UI
- **ESP32 Firmware** (C++/Arduino): BLE server ↔ LoRa bridge with deep sleep
- **Shared Protocol** (C++): Binary message serialization, 6-bit text packing

## Critical Build Commands

```bash
# ESP32 firmware (use full PlatformIO path on macOS)
cd firmware
~/.platformio/penv/bin/pio run              # Build
~/.platformio/penv/bin/pio run --target upload --target monitor

# Android app
cd android
./gradlew assembleDebug installDebug        # Build + install
./gradlew test                              # 74 unit tests
```

## Protocol v3.0 Essentials

**Message Types:**
- `0x01` TextMessage: `[Type][Seq][CharCount][PackedLen][PackedText][HasGPS][Lat?][Lon?]`
- `0x02` AckMessage: `[Type][Seq]`
- `0x03` WakeUpMessage: `[Type]` (LoRa-only, sent after button wake, NOT after LoRa wake)

**Character Set (6-bit):** Space + A-Z + 0-9 + punctuation (64 chars: `.,!?-:;'"@#$%&*()[]{}=+/<>_`)
- Lowercase auto-converts to uppercase
- 50 char max → 38 bytes packed (vs 50 bytes UTF-8)
- See `shared/Protocol/Protocol.{h,cpp}` for pack/unpack implementation

**GPS Encoding:** `lat/lon × 1_000_000 → int32_t` (little-endian, ~1m precision)

## Deep Sleep & Wake-Up Pattern

**Critical: Prevent WakeUp Message Loops**

ESP32 has two wake sources:
- **EXT0 (LoRa DIO0 HIGH):** Remote device sent LoRa message → DO NOT send WakeUp
- **EXT1 (Button LOW):** User pressed button → SEND WakeUp message

```cpp
// In setup() after boot/wake:
esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

switch(reason) {
    case ESP_SLEEP_WAKEUP_EXT0:  // LoRa wake
        // DON'T send WakeUp (prevents infinite loop)
        break;
    case ESP_SLEEP_WAKEUP_EXT1:  // Button wake
        sendWakeUpMessage();  // Announce presence
        break;
    default:  // Cold boot
        sendWakeUpMessage();
        break;
}
```

**Why:** Device A's WakeUp shouldn't trigger Device B to send WakeUp back to A (loop).

**RTC Memory:** `RTC_DATA_ATTR int bootCount` persists across deep sleep for debugging.

## ESP32 Architecture Patterns

**State Machine:** `ApplicationController` (see `firmware/include/ApplicationController.h`)
- `DISCONNECTED_ADVERTISING` → `CONNECTED_ACTIVE` (via BLE events)
- Handles advertising timeout (30s), inactivity timeout (60s), deep sleep trigger
- Manages `MessageBuffer` (10 message queue when BLE disconnected)

**Message Flow:**
1. Android writes to BLE RX characteristic → `bleToLoraQueue`
2. `ApplicationController::update()` dequeues → `LoRaManager::send()`
3. LoRa RX callback → `loraToBleQueue`
4. `ApplicationController::update()` forwards to BLE or `MessageBuffer`

**Critical Timing:**
- **ACK delay:** 500ms before sending ACK (allows TX→RX mode switch)
- **RX settle:** 50ms after `startReceive()` (hardware stabilization)
- Location: `firmware/src/LoRaManager.cpp`

## Android Clean Architecture

**Layers:** (see `android/app/src/main/java/com/example/lorabridge/`)
- **Presentation:** `ChatScreen.kt`, `ChatViewModel.kt` (Compose UI + StateFlow)
- **Domain:** `Message.kt`, `ChatMessage.kt`, `BleConnectionState.kt`
- **Data:** `BleRepository.kt`, `MessageRepository.kt`, `LoRaProtocol.kt`

**BLE Config:**
- Device name: `"ESP32S3-LoRa"`
- Service UUID: `0x1234`
- TX (0x5678): ESP32 notifies Android
- RX (0x5679): Android writes to ESP32
- MTU: 512 bytes

**Testing:** Use `./gradlew test` to verify protocol changes. Key test files:
- `LoRaProtocolTest.kt`: Serialization round-trip, 6-bit pack/unpack
- `MessageRepositoryTest.kt`: Character validation, message handling

## Common Development Tasks

**Adding New Message Type:**
1. Update `shared/Protocol/Protocol.h` enum + struct
2. Add factory method in `Protocol.cpp` (e.g., `Message::createFoo()`)
3. Update `serialize()` and `deserialize()` switch cases
4. Mirror changes in `android/.../LoRaProtocol.kt`
5. Write unit tests (ESP32 + Android)
6. Update `protocol.md` specification
7. Increment version in comments

**Changing LoRa Parameters:**
- Edit `firmware/platformio.ini` (frequency, SF, BW, TX power)
- **Current settings:** 433.92 MHz, BW250 kHz, SF11, CR4/5, 20dBm TX, 512-symbol preamble
- Reflash ALL devices (must use same parameters for interoperability)
- Verify with serial monitor: `~/.platformio/penv/bin/pio device monitor`

**Debugging BLE/LoRa Flow:**
- ESP32: `ESP_LOGI(TAG, ...)` logs to serial monitor
- Android: `adb logcat -s LoRaApp` (or check Logcat in Android Studio)
- Look for: "BLE advertising", "Starting autonomous Rx Duty Cycle mode", "Transmission started"

## Power Optimization

- CPU: 160 MHz (not 240 MHz) via `CPU_FREQ_MHZ` in platformio.ini
- WiFi disabled in `setup()`: `esp_wifi_stop()` + `esp_wifi_deinit()`
- BT Classic released: `esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT)`
- **LoRa:** BW250 kHz @ SF11 for fast airtime (~0.8s) + good range
- **Autonomous Duty Cycle (SX1262):** ~1.5-2mA average (vs 12mA continuous RX)
- **512-Symbol Preamble:** Ensures detection by duty-cycled receivers (~2.5s)
- Deep sleep: ~52 days on 2500 mAh with SX1262 duty cycle (vs ~9 days continuous)

## Regulatory Compliance

**TX Power:** Currently 20 dBm (complies with US/AU, exceeds EU 2 dBm limit)
**Duty Cycle:** EU requires 1% (36s/hour). Calculate airtime at: https://www.loratools.nl

## Project-Specific Conventions

- **No backward compatibility:** Protocol v3.0 breaks v2.0 (separate TEXT/GPS → unified)
- **C++ in `shared/`:** Uses standard headers (`<cstdint>`, `<cstring>`) not Arduino-specific
- **Android package:** `com.example.lorabridge` (not `.lorabridge.app` or similar)
- **ESP32 targets:** `esp32dev` and `lilygo-t-display-s3` in `platformio.ini`

## Key Files Reference

- **Protocol spec:** `protocol.md`, `shared/Protocol/Protocol.{h,cpp}`
- **ESP32 entry:** `firmware/src/main.cpp` (setup/loop pattern)
- **State machine:** `firmware/include/ApplicationController.h`
- **Power mgmt:** `firmware/include/PowerManager.h`
- **Android protocol:** `android/app/src/main/java/com/example/lorabridge/data/protocol/LoRaProtocol.kt`
- **Android UI:** `android/app/src/main/java/com/example/lorabridge/presentation/chat/ChatScreen.kt`
