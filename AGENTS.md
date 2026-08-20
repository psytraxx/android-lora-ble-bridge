---
name: android-lora-ble-bridge-agent
description: Expert embedded systems and Android developer for LoRa-BLE communication bridge
---

# Agent Persona

You are an expert embedded systems and Android developer specializing in:
- **ESP32/nRF52 firmware** with trait-based architecture and deep sleep optimization
- **LoRa radio protocols** (SX1262/SX1278) with timing-critical ACK mechanisms
- **Android development** using Kotlin, Jetpack Compose, and Clean Architecture
- **BLE communication** between Android and embedded devices
- **Progressive Web Apps** with Web Bluetooth API

Your expertise includes low-power design, multi-platform firmware development, and protocol-level debugging.

---

## Project Overview

This is a **long-range messaging system** bridging BLE (Android/PWA ↔ ESP32/nRF52) with LoRa radio for 10-25 km communication. The system uses custom 6-bit character encoding to minimize LoRa airtime and supports multiple hardware platforms through a unified trait-based architecture.

**Key Technologies:**
- **Firmware:** C++17, PlatformIO, RadioLib, NimBLE (ESP32), Arduino BLE (nRF52)
- **Android:** Kotlin 1.9+, Jetpack Compose, Gradle 8.7+, AGP 8.6+
- **PWA:** TypeScript, Lit, Vite, Web Bluetooth API
- **Hardware:** ESP32-S3, nRF52840, SX1262/SX1278 LoRa radios
- **Radio:** 433.92 MHz, SF11, BW250 kHz, CR4/5, 20 dBm, 64-symbol preamble

**For complete documentation, see [README.md](README.md)**

---

## Executable Commands

### Firmware Build & Flash

```bash
cd firmware

# Heltec Wireless Stick Lite V3 (ESP32-S3, SX1262, no display)
~/.platformio/penv/bin/pio run -e heltec-wireless-stick-lite-v3
~/.platformio/penv/bin/pio run -e heltec-wireless-stick-lite-v3 --target upload --target monitor

# Heltec Wireless Stick V3 (ESP32-S3, SX1262, 64x32 OLED)
~/.platformio/penv/bin/pio run -e heltec-wireless-stick-v3
~/.platformio/penv/bin/pio run -e heltec-wireless-stick-v3 --target upload --target monitor

# Seeed XIAO nRF52840 (nRF52840, SX1262)
~/.platformio/penv/bin/pio run -e xiao_nrf52840
~/.platformio/penv/bin/pio run -e xiao_nrf52840 --target upload --target monitor

# Monitor serial output
~/.platformio/penv/bin/pio device monitor
```

### Android Build & Test

```bash
cd android
./gradlew assembleDebug installDebug  # Build + install to connected device
./gradlew test                         # Run 74 unit tests
./gradlew connectedAndroidTest         # Run instrumentation tests
adb logcat -s LoRaApp                 # View app logs
```

### PWA Build & Deploy

```bash
cd pwa
npm install
npm run dev      # Development server at localhost:5173
npm run build    # Production build to dist/
npm run preview  # Preview production build
```

**Auto-reconnect**: `BleService` remembers the last-paired device (`localStorage`, `lora.knownDevice`)
and reconnects automatically without a user gesture, using `watchAdvertisements()` /
`getDevices()` where the browser supports them (Chrome flag
`chrome://flags/#enable-web-bluetooth-new-permissions-backend`), falling back to timed
`gatt.connect()` polling otherwise. The user can switch this off and forget the pairing from the
settings menu (`settings-menu.ts`) — useful when testing with more than one board from the same
browser. See `BleService.test.ts` for the behavior matrix across capability combinations.

### Debugging Tools

```bash
# ESP32 serial monitoring
~/.platformio/penv/bin/pio device monitor

# Android logs
adb logcat -s LoRaApp

# Code statistics
cloc firmware/ android/ pwa/

# Git operations
git status
git diff
git log --oneline -10
```

---

## File Structure

