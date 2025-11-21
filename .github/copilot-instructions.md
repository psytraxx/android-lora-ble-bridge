# GitHub Copilot Instructions

## Project Overview

This is a long-range messaging system bridging BLE (Android ↔ ESP32/nRF52) with LoRa radio for 3-10 km communication. The system uses custom 6-bit character encoding to minimize LoRa airtime and supports multiple hardware platforms through a unified trait-based architecture.

**Key Architecture:**
- **Android App** (Kotlin/Compose): BLE client, GPS source, message UI
- **Unified Firmware** (C++/Arduino): Multi-platform support (ESP32, nRF52)
  - ESP32: FreeRTOS tasks, NimBLE, deep sleep capable
  - nRF52: Loop-based, Arduino BLE, low power modes
- **Shared Protocol** (C++): Binary message serialization, 6-bit text packing
- **PWA** (TypeScript): Web Bluetooth support for browser access

## Critical Build Commands

```bash
# Firmware (unified multi-platform)
cd firmware

# ESP32 (LilyGo T-Display S3 with SX1278)
~/.platformio/penv/bin/pio run -e lilygo-t-display-s3
~/.platformio/penv/bin/pio run -e lilygo-t-display-s3 --target upload --target monitor

# ESP32 (Heltec WiFi LoRa V3 with SX1262)
~/.platformio/penv/bin/pio run -e heltec-wifi-lora-v3
~/.platformio/penv/bin/pio run -e heltec-wifi-lora-v3 --target upload --target monitor

# nRF52 (Seeed XIAO nRF52840 with SX1262)
~/.platformio/penv/bin/pio run -e xiao_nrf52840
~/.platformio/penv/bin/pio run -e xiao_nrf52840 --target upload --target monitor

# Android app
cd android
./gradlew assembleDebug installDebug        # Build + install
./gradlew test                              # 74 unit tests

# PWA
cd pwa
npm install
npm run dev                                  # Development server
npm run build                                # Production build
```

## Protocol v3.0 Essentials

**Message Types:**
- `0x01` TextMessage: `[Type][Seq][CharCount][PackedLen][PackedText][HasGPS][Lat?][Lon?]`
- `0x02` AckMessage: `[Type][Seq]`
- `0x03` WakeUpMessage: `[Type]` (LoRa-only, sent after button wake, NOT after LoRa wake)

**Character Set (6-bit):** Space + A-Z + 0-9 + punctuation (64 chars: `.,!?-:;'"@#$%&*()[]{}=+/<>_`)
- Lowercase auto-converts to uppercase
- 50 char max → 38 bytes packed (vs 50 bytes UTF-8)
- See `firmware/include/Protocol.h` and `firmware/src/Protocol.cpp` for pack/unpack implementation

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

## Firmware Architecture

**Unified Multi-Platform Design:**
- **Single `unified_main.cpp`** - One entry point for all platforms
- **Platform traits** - Compile-time polymorphism (no virtual functions)
- **Zero runtime overhead** - All platform selection done at compile-time

**Platform-Specific Implementations:**

**ESP32 (FreeRTOS Tasks):**
- `BLE Task` (priority 3): NimBLE, message forwarding
- `LoRa Task` (priority 4): RadioLib, ISR handling
- `Power Task` (priority 2): Timeout monitoring, deep sleep
- Files: `firmware/include/esp32/`, `firmware/src/esp32/`

**nRF52 (Loop-based):**
- Non-blocking state machines
- Arduino BLE stack
- Event-driven polling
- Files: `firmware/include/nrf52/`, `firmware/src/nrf52/`

**State Machine:** `ApplicationController`
- `DISCONNECTED_ADVERTISING` → `CONNECTED_ACTIVE` (via BLE events)
- Handles advertising timeout (30s), inactivity timeout (60s)
- Manages `MessageBuffer` (10 message queue when BLE disconnected)

**Message Flow:**
1. Android writes to BLE RX characteristic → `bleToLoraQueue`
2. Platform-specific handling:
   - ESP32: LoRa Task dequeues → `LoRaManager::send()`
   - nRF52: Loop processes queue → `LoRaManager::send()`
