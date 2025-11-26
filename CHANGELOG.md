## Changelog

### Firmware v3.3 (November 24-26, 2025)
- Sleep method renamed: `goToSleep()` → `enterDeepSleep()`
- Wake-up reason detection (EXT0 vs EXT1) to prevent WakeUp loops
- LED pin corrected (blue → green)
- CPU frequency reduced to 160 MHz (30-40% power savings)
- Optional WakeUp parameter in `startTransmit()`
- Enhanced SX1262 autonomous duty cycle support
- Code structure refactoring for readability
- GEMINI.md documentation added
- Message send status return value fixed

### Firmware v3.2 (November 21, 2025)
- Unified multi-platform architecture (ESP32 + nRF52)
- Platform traits for compile-time polymorphism
- Loop-based execution on both platforms
- nRF52 (Seeed XIAO nRF52840) support added
- LoRa settings: SF11 → SF9, preamble 512 → 8 symbols
- NimBLE (ESP32) and Arduino BLE (nRF52) integration
- RadioLib support for SX1262 and SX1278
- Message buffering up to 10 messages

### Android App - Kotlin Rewrite (October 2025)
- Migrated from Java to Kotlin + Jetpack Compose
- Clean Architecture (Domain/Data/Presentation)
- 74 unit tests (was 9 in Java version)
- Dependency injection with Hilt
- Material 3 UI with reactive StateFlow
- Auto-reconnect functionality (UC-1.3)

### Protocol v3.0 (October 2025)
- Unified TextMessage with optional GPS (was separate messages)
- Message types: TEXT (0x01), ACK (0x02), WAKE_UP (0x03)
- 6-bit character packing (64-char set, 24% bandwidth savings)
- GPS coordinates optional (hasGps flag)
- Click message to open Google Maps

### Protocol v2.0 (October 2025)
- Separated TextMessage and GpsMessage
- 6-bit encoding introduced (vs UTF-8)
- 40% bandwidth savings for text-only messages

### Protocol v1.0 (October 2025)
- Initial protocol with UTF-8 encoding
- Combined text+GPS in single DataMessage
