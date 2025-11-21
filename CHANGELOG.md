## Recent Improvements

**Note**: Android app was migrated from Java to Kotlin + Jetpack Compose + Clean Architecture.
Earlier entries refer to the legacy Java implementation.

---

### Firmware v3.2 - Unified Multi-Platform Architecture (November 21, 2025)

#### Major Architecture Refactor
- **Trait-Based Multi-Platform Support**: Single `unified_main.cpp` for ESP32 and nRF52
  - Compile-time platform selection via PlatformTraits
  - Zero runtime overhead (no virtual functions)
  - Clean separation of platform-specific vs common code
  - Files: `firmware/src/unified_main.cpp`, `firmware/include/{esp32,nrf52}/PlatformTraits.h`

#### New Platform: nRF52 Support
- **Seeed XIAO nRF52840** support added
  - ARM Cortex-M4 @ 64 MHz
  - 1MB Flash / 256KB RAM
  - Arduino BLE stack integration
  - SX1262 LoRa radio support
  - Loop-based architecture (non-blocking state machines)
  - Lower power consumption in active mode
  - Files: `firmware/include/nrf52/`, `firmware/src/nrf52/`

#### ESP32 Platform Enhancements
- **Maintained FreeRTOS Architecture**: Task-based concurrent operation
  - BLE Task (priority 3): NimBLE integration, message forwarding
  - LoRa Task (priority 4): RadioLib integration, ISR handling
  - Power Task (priority 2): Timeout monitoring, deep sleep management
  - Files: `firmware/include/esp32/`, `firmware/src/esp32/`

#### Hardware Support Matrix
- **ESP32 Boards**:
  - LilyGo T-Display S3 (SX1278)
  - Heltec WiFi LoRa V3 (SX1262 with autonomous duty cycle)
- **nRF52 Boards**:
  - Seeed XIAO nRF52840 (SX1262)
- **Radio Support**:
  - SX1262: Autonomous duty cycle (~1.5-2mA avg)
  - SX1278: Continuous RX mode (~12-15mA avg)

#### LoRa Configuration Updates
- **Spreading Factor**: Changed from SF11 → SF9 for balanced range/speed
  - Range: 3-10 km (vs 5-15 km @ SF11)
  - Airtime: ~0.3-0.6s per message (vs ~0.8-1.0s @ SF11)
  - Duty cycle: 60-180 msgs/hour (vs 36-90 msgs/hour @ SF11)
- **Maintained**: BW250 kHz, CR4/5, 433.92 MHz, 20 dBm TX
- **Preamble**: 8 symbols (down from 512, using default RadioLib)

#### BLE Integration
- **ESP32**: NimBLE-Arduino stack
  - Lower RAM usage vs ESP-IDF BLE
  - FreeRTOS task integration
  - Custom BLE adapter implementation
  - Files: `firmware/include/esp32/BLEManager.h`, `firmware/src/esp32/BLEManager.cpp`
- **nRF52**: Arduino BLE stack
  - SoftDevice integration
  - Event-driven callbacks
  - Non-blocking operations
  - Files: `firmware/include/nrf52/BLEManager.h`, `firmware/src/nrf52/BLEManager.cpp`

#### Code Organization
- **Directory Structure**:
  ```
  firmware/
  ├── include/
  │   ├── common/         # Platform-agnostic code (MessageQueue, etc.)
  │   ├── esp32/          # ESP32-specific headers
  │   ├── nrf52/          # nRF52-specific headers
  │   └── Protocol.h      # Shared protocol
  ├── src/
  │   ├── unified_main.cpp    # Single entry point
  │   ├── Protocol.cpp        # Shared implementation
  │   ├── esp32/              # ESP32 implementations
  │   └── nrf52/              # nRF52 implementations
  └── platformio.ini      # Multi-environment build config
  ```

#### Build System
- **PlatformIO Environments**:
  - `lilygo-t-display-s3`: ESP32-S3 with SX1278
  - `heltec-wifi-lora-v3`: ESP32-S3 with SX1262
  - `xiao_nrf52840`: nRF52840 with SX1262
- **Build Flags**: Platform-specific pin definitions and radio types
- **Device Names**: Auto-generated from chip ID via Python script

#### Performance Improvements
- **Resource Usage**:
  - ESP32: ~400-500 KB Flash, ~51 KB RAM (15.6% of 327 KB)
  - nRF52: ~300-400 KB Flash, ~30-40 KB RAM (12-16% of 256 KB)
