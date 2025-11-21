# LoRa Bridge - Modern Android App

A long-range messaging system enabling text messages (up to 50 characters) and GPS coordinates via 433 MHz LoRa using ESP32-S3 and Android devices.

**Modern Re-implementation:** Kotlin + Jetpack Compose + Clean Architecture

---

## 🎯 Features

- ✅ **BLE Communication** - Connect to ESP32S3-LoRa via Bluetooth Low Energy
- ✅ **LoRa Messaging** - Send/receive messages over 5-15 km range
- ✅ **GPS Integration** - Attach location coordinates to messages
- ✅ **6-bit Encoding** - Efficient text packing (24% bandwidth savings)
- ✅ **ACK Tracking** - Delivery confirmation with visual indicators
- ✅ **Auto-Reconnect** - Queue messages and reconnect automatically
- ✅ **Material 3 UI** - Modern Jetpack Compose interface
- ✅ **Google Maps Integration** - Tap GPS messages to open location

---

## 📱 Screenshots

```
┌─────────────────────┐
│  LoRa Chat          │
├─────────────────────┤
│ ✅ Ready to send!   │
│ 47.123, 8.987 (GPS) │
├─────────────────────┤
│                     │
│  ┌──────────────┐   │  ← Received
│  │ HELLO WORLD  │   │
│  │ 14:23     📍 │   │
│  └──────────────┘   │
│                     │
│        ┌─────────┐  │  ← Sent
│        │ TEST ✓  │  │
│        │ 14:25   │  │
│        └─────────┘  │
│                     │
├─────────────────────┤
│ 11/50 chars (21 B)  │
│ ┌────────────┐ [>]  │
│ │ Type msg...│      │
│ └────────────┘      │
└─────────────────────┘
```

---

## 🏗️ Architecture

### Clean Architecture Layers

```
┌────────────────────────────────────────┐
│         Presentation Layer             │
│  (Jetpack Compose + ViewModels)        │
│  • ChatScreen, MessageBubble           │
│  • ChatViewModel, ChatUiState          │
└────────────────────────────────────────┘
                   ↓
┌────────────────────────────────────────┐
│           Domain Layer                 │
│        (Business Logic)                │
│  • Models: Message, ChatMessage        │
│  • States: BleConnectionState          │
└────────────────────────────────────────┘
                   ↓
┌────────────────────────────────────────┐
│            Data Layer                  │
│   (Repositories + Protocol)            │
│  • BleRepository, LocationRepository   │
│  • MessageRepository, LoRaProtocol     │
└────────────────────────────────────────┘
```

### Technologies
- **Language:** Kotlin 2.0.21
- **UI:** Jetpack Compose + Material 3
- **Architecture:** MVVM + Clean Architecture
- **DI:** Hilt 2.51.1
- **Async:** Kotlin Coroutines + StateFlow
- **Location:** Google Play Services Location
- **Min SDK:** 29 (Android 10)
- **Target SDK:** 36

---

## 📦 Protocol v3.0

### Message Format

**TextMessage:**
```
[Type:1] [Seq:1] [CharCount:1] [PackedLen:1] [PackedText:N] [HasGPS:1] [Lat:4] [Lon:4]
```
- Max size: 52 bytes
- GPS optional

**AckMessage:**
```
[Type:1] [Seq:1]
```
- Size: 2 bytes

### 6-Bit Character Encoding
**Charset (64 chars):**
```
 ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:;'"@#$%&*()[]{}=+/<>_
```

**Efficiency:**
- 50 chars × 6 bits = 300 bits = 38 bytes
- UTF-8: 50 chars × 8 bits = 50 bytes
- **Savings:** 24% bandwidth reduction

---

## 🚀 Build & Run

### Prerequisites
- JDK 11 or higher (JDK 17 or 21 recommended)
- Android SDK 36
- Android Studio Ladybug or newer

### Build
```bash
cd lorabridge
./gradlew assembleDebug
```

**Output:** `app/build/outputs/apk/debug/app-debug.apk`

### Install
```bash
./gradlew installDebug
```

### Run Tests
```bash
./gradlew test
```

**Coverage:** 74 unit tests across multiple test files

---

## 📊 Project Status

### Implementation: 92%
- ✅ **23/25 use cases** fully implemented
- ⚠️ **1/25 use cases** partially implemented (UC-7.3)
- ✅ **43 unit tests** passing