3. LoRa RX callback → `loraToBleQueue`
4. Platform forwards to BLE or `MessageBuffer`

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
1. Update `firmware/include/Protocol.h` enum + struct
2. Add factory method in `Protocol.cpp` (e.g., `Message::createFoo()`)
3. Update `serialize()` and `deserialize()` switch cases
4. Mirror changes in `android/.../LoRaProtocol.kt`
5. Write unit tests (ESP32 + Android)
6. Update `protocol.md` specification
7. Increment version in comments

**Changing LoRa Parameters:**
- Edit `firmware/platformio.ini` (frequency, SF, BW, TX power)
- **Current settings:** 433.92 MHz, BW250 kHz, SF9, CR4/5, 20dBm TX, 8-symbol preamble
- **For longer range:** Change `-DLORA_SPREADING_FACTOR=9` to `11` (reduces speed, increases range 50%)
- Reflash ALL devices (must use same parameters for interoperability)
- Verify with serial monitor: `~/.platformio/penv/bin/pio device monitor`

**Debugging BLE/LoRa Flow:**
- ESP32: `ESP_LOGI(TAG, ...)` logs to serial monitor
- Android: `adb logcat -s LoRaApp` (or check Logcat in Android Studio)
- Look for: "BLE advertising", "Starting autonomous Rx Duty Cycle mode", "Transmission started"

## Power Optimization

**ESP32:**
- CPU: 160 MHz (not 240 MHz) via `CPU_FREQ_MHZ` in platformio.ini
- WiFi disabled in `setup()`: `esp_wifi_stop()` + `esp_wifi_deinit()`
- BT Classic released: `esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT)`
- **LoRa:** BW250 kHz @ SF9 for fast airtime (~0.3-0.6s) + good range
- **Autonomous Duty Cycle (SX1262):** ~1.5-2mA average (vs 12-15mA continuous RX on SX1278)
- Deep sleep: Multiple weeks on 2500 mAh with SX1262

**nRF52:**
- SoftDevice power modes for BLE
- WFI (Wait For Interrupt) during idle
- Lower active power consumption than ESP32
- SX1262 autonomous duty cycle for ultra-low power

## Regulatory Compliance

**TX Power:** Currently 20 dBm (complies with US/AU, exceeds EU 2 dBm limit)
**Duty Cycle:** EU requires 1% (36s/hour). Calculate airtime at: https://www.loratools.nl

## Project-Specific Conventions

- **Multi-platform firmware:** Single codebase supports ESP32 and nRF52 via platform traits
- **Protocol C++:** Uses standard headers (`<cstdint>`, `<cstring>`) not Arduino-specific for portability
- **Android package:** `com.example.lorabridge` (Kotlin + Jetpack Compose)
- **PlatformIO targets:** `lilygo-t-display-s3`, `heltec-wifi-lora-v3`, `xiao_nrf52840`
- **Radio support:** SX1262 (autonomous duty cycle), SX1278 (continuous RX)
- **Compile-time platform selection:** Platform-specific code isolated to `esp32/` and `nrf52/` directories

## Key Files Reference

**Firmware:**
- **Unified entry:** `firmware/src/unified_main.cpp` (single entry point for all platforms)
- **Protocol:** `firmware/include/Protocol.h`, `firmware/src/Protocol.cpp` (shared across platforms)
- **Platform traits:**
  - `firmware/include/esp32/PlatformTraits.h`
  - `firmware/include/nrf52/PlatformTraits.h`
- **ESP32 implementations:** `firmware/include/esp32/`, `firmware/src/esp32/`
- **nRF52 implementations:** `firmware/include/nrf52/`, `firmware/src/nrf52/`
- **Build config:** `firmware/platformio.ini` (multi-environment setup)

**Android:**
- **Protocol:** `android/app/src/main/java/com/example/lorabridge/data/protocol/LoRaProtocol.kt`
- **UI:** `android/app/src/main/java/com/example/lorabridge/presentation/chat/ChatScreen.kt`
- **BLE:** `android/app/src/main/java/com/example/lorabridge/data/ble/BleRepository.kt`

**Documentation:**
- **Protocol spec:** `protocol.md`
- **Firmware architecture:** `firmware/ARCHITECTURE.md` (v3.0 - unified multi-platform)
- **Android docs:** `android/README.md`, `android/docs/`
- **Changelog:** `CHANGELOG.md`