- **Battery Life**:
  - SX1262 (autonomous duty cycle): Multiple weeks on 2500 mAh
  - SX1278 (continuous RX): Several days on 2500 mAh

#### Documentation Updates
- **firmware/ARCHITECTURE.md**: Complete rewrite for multi-platform architecture
  - Platform comparison tables
  - Trait system documentation
  - Resource usage breakdowns
  - Migration guide from v2.0
- **protocol.md**: Updated LoRa configuration and timing tables
- **README.md**: Added platform support matrix and build instructions

#### Testing & Validation
- **Cross-Platform Protocol**: Verified binary compatibility
  - ESP32 ↔ nRF52 communication
  - Android ↔ ESP32/nRF52 BLE
  - PWA ↔ ESP32/nRF52 Web Bluetooth
- **Unit Tests**: Protocol serialization tests pass on all platforms

#### Breaking Changes
**None** - Protocol v3.1 remains unchanged, all platforms compatible

#### Migration Path
- **From ESP32-only v2.0**: No changes required for ESP32 builds
- **Code moved**: ESP32-specific code now in `esp32/` subdirectories
- **Build targets**: Use `-e <environment>` flag with pio

---

### Android App - Critical Connection State & Message Delivery Fixes (October 23, 2025)
*Legacy Java implementation - since replaced by Kotlin/Compose version*

#### Critical Bug Fixes
- **CRITICAL - Buffered Messages Not Delivered on Reconnect**: Fixed timing issue where messages were lost during reconnection
  - Problem: Android set `connected=true` BEFORE notifications were fully enabled on ESP32
  - ESP32 would immediately send buffered messages, but Android wasn't ready to receive them
  - Solution: Delay `connected=true` until `onDescriptorWrite` callback confirms notifications enabled
  - Impact: All buffered messages now reliably delivered when app reconnects
  - File: `BleManager.java:330-405` (legacy Java implementation)

- **CRITICAL - Connection State UI Still Mismatched**: Enhanced connection state validation
  - Problem: Validation wasn't checking actual GATT connection state from BluetoothManager
  - Only checked if characteristic objects existed, not if GATT was actually connected
  - Solution: Query `BluetoothManager.getConnectionState()` for actual connection status
  - Added synchronous execution when already on main thread (prevents async race conditions)
  - Impact: Send button state now ALWAYS matches connection status text
  - File: `BleManager.java:434-482` (legacy Java implementation)

#### Technical Details
**Connection Sequence** (Now Correct):
1. BLE GATT connects → Status: "🔗 Negotiating..."
2. MTU negotiated → Status: "🔧 Discovering services..."
3. Services discovered → TX/RX characteristics found
4. Notification enable requested → CCCD descriptor write initiated
5. **NEW**: Wait for `onDescriptorWrite` success callback
6. **ONLY THEN**: Set `connected=true` → Status: "✅ Ready to send!"

**Why This Matters:**
- ESP32 sends buffered messages as soon as Android sets `connected=true`
- If notifications aren't enabled yet, messages are lost
- Old behavior: Connected too early (step 4)
- New behavior: Connected at right time (step 6)

#### Test Results
- **Build**: ✅ Successful
- **Unit Tests**: ✅ All 9 tests passing (legacy Java implementation; current Kotlin version has 43 tests)
- **Impact**: Reliable message delivery on every reconnection

---

### Android App - Connection State Fix & Foreground Service Removal (October 23, 2025)
*Legacy Java implementation - since replaced by Kotlin/Compose version*

#### Critical Bug Fixes
- **CRITICAL - Connection State Synchronization**: Fixed UI state mismatch where send button was greyed out but status showed "Ready to send"
  - Problem: LiveData connection state could become stale when app went to background and returned to foreground
  - Root Cause: No validation of actual GATT connection state (bluetoothGatt, txCharacteristic, rxCharacteristic) on app resume
  - Solution: Added `validateConnectionState()` method that checks actual GATT objects and forces LiveData update
  - Impact: UI now always accurately reflects actual BLE connection state after screen unlock or app switching
  - File: `BleManager.java:428-464`, `MainActivity.java:186-198` (legacy Java implementation)