### Test Coverage: 24%
- ✅ Protocol serialization (100%)
- ✅ Domain models (100%)
- ✅ Message repository (100%)
- ⚠️ ViewModel (needs tests)
- ⚠️ UI (needs instrumentation tests)

---

## 📚 Documentation

### Core Docs
- **[USE_CASES.md](docs/USE_CASES.md)** - 25 use cases documented
- **[STATE_DIAGRAM.md](docs/STATE_DIAGRAM.md)** - 6 Mermaid diagrams
- **[IMPLEMENTATION_SUMMARY.md](docs/IMPLEMENTATION_SUMMARY.md)** - Architecture overview

### Tracking & Testing
- **[USE_CASE_IMPLEMENTATION_MAP.md](docs/USE_CASE_IMPLEMENTATION_MAP.md)** - Code → Use case mapping
- **[USE_CASE_TRACKING_SUMMARY.md](docs/USE_CASE_TRACKING_SUMMARY.md)** - Traceability guide
- **[TESTING_SUMMARY.md](docs/TESTING_SUMMARY.md)** - Test coverage & guidelines

### Changelog
- **[CHANGELOG.md](docs/CHANGELOG.md)** - Version history

---

## 🔑 Key Use Cases

### UC-1.3: Auto-Reconnect on Disconnection
When disconnected, sending a message will:
1. Queue the message
2. Start BLE scan automatically
3. Send message when reconnected
4. Display "Connected! Sending message..." toast

### UC-3.1: Send Text Message with GPS
1. Validate text (max 50 chars, supported charset)
2. Request fresh GPS update
3. Create message with optional GPS
4. Send via BLE → LoRa
5. Show PENDING status (⏱)
6. Wait for ACK (5s timeout)
7. Update to DELIVERED (✓) on ACK

### UC-4.2: Open Location in Maps
Tap any message with GPS coordinates to:
1. Open Google Maps app (if installed)
2. Or fallback to browser
3. Show marker at coordinates

---

## 📝 File Structure

```
lorabridge/
├── app/src/main/java/com/example/lorabridge/
│   ├── data/
│   │   ├── ble/
│   │   │   ├── BleConstants.kt
│   │   │   └── BleRepository.kt          ← UC-1.1, UC-1.2, UC-1.4
│   │   ├── location/
│   │   │   └── LocationRepository.kt     ← UC-2.1, UC-2.2
│   │   ├── protocol/
│   │   │   └── LoRaProtocol.kt           ← UC-5.1, UC-5.2, UC-5.3
│   │   └── repository/
│   │       └── MessageRepository.kt      ← UC-6.1, UC-6.2
│   ├── domain/
│   │   └── model/
│   │       ├── BleConnectionState.kt
│   │       ├── ChatMessage.kt
│   │       ├── Location.kt
│   │       ├── Message.kt
│   │       └── Result.kt
│   ├── presentation/
│   │   ├── chat/
│   │   │   ├── ChatScreen.kt             ← UC-7.1, UC-7.2, UC-6.3
│   │   │   └── ChatViewModel.kt          ← UC-1.3, UC-2.3, UC-3.1-3.4
│   │   └── components/
│   │       └── MessageBubble.kt          ← UC-6.1, UC-6.2, UC-4.2
│   ├── di/
│   │   └── AppModule.kt
│   ├── LoRaBridgeApplication.kt
│   └── MainActivity.kt                    ← UC-8.2
├── app/src/test/
│   ├── data/protocol/
│   │   └── LoRaProtocolTest.kt           ← 13 tests
│   ├── data/repository/
│   │   └── MessageRepositoryTest.kt      ← 11 tests
│   └── domain/model/
│       ├── ChatMessageTest.kt            ← 10 tests
│       └── LocationDataTest.kt           ← 9 tests
└── docs/
    ├── USE_CASES.md
    ├── STATE_DIAGRAM.md
    ├── IMPLEMENTATION_SUMMARY.md
    ├── USE_CASE_IMPLEMENTATION_MAP.md
    ├── USE_CASE_TRACKING_SUMMARY.md
    ├── TESTING_SUMMARY.md
    └── CHANGELOG.md
```

---

## 🎨 Improvements Over Java Version