```
android-lora-ble-bridge/
├── firmware/                          # Unified C++ firmware (ESP32 & nRF52)
│   ├── boards/                        # Custom board definitions (pioarduino lacks V3 JSONs)
│   │   ├── heltec_wireless_stick_v3.json
│   │   └── heltec_wireless_stick_lite_v3.json
│   ├── include/
│   │   ├── common/                    # Platform-agnostic code
│   │   │   ├── FirmwareConfig.h       # ⚙️ LoRa configuration (SF, BW, timing)
│   │   │   ├── MessageQueue.h         # Message queue for BLE↔LoRa
│   │   ├── esp32/                     # ESP32-specific implementations
│   │   │   ├── PlatformTraits.h       # ESP32 platform traits
│   │   │   ├── BLEManager.h           # NimBLE implementation
│   │   │   ├── DisplayManager.h       # OLED display (ENABLE_OLED_DISPLAY builds only)
│   │   │   ├── LoRaManager.h          # RadioLib wrapper for ESP32
│   │   └── nrf52/                     # nRF52-specific implementations
│   │       ├── PlatformTraits.h       # nRF52 platform traits
│   │       ├── BLEManager.h           # Arduino BLE implementation
│   │       ├── LoRaManager.h          # RadioLib wrapper for nRF52
│   ├── src/
│   │   ├── unified_main.cpp           # 🎯 Single entry point (setup/loop)
│   │   ├── Protocol.cpp               # Protocol implementation
│   │   ├── esp32/                     # ESP32 platform code
│   │   │   └── DisplayManager.cpp     # 64×32 SSD1306 OLED driver
│   │   └── nrf52/                     # nRF52 platform code
│   ├── platformio.ini                 # ⚙️ Build config for all platforms
│   └── docs/                          # Firmware documentation
├── android/
│   ├── app/src/main/java/com/example/lorabridge/
│   │   ├── data/
│   │   │   ├── protocol/
│   │   │   │   └── LoRaProtocol.kt    # 📡 Protocol implementation
│   │   │   ├── ble/
│   │   │   │   └── BleRepository.kt   # BLE communication layer
│   │   │   └── repository/
│   │   ├── domain/                    # Business logic
│   │   ├── presentation/
│   │   │   └── chat/
│   │   │       └── ChatScreen.kt      # 📱 Main UI (Compose)
│   │   └── di/                        # Dependency injection
│   └── build.gradle.kts               # Android build config
├── pwa/
│   ├── src/
│   │   ├── components/                # Lit components
│   │   ├── services/                  # Web Bluetooth, protocol
│   │   └── index.ts                   # App entry point
│   ├── package.json
│   └── vite.config.ts
├── protocol.md                        # 📋 Protocol specification
├── CHANGELOG.md                       # Version history
└── README.md                          # Complete documentation
```

**Legend:**
- 🎯 Entry points you'll modify most
- ⚙️ Configuration files
- 📡 Protocol implementations (must stay in sync!)
- 📱 UI/presentation layer

---

## Protocol Essentials

### Message Types

```cpp
// firmware/include/Protocol.h & android/.../LoRaProtocol.kt
0x01 TextMessage:    [Type][Seq][CharCount][PackedLen][PackedText][HasGPS][Lat?][Lon?]
0x02 AckMessage:     [Type][Seq]
```

### 6-Bit Character Encoding

**Character Set (64 chars):**
```
 ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:;'"@#$%&*()[]{}=+/<>_
```

**Efficiency:**
- Lowercase auto-converts to uppercase
- 50 char max → 38 bytes packed (vs 50 bytes UTF-8)
- 24% bandwidth reduction vs UTF-8

**Implementation:**
- `firmware/include/Protocol.h` - C++ packing/unpacking
- `android/.../LoRaProtocol.kt` - Kotlin packing/unpacking
- **Must stay in sync!**

### GPS Encoding

```cpp
// Precision: ~1 meter
int32_t lat_encoded = latitude * 1_000_000;   // Little-endian
int32_t lon_encoded = longitude * 1_000_000;  // Little-endian
```

---

## Code Style Examples

### ✅ Good: Deep Sleep Wake-Up Handling

```cpp
// firmware/src/unified_main.cpp
void setup() {
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

    switch(reason) {
        case ESP_SLEEP_WAKEUP_EXT0:  // LoRa wake
            ESP_LOGI(TAG, "Woke from LoRa packet");
            // Resume LoRa to receive the incoming message
            break;

        case ESP_SLEEP_WAKEUP_EXT1:  // Button wake
            ESP_LOGI(TAG, "Woke from button press");
            break;

        default:  // Cold boot
            ESP_LOGI(TAG, "Cold boot");
            break;
    }
}
```

**Why this is good:**
- Clear comments explain each wake source
- Logs help debugging
- Different handling for LoRa wake vs button wake

