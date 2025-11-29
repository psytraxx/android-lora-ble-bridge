# GitHub Copilot Instructions

## Project Overview

Long-range messaging system: Android/PWA ↔ BLE ↔ ESP32/nRF52 ↔ LoRa (3-10 km range)

**For complete documentation, see [README.md](../README.md)**

## Quick Reference

**Build Commands:**
```bash
# Firmware
cd firmware
~/.platformio/penv/bin/pio run -e lilygo-t-display-s3        # ESP32 SX1278
~/.platformio/penv/bin/pio run -e heltec-wifi-lora-v3       # ESP32 SX1262
~/.platformio/penv/bin/pio run -e xiao_nrf52840             # nRF52 SX1262

# Android
cd android
./gradlew assembleDebug installDebug
./gradlew test                                               # 74 unit tests

# PWA
cd pwa
npm run dev
```

**Protocol:**
- TextMessage (0x01): `[Type][Seq][CharCount][PackedLen][PackedText][HasGPS][Lat?][Lon?]`
- AckMessage (0x02): `[Type][Seq]`
- WakeUpMessage (0x03): `[Type]` (LoRa-only)
- 6-bit encoding: ` ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:;'"@#$%&*()[]{}=+/<>_`

**Deep Sleep (ESP32):**
- EXT0 (LoRa wake): Don't send WakeUp
- EXT1 (Button wake): Send WakeUp
- Prevents infinite WakeUp loops

**Key Files:**
- `firmware/src/unified_main.cpp` - Entry point
- `firmware/include/Protocol.h` - Protocol definition
- `android/.../LoRaProtocol.kt` - Android protocol
- `protocol.md` - Full protocol spec
- `README.md` - Complete documentation

**Architecture:**
- Multi-platform firmware (ESP32/nRF52) via traits
- Android: Kotlin + Compose + Clean Architecture
- BLE: Service 0x1234, TX 0x5678, RX 0x5679
- LoRa: 433.92 MHz, SF11, BW125 kHz, CR4/8, 20dBm

**Development:**
- Add message type: Update Protocol.h → Protocol.cpp → LoRaProtocol.kt → tests
- Change LoRa params: Edit `platformio.ini`, reflash all devices
- Debug: ESP32 logs via serial, Android via `adb logcat -s LoRaApp`
