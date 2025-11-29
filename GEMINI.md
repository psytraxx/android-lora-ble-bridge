# Gemini Instructions

## Project Overview

This is a long-range messaging system bridging BLE (Android ↔ ESP32/nRF52) with LoRa radio for 10-35 km communication. The system uses custom 6-bit character encoding to minimize LoRa airtime and supports multiple hardware platforms through a unified trait-based architecture. Optimized for dense urban environments with SF11+BW125+CR4/8 configuration.

**For complete documentation, see [README.md](README.md)**

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

## Protocol Essentials

**Message Types:**
- `0x01` TextMessage: `[Type][Seq][CharCount][PackedLen][PackedText][HasGPS][Lat?][Lon?]`
- `0x02` AckMessage: `[Type][Seq]`
- `0x03` WakeUpMessage: `[Type]` (LoRa-only, sent after button wake, NOT after LoRa wake)

**Character Set (6-bit):** ` ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:;'"@#$%&*()[]{}=+/<>_`
- Lowercase auto-converts to uppercase
- 50 char max → 38 bytes packed (vs 50 bytes UTF-8)
- See `firmware/include/Protocol.h` for implementation

**GPS Encoding:** `lat/lon × 1_000_000 → int32_t` (little-endian, ~1m precision)

## Deep Sleep Pattern (ESP32)

**Critical: Prevent WakeUp Message Loops**

```cpp
// In setup() after boot/wake:
esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

switch(reason) {
    case ESP_SLEEP_WAKEUP_EXT0:  // LoRa wake - DON'T send WakeUp
        break;
    case ESP_SLEEP_WAKEUP_EXT1:  // Button wake - SEND WakeUp
        sendWakeUpMessage();
        break;
    default:  // Cold boot
        sendWakeUpMessage();
        break;
}
```

## Common Development Tasks

**Changing LoRa Parameters:**
- Edit `firmware/include/common/FirmwareConfig.h`
- Current (v3.4): 433.92 MHz, BW125 kHz, SF11, CR4/8, 20dBm TX, 32-symbol preamble
- Optimized for: Maximum range in dense urban environments (~3.5x better than SF9+BW250)
- Timing constants (ACK_DELAY_MS, WAKEUP_TO_MESSAGE_DELAY_MS) auto-calculated from parameters
- Reflash ALL devices (must use same parameters)

**Debugging:**
- ESP32: `ESP_LOGI(TAG, ...)` → serial monitor
- Android: `adb logcat -s LoRaApp`
- Monitor: `~/.platformio/penv/bin/pio device monitor`

## Key Files

**Firmware:**
- `firmware/src/unified_main.cpp` - Single entry point for all platforms
- `firmware/include/Protocol.h` - Shared protocol
- `firmware/include/esp32/PlatformTraits.h` - ESP32 platform traits
- `firmware/include/nrf52/PlatformTraits.h` - nRF52 platform traits
- `firmware/platformio.ini` - Build configuration

**Android:**
- `android/app/src/main/java/com/example/lorabridge/data/protocol/LoRaProtocol.kt`
- `android/app/src/main/java/com/example/lorabridge/presentation/chat/ChatScreen.kt`
- `android/app/src/main/java/com/example/lorabridge/data/ble/BleRepository.kt`

**Documentation:**
- `README.md` - Complete project documentation
- `protocol.md` - Protocol specification
- `CHANGELOG.md` - Version history

## Project Conventions

- **Multi-platform firmware:** Single codebase for ESP32 and nRF52 via platform traits
- **Android:** Kotlin + Jetpack Compose + Clean Architecture
- **BLE Service UUID:** `0x1234` (TX: `0x5678`, RX: `0x5679`)
- **PlatformIO targets:** `lilygo-t-display-s3`, `heltec-wifi-lora-v3`, `xiao_nrf52840`
- **Radio support:** SX1262 (autonomous duty cycle), SX1278 (continuous RX)
- **Power:** 160 MHz CPU, autonomous duty cycle → weeks of battery life (SX1262)