#### Architecture Simplification
- **Removed Foreground Service**: Eliminated LoRaForegroundService as it's unnecessary with ESP32's message buffering
  - ESP32 already buffers up to 10 messages when BLE disconnected and re-delivers them on reconnect
  - Foreground service caused state synchronization issues between service and MainActivity
  - Reduces app complexity and memory footprint (~2-5 MB savings)
  - Simplifies permission requirements (removed FOREGROUND_SERVICE, POST_NOTIFICATIONS)
  - Files removed: `LoRaForegroundService.java` (legacy Java implementation)
  - Files modified: `MainActivity.java`, `AndroidManifest.xml` (legacy Java implementation)

#### Code Quality Improvements
- **Scheduled Disconnect Fix**: Prevented multiple overlapping disconnect timers
  - Problem: Each message send scheduled a new 30-second disconnect without cancelling previous ones
  - Solution: Cancel previous disconnect callback before scheduling new one
  - Added `cancelPendingDisconnect()` method for explicit cancellation
  - File: `MessageViewModel.java:165-187` (legacy Java implementation)

- **LiveData Update Strategy**: Changed from `postValue()` to `setValue()` for immediate observer notification
  - Ensures UI updates happen synchronously when validating connection state
  - Prevents race conditions between background threads and main thread

#### Test Results
- **Build**: ✅ Successful
- **Unit Tests**: ✅ All 9 tests passing (legacy Java implementation; current Kotlin version has 43 tests)
- **Impact**: Simpler, more reliable app with accurate UI state

---

### ESP32 Firmware - Power Optimization & Critical Bug Fixes (October 23, 2025)

#### Critical Bug Fixes
- **CRITICAL - Message Buffer Corruption**: Fixed message buffering implementation that caused Text and ACK messages to overwrite each other
  - Problem: Three separate `static Message bufferedMessages[10]` buffers declared in loop() scope with overlapping storage
  - Solution: Created `MessageBuffer` class with proper circular buffer implementation (single global instance)
  - Impact: Messages now reliably buffered and delivered when BLE reconnects
  - Files: `firmware/include/MessageBuffer.h` (new), `firmware/src/main.cpp`

- **BLE Advertising Timeout Removed**: Eliminated 8-second inactivity timeout that stopped advertising
  - Problem: Android couldn't reconnect after timeout, preventing buffered message delivery
  - Solution: Removed automatic advertising stop - now always discoverable
  - Impact: Android can always reconnect to retrieve buffered LoRa messages
  - File: `firmware/src/BLEManager.cpp:194-196`

#### Power Optimizations
- **Adaptive Loop Delay**: Implemented intelligent delay based on activity
  - Idle state: 100ms delay (90% CPU usage reduction)
  - Active state: 10ms delay (maintains responsiveness)
  - Impact: Significant power savings when no BLE/LoRa activity
  - File: `firmware/src/main.cpp:489-503`

- **ISR Optimization**: Improved interrupt handling for LoRa reception
  - Added `IRAM_ATTR` to LoRa receive callback for fast execution
  - Proper use of `xQueueSendFromISR` and `portYIELD_FROM_ISR`
  - Volatile flag for activity tracking
  - Impact: Efficient interrupt processing, always receives LoRa messages

- **Removed Redundant Code**: Cleaned up unnecessary power management calls
  - Removed redundant `esp_wifi_set_ps()` call in loop (WiFi already disabled)
  - Note: Light sleep intentionally NOT implemented (would prevent BLE wake-up)

#### Code Quality Improvements
- **Function Extraction**: Refactored main loop for maintainability
  - Extracted `processLoRaPacket()` (120 lines)
  - Extracted `handleLoRaToBleForwarding()` (50 lines)
  - Reduced main loop complexity

- **Message Buffer Management**: Centralized buffering logic
  - Circular buffer with FIFO behavior
  - Automatic oldest message drop when full (10 message capacity)
  - Clear separation between queue (live messages) and buffer (offline storage)

#### Performance Characteristics
- **Power Consumption**:
  - Idle (BLE advertising + LoRa RX): ~40-50mA
  - Active (BLE connected): ~80-100mA
  - LoRa TX: ~120mA peak (brief)

- **Battery Life (2500mAh)**:
  - Mostly idle: 50-60 hours
  - Mixed usage: 25-30 hours
  - Continuous activity: 20-25 hours

- **Memory Usage**:
  - RAM: 10.5% (34,476 / 327,680 bytes)
  - Flash: 6.6% (433,540 / 6,553,600 bytes)