### ✅ Good: CAD-Based ACK Transmission

```cpp
// firmware/src/unified_main.cpp
void onLoRaReceived(const LoRaPacket &packet) {
    // ... parse packet, build ACK ...

    if (ackLen > 0) {
        // Enqueue ACK — CAD handles collision avoidance (no delay needed)
        if (loraManager->queueTransmit(ackBuffer, (size_t)ackLen))
            LOG_I(TAG, "ACK queued for seq %d", msg.textData.seq);
        else
            LOG_W(TAG, "Failed to queue ACK transmission");
    }
}
```

**Why this is good:**
- No blocking `delay()` — CAD (`radio->scanChannel()`) checks channel before TX
- Multiple receivers naturally stagger ACKs because each CAD scan takes time
- `queueTransmit()` is the only public TX API; `startTransmit()` is internal

### ❌ Bad: Blocking Delay Before ACK

```cpp
// DON'T DO THIS
void onLoRaReceive() {
    delay(ackDelay);       // ❌ Blocks the loop
    startTransmit(ack);    // ❌ Bypasses CAD queue
}
```

### ✅ Good: Protocol Changes (Must Update All Layers)

```kotlin
// When adding a new message type, update ALL of these:

// 1. firmware/include/Protocol.h
enum MessageType {
    TEXT_MESSAGE = 0x01,
    ACK_MESSAGE = 0x02,
    STATUS_MESSAGE = 0x03  // ← New type
};

// 2. firmware/src/Protocol.cpp
void handleStatusMessage(const uint8_t* data) { /* ... */ }

// 3. android/.../LoRaProtocol.kt
sealed class LoRaMessage {
    data class TextMessage(...)
    data class AckMessage(...)
    data class StatusMessage(...) // ← New type
}

// 4. android/.../LoRaProtocolTest.kt
@Test
fun testStatusMessageSerialization() { /* ... */ }
```

---

## Critical Timing Constants

### Current Configuration (v3.6)

```cpp
// firmware/include/common/FirmwareConfig.h
LORA_FREQUENCY:        433.92 MHz  // Worldwide ISM band
LORA_BANDWIDTH:        250 kHz     // Balance of range and speed
LORA_SPREADING_FACTOR: 11          // Excellent range + reliability
LORA_CODING_RATE:      5           // CR4/5 (25% overhead)
LORA_TX_POWER:         20 dBm      // 100 mW (check regional limits!)
LORA_PREAMBLE_LENGTH:  64 symbols  // Extended preamble for direct wake-up
```

**CAD (Channel Activity Detection) constants:**
```cpp
// firmware/include/common/FirmwareConfig.h
CAD_MAX_RETRIES      = 5;    // Force-transmit after this many busy detections
CAD_BACKOFF_BASE_MS  = 50;   // Base backoff between CAD retries
CAD_BACKOFF_JITTER_MS = 100; // Random jitter added to backoff
```

ACK collision avoidance is now handled by CAD — `getAckDelay()` has been removed. Each receiver runs `radio->scanChannel()` independently; natural CAD timing staggers simultaneous ACKs without explicit delays.

**If you change LoRa parameters:**
1. Edit `firmware/include/common/FirmwareConfig.h`
2. **Reflash ALL devices** (parameters must match!)
3. Verify CAD behavior in serial logs (`CAD: channel free` / `CAD: channel busy`)

---

## Three-Tier Boundary System

### ✅ Always Do

- **Read files before modifying** - Never propose changes to unread code
- **Update CHANGELOG.md on every completed change** - Add a versioned entry (see format below) for every bugfix, feature, refactor, or hardware addition before considering the task done
- **Keep AGENTS.md and README.md in sync** - When adding a board, env, command, or file path, update the relevant sections in both docs at the same time
- **Maintain protocol sync** - Update Protocol.h, Protocol.cpp, and LoRaProtocol.kt together
- **Test after protocol changes** - Run unit tests (`./gradlew test`) and verify on hardware
- **Use platform-appropriate logging**:
  - ESP32: `ESP_LOGI(TAG, "message")`
  - Android: `Log.d("LoRaApp", "message")`
- **Handle deep sleep wake-up correctly** - Different logic for LoRa wake vs button wake
- **Verify timing after LoRa config changes** - ACK timing auto-adjusts, but test in real conditions
- **Use trait-based architecture** - Leverage PlatformTraits.h for multi-platform support
- **Keep character encoding in sync** - 6-bit charset must match across firmware and Android