### Code Quality
1. **50% less code** - Kotlin's conciseness
2. **Type safety** - Sealed classes for states
3. **Null safety** - Built into language
4. **No memory leaks** - Structured coroutines
5. **Reactive UI** - StateFlow + Compose

### Architecture
1. **Clean architecture** - Domain/Data/Presentation layers
2. **Dependency injection** - Hilt
3. **Testability** - 43 unit tests
4. **Traceability** - UC references in code

### Features
1. **Auto-reconnect** - UC-1.3 fully implemented
2. **Better error handling** - Result sealed class
3. **Modern UI** - Material 3 + Compose
4. **Better UX** - Automatic keyboard dismiss

---

## 🛠️ Hardware Requirements

### Firmware (ESP32/nRF52)
- **Firmware:** C++/Arduino/PlatformIO (Unified multi-platform)
- **Location:** `../firmware/`
- **Supported Platforms:**
  - ESP32: LilyGo T-Display S3, Heltec WiFi LoRa V3
  - nRF52: Seeed XIAO nRF52840
- **BLE Service UUID:** 0x1234
- **TX Characteristic:** 0x5678 (notifications)
- **RX Characteristic:** 0x5679 (writes)

### LoRa Configuration
- **Frequency:** 433.92 MHz
- **Bandwidth:** 250 kHz (fast airtime)
- **Spreading Factor:** 9 (balanced range/speed)
- **Coding Rate:** 4/5
- **TX Power:** 20 dBm (check regional limits)
- **Range:** 3-10 km typical (SF9), up to 15 km (SF11 option)
- **Radios:** SX1262 (autonomous duty cycle), SX1278 (continuous RX)

---

## ⚠️ Regulatory Compliance

### TX Power Limits
- **EU/Switzerland 433 MHz:** 2 dBm max (current: 20 dBm exceeds)
- **US 433 MHz:** 17 dBm max ✅
- **Australia 433 MHz:** 14 dBm max ✅

### Duty Cycle (EU: 1%)
- Max 36 seconds transmission per hour
- Current message: ~1-2 seconds airtime
- See: https://www.loratools.nl/#/airtime

**Use proper 433 MHz antenna (~17 cm quarter-wave)**

---

## 🐛 Known Limitations

1. **Java Version:** Use JDK 17 or 21 (JDK 25 has compatibility issues)
2. **Location Services:** Required for BLE scanning (Android OS requirement)
3. **Message Length:** Max 50 characters
4. **Character Set:** 64 characters only (6-bit encoding)
5. **No Persistence:** Messages cleared on app restart
6. **Single Device:** One ESP32S3 connection at a time
7. **UC-7.3 Partial:** No auto-retry when location services enabled

---

## 🔮 Future Enhancements

### High Priority
- [ ] Room database for message persistence
- [ ] ViewModel unit tests with mocked dependencies
- [ ] Instrumentation tests for UI
- [ ] Complete UC-7.3 (location change BroadcastReceiver)

### Medium Priority
- [ ] Dark mode support
- [ ] Message timestamps grouping
- [ ] Connection retry with exponential backoff
- [ ] Message pagination for large histories

### Low Priority
- [ ] Multiple device support
- [ ] Message encryption
- [ ] File attachments (within bandwidth limits)
- [ ] Group messaging
- [ ] Read receipts

---

## 📄 License

Same as original project.

---

## 🤝 Contributing

### Development Setup
1. Clone repository
2. Open in Android Studio
3. Sync Gradle
4. Run tests: `./gradlew test`
5. Build: `./gradlew assembleDebug`

### Code Guidelines
- Follow Clean Architecture principles
- Add `@see UC-X.X` references for traceability
- Write unit tests for new features
- Update documentation (USE_CASES.md, etc.)
- Follow Kotlin coding conventions

### Pull Request Checklist
- [ ] Tests pass (`./gradlew test`)
- [ ] Code documented with UC references
- [ ] CHANGELOG.md updated
- [ ] Use case coverage maintained/improved

---

## 📞 Support

- **Issues:** Report at GitHub issues page
- **Docs:** See `docs/` folder
- **Tests:** `./gradlew test`
- **ESP32 Firmware:** See `../firmware/`

---

**Version:** 1.0.3
**Last Updated:** 2025-10-26
**Protocol:** v3.0
**Implementation:** 92% (23/25 use cases)
**Tests:** 43 unit tests passing