#### System Reliability
- ✅ Always receives LoRa messages (interrupt-driven)
- ✅ Buffers up to 10 messages when BLE disconnected
- ✅ Delivers all buffered messages on Android reconnection
- ✅ Never stops advertising automatically
- ✅ CPU @ 160MHz (power-optimized)
- ✅ WiFi/Bluetooth Classic disabled

---

### Android App - Critical Bug Fixes & Code Quality (October 23, 2025)
*Legacy Java implementation - since replaced by Kotlin/Compose version*

#### Connection State & UX Fixes
- **CRITICAL - Send Button/Status Mismatch**: Fixed mismatched UI state where send button was disabled but status showed "Ready"
  - Problem: `connected` flag set too early (on BLE connect) before service discovery completed
  - Solution: Only set `connected=true` after full successful connection (MTU, service discovery, characteristics)
  - Added `connected=false` for all failure cases (missing service, missing characteristics, discovery failed)
  - Impact: Send button state now always matches connection status text
  - File: `BleManager.java:248, 330, 334, 339, 344` (legacy Java implementation)

- **Reconnect Button Improvements**: All error messages now include "Tap here to reconnect" for consistency
  - Users can always manually reconnect by tapping status text
  - Works for all error states (disconnected, characteristics missing, service not found, discovery failed)

#### BLE Scan Optimization
- **Fast Device Discovery**: Implemented optimized BLE scanning with filters and settings
  - **ScanFilter**: Only scans for devices named "ESP32S3-LoRa" (ignores other BLE devices)
  - **ScanSettings**: LOW_LATENCY mode for fastest scanning
  - **Match Mode**: AGGRESSIVE matching reports device immediately
  - **Impact**: Device found in 1-3 seconds (vs 5-15 seconds previously) in crowded BLE environments
  - **Trade-off**: Higher power during scan, but much shorter scan duration
  - File: `BleManager.java:178-196` (legacy Java implementation)

#### Memory Leak Fixes
- **Handler Cleanup**: Fixed memory leak in MessageViewModel - Handler callbacks now properly cleaned up in `onCleared()`
- **LocationListener Leaks**: Completely refactored GpsManager to use reusable LocationListener instances instead of creating anonymous listeners
  - Reduced GpsManager from 279 → 218 lines (22% reduction)
  - Fixed auto-cleanup after single location updates
  - Removed 97 lines of dead code (unused continuous update infrastructure)

#### Race Condition Fixes
- **BLE Disconnect**: Replaced Thread-based auto-disconnect with Handler-based implementation to prevent race conditions
- **Connection State**: Fixed BLE connection state management when device powers off
  - Properly closes GATT connection on disconnect
  - Resets characteristics to null
  - Handles connection failures gracefully
  - Reconnection now reliable after device power cycle

#### Logic Bug Fixes
- **GPS Management**: Removed unnecessary `startLocationUpdates()` / `stopLocationUpdates()` calls (app uses event-driven single updates)
- **Permission Helper**: Added null/empty array validation in `areAllPermissionsGranted()` to prevent false positives
- **MainActivity Lifecycle**: Removed redundant GPS stop/start in onPause/onResume (already event-driven)

#### Code Quality Improvements
- **Color Resources**: Extracted 7 hardcoded color values to `colors.xml` for better maintainability and theming support
- **BLE Scan Timeout**: Increased from 7 seconds → 15 seconds for better device discovery
- **Code Reduction**: Net reduction of ~40 lines while fixing all issues

#### Test Results
- **Build**: ✅ All builds successful
- **Unit Tests**: ✅ All 9 tests passing (legacy Java implementation; current Kotlin version has 43 tests)
- **Impact**: More stable, efficient, and maintainable codebase

---

### Critical Bug Fixes & Reliability Improvements (October 23, 2025)

#### ESP32 Firmware
- **Dead Code Cleanup**: Removed orphaned sleep management code from v2.2.0 (empty `updateSleepActivity()` function)
- **BLE Disconnect Timeout**: Increased from 8 seconds → 60 seconds to prevent disconnects during active messaging
- **Code Quality**: Cleaner codebase with no unused function calls

#### Android App
- **UI Thread Fix**: Removed blocking `Thread.sleep()` that could cause ANR (Application Not Responding)
  - Implemented async connection handling with background threads
  - Added user feedback via Toast messages for connection status
