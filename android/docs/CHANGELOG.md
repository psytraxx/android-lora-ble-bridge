# LoRa Bridge - Changelog

## [1.0.3] - 2025-10-26

### Added - Unit Tests
- **43 Unit Tests Across 4 Test Files**
  - `LoRaProtocolTest.kt` - 13 tests (fixed calculation bug)
  - `LocationDataTest.kt` - 9 tests (NEW)
  - `ChatMessageTest.kt` - 10 tests (NEW)
  - `MessageRepositoryTest.kt` - 11 tests (NEW)

### Fixed
- **LoRaProtocolTest** calculation comment - "HELLO WORLD" is 11 chars, not 10
  - Fixed expected packed size: 9 bytes (was incorrectly commented as 8)
  - Added explicit test for 10-char string

### Coverage
- **Use Cases Tested:** 6/25 (24%)
  - UC-5.1: Serialize Text Message ✅
  - UC-5.2: Deserialize Received Message ✅
  - UC-5.3: Validate Character Support ✅
  - UC-2.2: Get Last Known Location (model) ✅
  - UC-6.1: Display Chat Message (model + repository) ✅
  - UC-6.2: Update ACK Status Indicator ✅

### Dependencies
- Added `kotlinx-coroutines-test:1.8.1` for coroutine testing

### Documentation
- Created `TESTING_SUMMARY.md` - comprehensive testing guide
  - Test coverage by use case
  - How to run tests
  - Future testing recommendations
  - CI/CD integration examples

### Run Tests
```bash
./gradlew test
```

---

## [1.0.2] - 2025-10-26

### Added
- **Use Case Implementation Tracking**
  - Created `USE_CASE_IMPLEMENTATION_MAP.md` - comprehensive mapping of all 25 use cases to code
  - Added `@see UC-X.X` references throughout codebase for traceability
  - Coverage: 92% fully implemented (23/25), 4% partially implemented (1/25)
  - Files annotated:
    - `BleRepository.kt` - UC-1.1, UC-1.2, UC-1.4
    - `LocationRepository.kt` - UC-2.1, UC-2.2
    - `LoRaProtocol.kt` - UC-5.1, UC-5.2, UC-5.3
    - `ChatViewModel.kt` - UC-1.5, UC-2.3, UC-3.1, UC-3.2, UC-3.3, UC-3.4, UC-4.1
    - `MessageRepository.kt` - UC-6.1, UC-6.2
    - `ChatScreen.kt` - UC-7.1, UC-7.2, UC-6.3, UC-8.1
    - `MessageBubble.kt` - UC-6.1, UC-6.2, UC-4.2
    - `MainActivity.kt` - UC-8.2

### Documentation
- Detailed implementation status for each use case
- Line number references for all implementations
- Testing recommendations
- Known limitations (UC-7.3 partially implemented)

---

## [1.0.1] - 2025-10-26

### Fixed
- **UC-1.3: Auto-Reconnect on Disconnection** - Implemented queue-and-reconnect behavior
  - Send button now enabled when disconnected (not just when connected)
  - When user sends message while disconnected:
    - Message is queued
    - "Reconnecting..." toast displayed
    - BLE scan initiated
    - Input field cleared immediately
    - Message automatically sent when connection established
    - "Connected! Sending message..." toast displayed
  - Updated `ChatViewModel.kt`:
    - Added `pendingMessageText` field to queue messages
    - Modified `canSendMessage` logic: enabled when NOT waiting for ACK (regardless of connection state)
    - Split `sendMessage` into validation + `sendMessageInternal` for reuse
    - Auto-send queued message in `observeBleConnection` when connected
  - Files changed:
    - `presentation/chat/ChatViewModel.kt`

### Implementation Details
```kotlin
// Before: Could only send when connected
canSendMessage = connectionState is BleConnectionState.Connected && pendingAckSeq == null

// After: Can send when disconnected (for queue) or connected (for immediate send)
canSendMessage = pendingAckSeq == null  // Only blocked when waiting for ACK
```

**User Flow:**
1. User types message while disconnected
2. User clicks Send button (now enabled)
3. Message queued + input cleared
4. "Reconnecting..." toast shown
5. BLE scan starts
6. [Connection established]
7. "Connected! Sending message..." toast shown
8. Queued message sent automatically
9. Normal ACK flow continues

---

## [1.0.0] - 2025-10-26

### Added
- Initial modern re-implementation with Kotlin + Jetpack Compose
- Clean Architecture (Domain/Data/Presentation layers)
- Kotlin Coroutines + StateFlow for reactive state
- Hilt dependency injection
- BLE connection management with state machine
- GPS location with FusedLocationProvider
- Protocol v3.0 with 6-bit encoding
- Chat UI with Jetpack Compose + Material 3
- Permissions handling with Accompanist
- Unit tests for protocol layer
- Comprehensive documentation:
  - USE_CASES.md (31 use cases)
  - STATE_DIAGRAM.md (6 Mermaid diagrams)
  - IMPLEMENTATION_SUMMARY.md

### Technology Stack
- Kotlin 2.0.21
- Jetpack Compose with Material 3
- Hilt 2.51.1
- Coroutines 1.8.1
- StateFlow/Flow
- Compile SDK 35, Min SDK 29

### Features
- ✅ BLE scanning, connection, auto-reconnect, auto-disconnect
- ✅ GPS single updates with provider fallback
- ✅ Message serialization with 6-bit encoding
- ✅ ACK tracking with timeout
- ✅ Chat UI with message bubbles, status indicators
- ✅ Clickable GPS messages → Google Maps
- ✅ Character counter with validation
- ✅ Real-time connection status
