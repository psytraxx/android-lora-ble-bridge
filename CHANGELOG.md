## Changelog

### Firmware v3.8 (June 2026)
- **Heltec Wireless Stick V3 support**: new `heltec-wireless-stick-v3` PlatformIO environment for the ESP32-S3 board with 64×32 SSD1306 OLED; `DisplayManager` shows BLE status, LoRa RSSI/SNR, and battery level on the display; display powered via VEXT (GPIO36), I²C on SDA=17/SCL=18/RST=21
- **Board rename**: `heltec-wifi-lora-v3` env correctly renamed to `heltec-wireless-stick-lite-v3` to reflect actual hardware; board definition updated to `heltec_wireless_stick_lite_v3`
- **Custom board definitions**: added `firmware/boards/heltec_wireless_stick_v3.json` and `firmware/boards/heltec_wireless_stick_lite_v3.json` — required because pioarduino 55.03.38 does not ship V3 Wireless Stick board JSONs; `boards_dir = boards` added to `[platformio]` section
- **sdkconfig.defaults hardened**: added `CONFIG_BT_ENABLED`, Modbus mode flags, 8 MB flash size, and size-optimised compiler settings so new ESP32 environments generate a correct sdkconfig from scratch without manual copying
- **Documentation**: AGENTS.md, README.md, and memory updated to reflect current board names, build commands, pin tables, and file structure; changelog maintenance rule added to AGENTS.md

### Firmware v3.7 (March 2026)
- **Non-blocking TX→RX settle**: replaced `delay(RX_SETTLE_TIME_MS)` with a new `STATE_TX_SETTLING` state; `process()` polls `millis()` until the deadline passes, then calls `startReceive()` — main loop stays responsive during the 50ms settle window
- **Non-blocking BLE GATT settle**: replaced `delay(500)` in `onBleConnected()` with a `bleGattReadyAt` deadline; buffered-message delivery in `loop()` is deferred until the deadline passes, avoiding a blocking pause on Android reconnect
- **Protocol validation**: `Message::deserialize()` now early-rejects packets where `packedLen` or `charCount` exceed their maximum valid values before any buffer access
- **MessageBuffer corruption recovery**: `peek()` now iterates (not recurses) over corrupted NVS entries, preventing potential stack overflow when multiple consecutive entries are bad
- **`std::unique_ptr`** used for `bleManager`, `loraManager`, and `ledManager` in `unified_main.cpp` — clarifies ownership with zero runtime cost
- **Build hardening**: `-Wextra` added globally; `-Wshadow` scoped to project sources only (via `build_src_flags`) to avoid noise from third-party libraries like RadioLib

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
