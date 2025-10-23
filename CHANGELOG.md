## Recent Improvements

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
