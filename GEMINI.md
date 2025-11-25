# Android LoRa BLE Bridge

## Project Overview
This project creates a long-range communication system bridging **Bluetooth Low Energy (BLE)** and **LoRa** radio. It enables Android devices or Web browsers to send 6-bit packed text messages and GPS coordinates over LoRa (433 MHz) via an intermediary hardware bridge (ESP32 or nRF52).

**Key Features:**
*   **Range:** 3-15 km via LoRa (SX1262/SX1278).
*   **Architecture:** Android/Web App <-> BLE <-> Bridge Firmware <-> LoRa.
*   **Protocol:** Custom binary protocol (v3.0) with 6-bit text packing for efficiency.
*   **Hardware Support:** ESP32 (LilyGo T-Display S3, Heltec WiFi LoRa V3), nRF52 (Seeed XIAO nRF52840).

## Project Structure
*   **`android/`**: Android Application (Kotlin, Jetpack Compose, Clean Architecture).
*   **`firmware/`**: Unified C++/PlatformIO firmware for ESP32 and nRF52.
*   **`pwa/`**: Progressive Web App (TypeScript, Lit, Web Bluetooth).
*   **`protocol.md`**: Specification of the communication protocol.

## Quick Start

### 1. Android App
**Prerequisites:** JDK 11+, Android SDK.

```bash
cd android
# Build Debug APK
./gradlew assembleDebug
# Run Unit Tests
./gradlew test
# Install to connected device
./gradlew installDebug
```

### 2. Firmware (C++)
**Prerequisites:** PlatformIO Core (CLI).

```bash
cd firmware

# Build & Upload for ESP32 (LilyGo T-Display S3)
~/.platformio/penv/bin/pio run  run -e lilygo-t-display-s3 --target upload --target monitor

# Build & Upload for ESP32 (Heltec WiFi LoRa V3)
~/.platformio/penv/bin/pio run  run -e heltec-wifi-lora-v3 --target upload --target monitor

# Build & Upload for nRF52 (Seeed XIAO)
~/.platformio/penv/bin/pio run  run -e xiao_nrf52840 --target upload --target monitor
```

### 3. Progressive Web App (PWA)
**Prerequisites:** Node.js 18+.

```bash
cd pwa
npm install
# Start Development Server
npm run dev
# Build for Production
npm run build
```

## Architecture & Protocol

### System Flow
1.  **User Input:** Message typed in Android/Web App.
2.  **BLE Transfer:** App sends data to Bridge via BLE (Characteristic `0x5679`).
3.  **LoRa TX:** Bridge validates and transmits data via LoRa.
4.  **LoRa RX:** Receiving Bridge gets packet, validates, sends ACK.
5.  **BLE Notify:** Receiving Bridge forwards message to its connected App via BLE (Characteristic `0x5678`).

### Protocol v3.0
*   **Text Message (0x01):** `[Type][Seq][CharCount][PackedLen][PackedText][HasGPS][Lat][Lon]`
*   **Ack Message (0x02):** `[Type][Seq]`
*   **Encoding:** 6-bit packing (64-char set), GPS as `int32` (micro-degrees).

## Development Conventions
*   **Branching:** Follow standard git workflows.
*   **Commits:** Clear, descriptive messages.
*   **Code Style:**
    *   **Android:** Kotlin Clean Architecture (Domain/Data/Presentation).
    *   **Firmware:** Trait-based polymorphism for platform abstraction (`esp32/`, `nrf52/`).
    *   **PWA:** Modern TypeScript with Lit components.
*   **Testing:** Run relevant tests before committing (`./gradlew test` for Android, `vitest` for PWA).

## Key Configuration Files
*   `android/build.gradle.kts`: Android dependencies.
*   `firmware/platformio.ini`: Firmware build environments & pin definitions.
*   `firmware/include/Protocol.h`: Single source of truth for protocol structs.
*   `pwa/package.json`: Web app dependencies.