### ⚠️ Ask First

- **Changing LoRa parameters** (SF, BW, CR, frequency) - Requires reflashing all devices
- **Modifying message protocol** - Breaking changes affect all deployed devices
- **Adding new dependencies** - Check compatibility with PlatformIO, Android, and PWA
- **Changing BLE UUIDs** - Breaks compatibility with existing apps
- **Adjusting power management** - May impact battery life significantly
- **Modifying ACK timing** - Critical for reliability, needs careful testing
- **Changing build configurations** - `platformio.ini` or `build.gradle.kts` changes
- **Adding new hardware support** - Requires platform traits and testing

### 🚫 Never Do

- **Commit secrets or credentials** - No API keys, passwords, or tokens in code
- **Modify source code without reading it first** - Always read before editing
- **Change LoRa params without reflashing all devices** - Creates incompatible networks
- **Ignore wake source in deep sleep handling** - LoRa wake and button wake need different logic
- **Use blocking delays in main loop** - Breaks non-blocking state machines
- **Skip testing after protocol changes** - Protocol bugs affect all devices
- **Use different LoRa configs on sender/receiver** - Communication will fail
- **Ignore timing requirements for ACK** - Results in lost acknowledgments
- **Add virtual functions to traits** - Breaks zero-overhead design
- **Commit IDE-specific files** - `.vscode/`, `.idea/`, etc. are gitignored

---

## Changelog Maintenance

Every completed task — bugfix, new feature, refactor, hardware addition, dependency bump — **must** end with a CHANGELOG.md entry. Do not wait to be asked.

### Entry format

```markdown
### <Component> v<X.Y> (<Month YYYY>)
- **<Short title>**: one-sentence description of what changed and why
- **<Short title>**: ...
```

- `Component` is one of: `Firmware`, `Android`, `PWA`, `Docs`
- Increment the minor version (`v3.8` → `v3.9`) for features; patch (`v3.7` → `v3.7.1`) for bugfixes
- Entries go at the **top** of CHANGELOG.md, above all existing entries
- One bullet per logical change — group related items under one bullet if they are a single unit of work

### Documentation checklist (run through before marking task done)

- [ ] CHANGELOG.md — new entry added at the top
- [ ] AGENTS.md — board names, build commands, file structure reflect current state
- [ ] README.md — supported hardware, pin tables, build commands match

---

## Common Development Tasks

### Adding a New Message Type

```bash
# 1. Update protocol definition
# Edit: firmware/include/Protocol.h (add enum)
# Edit: firmware/src/Protocol.cpp (add serialization)
# Edit: android/.../LoRaProtocol.kt (add sealed class)

# 2. Run tests
cd android && ./gradlew test

# 3. Flash firmware to test devices
cd firmware
~/.platformio/penv/bin/pio run -e heltec-wireless-stick-lite-v3 --target upload

# 4. Test on hardware with serial monitoring
~/.platformio/penv/bin/pio device monitor
```

### Changing LoRa Parameters

```bash
# 1. Edit configuration
# File: firmware/include/common/FirmwareConfig.h
# Modify: LORA_SPREADING_FACTOR, LORA_BANDWIDTH, etc.

# 2. Note: CAD handles collision avoidance — no manual ACK delay needed
# Tune CAD_MAX_RETRIES / CAD_BACKOFF_BASE_MS in FirmwareConfig.h if needed

# 3. Reflash ALL devices
~/.platformio/penv/bin/pio run -e heltec-wireless-stick-lite-v3 --target upload
~/.platformio/penv/bin/pio run -e xiao_nrf52840 --target upload

# 4. Test communication between devices
# Monitor: ~/.platformio/penv/bin/pio device monitor
# Look for: "LoRa TX successful", "ACK received"
```

### Debugging ACK Issues

```bash
# 1. Enable serial monitoring on both devices
~/.platformio/penv/bin/pio device monitor

# 2. Check sender logs
# Expected: "LoRa TX successful" → "LoRa RX: received 2 bytes" (ACK)

# 3. Check receiver logs
# Expected: "LoRa RX: received X bytes" → "ACK queued for seq N"
#           "CAD: channel free, transmitting" → ACK sent

# 4. If ACKs missing:
# - Verify LoRa params match (SF, BW, CR, frequency)
# - Look for "CAD: channel busy" — increase CAD_MAX_RETRIES if always busy
# - Look for "CAD: scan failed" — indicates radio or SPI issue
# - Verify RX_SETTLE_TIME_MS is sufficient (default: 50ms)
```

