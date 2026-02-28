## Changelog

### Firmware v3.6 (February 2026)
- **CAD-based transmission queue** added to `LoRaManager`
  - `queueTransmit()` is now the public API for all outgoing packets (ACKs and BLE-originated messages)
  - Internal 5-entry circular TX queue; `processTxQueue()` runs in the main loop when radio is idle
  - Channel Activity Detection via `radio->scanChannel()`: transmits if free, backs off if busy
  - Up to 5 CAD retries with jitter before force-transmitting (`CAD_MAX_RETRIES`, `CAD_BACKOFF_BASE_MS`, `CAD_BACKOFF_JITTER_MS`)
  - Radio restarted into RX after each failed CAD scan (scanChannel leaves it in standby)
- **Removed `getAckDelay()`** - CAD collision avoidance replaces manual ACK delay + jitter
- **PowerManager fix**: serial flush and drain delay now occur after GPIO pin configuration, immediately before `Serial.end()` — ensures all logs are sent before deep sleep
- **PlatformIO**: platform version downgraded for Heltec WiFi LoRa v3 compatibility
- **sdkconfig**: removed unused ESP TLS peripheral configuration

### Android (February 2026)
- Dependency bumps: AGP 9.0.1, Kotlin 2.3.10, Compose BOM 2026.02.01, Hilt 2.59.2, Activity Compose 1.12.4, Mockito 5.22.0

### Firmware v3.5 (January 2026)
- **Removed WakeUp message type (0x03)** - Protocol simplified to Text (0x01) and Ack (0x02) messages only
- **LoRa config updated**: BW250 kHz + CR4/5 (was BW125 + CR4/8) for better throughput
- Preamble extended from 32 to 64 symbols - text messages now directly wake duty-cycled receivers
- ACK timing simplified - short delay (~150-450ms) instead of ToA-based calculation (~2+ seconds)
- Removed WAKEUP_TO_MESSAGE_DELAY_MS - no longer needed with extended preamble
- Documentation updated across README.md, protocol.md, AGENTS.md

### Firmware v3.4 (November 29, 2025)
- LoRa configuration optimized for dense urban environments
- Updated to SF11 + BW125 kHz + CR4/8 for maximum range (~3.5x improvement)
- Preamble reduced from 512 to 32 symbols
- Auto-calculated timing constants (ACK_DELAY_MS)
- Documentation updated across README.md, protocol.md, GEMINI.md
- Range improvement: 3-10 km → 10-35 km typical

### Firmware v3.3 (November 24-26, 2025)
- Sleep method renamed: `goToSleep()` → `enterDeepSleep()`
- Wake-up reason detection (EXT0 vs EXT1) for proper handling
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
- Message types: TEXT (0x01), ACK (0x02) - Note: WAKE_UP (0x03) was removed in v3.5
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