- **Message Retry Logic**: Added automatic retry mechanism for failed message sends
  - 1-second retry delay in background thread
  - User notification when retry occurs
  - Improved message delivery reliability
- **BLE Scan Timeout**: Increased from 5 seconds → 15 seconds for better device discovery in noisy RF environments
- **Disconnect Delay**: Increased from 5 seconds → 30 seconds to allow time for ACK reception
- **Error Handling**: Added user-facing error messages for all failure scenarios

#### Background Service (Android)
- **NEW: Foreground Service**: Maintains BLE connection even when app is minimized
  - Receives LoRa messages in background
  - Shows notifications for incoming messages
  - Persistent notification displays connection status
  - Automatic device scanning on service start
  - Proper lifecycle management
- **Required Permissions**: Added `FOREGROUND_SERVICE` and `POST_NOTIFICATIONS` permissions
- **Android 8.0+**: Uses foreground service type `connectedDevice`
- **Impact**: True "always-on" message reception capability

#### Performance Impact
- **ESP32**: Better battery life (fewer reconnections due to longer timeout)
- **Android**: Slightly increased memory (~2-5 MB for service), significantly improved UX
- **Reliability**: Automatic retry and background reception greatly improve message delivery

#### Breaking Changes
**None** - All changes are backward compatible with existing protocol and devices

---

### Protocol v3.0 - Unified Text and GPS Messages (October 2025)
- **Unified Message Type**: Text messages now include optional GPS coordinates (single message instead of two)
- **Message Types Reduced**: From 3 types (Text 0x01, GPS 0x02, ACK 0x03) to 2 types (Text 0x01, ACK 0x02)
- **Click to Navigate**: Tap any message with GPS to open Google Maps (no GPS text shown in bubble)
- **Wire Format**: Text + HasGPS flag + optional Lat/Lon fields
- **Performance Benefits**:
  - One message instead of two (no 1200ms inter-message delay)
  - Faster transmission and lower latency
  - Simplified message handling across all platforms
- **Breaking Change**: Not backward compatible - all devices must update simultaneously

### Android App Refactoring (October 2025)
- **Separation of Concerns**: Completely refactored MainActivity (~600 lines → ~150 lines) by extracting business logic into dedicated managers
- **BleManager**: New class handling all Bluetooth LE operations (scanning, connection, GATT services, message sending/receiving)
- **GpsManager**: Dedicated GPS location management with fallback providers (GPS, Network, Fused)
- **MessageViewModel**: MVVM pattern implementation for message state management and UI updates
- **PermissionHelper**: Utility class for centralized permission checking and requests
- **Layout Optimization**: Fixed unnecessary nested LinearLayout in message items, improved performance
- **Build Fixes**: Resolved compilation errors and lint warnings for stable builds

### Android Chat UI Refresh (Oct 2025)
- **Chat Layout**: Introduced RecyclerView with message bubbles, timestamps, GPS markers, and delivery (ACK) indicators.
- **Status Banner**: Connection and GPS info now live in a compact, icon-led header.
- **Input Experience**: Added single-line composer with character counter, keyboard send/enter handling, and automatic dismissal after sending.

### Protocol v2.0 - Separate Message Types (Oct 2025)
- **Separate Messages**: Text and GPS now sent as independent messages
  - `TextMessage` (0x01): Text only with 6-bit packing
  - `GpsMessage` (0x02): GPS coordinates only (10 bytes fixed)
  - `AckMessage` (0x03): Acknowledgments
- **Bandwidth Savings**: 
  - Text-only: 40% smaller (7 bytes for "SOS" vs 15 bytes)
  - 6-bit encoding: 25% smaller than UTF-8 for uppercase text
  - GPS optional: Only sent when GPS is enabled
- **Flexible Usage**:
  - Send text without GPS when location not needed
  - Send GPS updates separately for tracking
  - Text always sent, GPS only when available

### Power Optimization (40-50% savings)
- **CPU Clock**: Reduced from 240 MHz to 160 MHz
- **Auto Light Sleep**: Enabled via Embassy async framework
- **Battery Life**: 70-100 hours on 2500 mAh (was 50-60 hours)

### Message Buffering
- **Buffer Capacity**: 10 messages (was 1)
- **BLE→LoRa Channel**: 5 messages (increased from 1 for text+GPS bursts)
- **Behavior**: Continues receiving LoRa messages even when phone is disconnected
- **On Reconnect**: All buffered messages delivered immediately