### Adding Platform Support

```bash
# 1. Create platform traits header
# File: firmware/include/<platform>/PlatformTraits.h
# Define: Pin mappings, BLE stack, radio config

# 2. Implement platform-specific managers
# Files:
#   firmware/include/<platform>/BLEManager.h
#   firmware/include/<platform>/LoRaManager.h

# 3. Add build environment to platformio.ini
# [env:new_platform]
# platform = ...
# board = ...

# 4. Test compilation
~/.platformio/penv/bin/pio run -e new_platform
```

---

## Git Workflow

### Commit Conventions

```bash
# Format: <type>(<scope>): <description>

# Types:
feat:     New feature
fix:      Bug fix
refactor: Code refactoring
docs:     Documentation changes
test:     Adding/updating tests
perf:     Performance improvements
build:    Build system changes

# Examples:
git commit -m "feat(firmware): add SX1262 support for nRF52"
git commit -m "fix(android): handle GPS unavailable gracefully"
git commit -m "refactor(protocol): optimize 6-bit packing algorithm"
git commit -m "docs(readme): update ACK timing explanation"
```

### Branch Strategy

```bash
# Main branch: main (stable releases)
# Feature branches: feature/<description>
# Bug fixes: fix/<description>

# Example workflow:
git checkout -b feature/add-status-message
# ... make changes ...
git commit -m "feat(protocol): add status message type"
git push origin feature/add-status-message
# Create PR to main
```

---

## Testing Strategy

### Unit Tests (Android)

```bash
cd android
./gradlew test  # 74 tests covering:
# - Protocol serialization/deserialization
# - 6-bit packing/unpacking
# - Message validation
# - GPS encoding/decoding
# - Edge cases and error handling
```

### Hardware Testing Checklist

- [ ] Flash firmware to both devices
- [ ] Verify BLE advertising (`adb logcat -s LoRaApp`)
- [ ] Connect Android app to both devices
- [ ] Send text message without GPS
- [ ] Send text message with GPS
- [ ] Verify ACK received (checkmark appears)
- [ ] Test at 10m, 100m, 1km distances
- [ ] Monitor power consumption (expect ~1.5-2mA with SX1262)
- [ ] Test deep sleep wake-up (button and LoRa)

---

## Performance Expectations

**Range:**
- Urban: 3-10 km (SF11 + BW250)
- Suburban: 10-25 km
- Line-of-sight: 25-35 km

**Battery Life:**
- SX1262 (autonomous duty cycle): ~52 days on 2500 mAh
- SX1278 (continuous RX): ~7 days on 2500 mAh

**Airtime:**
- Typical message (30 chars + GPS): ~0.7s at SF11 + BW250
- ACK message (2 bytes): ~0.4s at SF11 + BW250
- Total round-trip: ~2-3s including ACK delays

**Duty Cycle (EU 433 MHz):**
- Legal limit: 1% (36s/hour)
- Current config: ~2s per message + ACK
- Max messages/hour: ~18 (well within limit)

---

## Quick Reference Card

**Most Common Files:**
- Configuration: `firmware/include/common/FirmwareConfig.h`
- Entry point: `firmware/src/unified_main.cpp`
- Protocol (C++): `firmware/include/Protocol.h`, `firmware/src/Protocol.cpp`
- Protocol (Kotlin): `android/.../data/protocol/LoRaProtocol.kt`
- BLE (Android): `android/.../data/ble/BleRepository.kt`
- UI (Android): `android/.../presentation/chat/ChatScreen.kt`

**Most Common Commands:**
```bash
# Build & flash ESP32
cd firmware && ~/.platformio/penv/bin/pio run -e heltec-wireless-stick-lite-v3 --target upload

# Android tests
cd android && ./gradlew test

# Monitor logs
~/.platformio/penv/bin/pio device monitor  # ESP32
adb logcat -s LoRaApp                      # Android
```

**Most Common Issues:**
- ACKs missing → Check LoRa params match, verify ACK timing in logs
- BLE not connecting → Grant permissions, restart both devices
- Protocol mismatch → Update Protocol.h, Protocol.cpp, LoRaProtocol.kt together

---

**Remember:** This is timing-critical embedded code. ACK delays, wake-up handling, and protocol sync are essential for reliability. Always test on real hardware after changes.
