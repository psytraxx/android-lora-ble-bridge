# Use Case Implementation Mapping

This document maps all use cases to their implementations in the codebase.

## ✅ Implementation Status Legend
- ✅ **Fully Implemented**
- ⚠️ **Partially Implemented**
- ❌ **Not Implemented**

---

## 1. BLE Connection Management (5 use cases)

### ✅ UC-1.1: Scan for ESP32S3 Device
**Status:** Fully Implemented
**File:** `data/ble/BleRepository.kt`
**Functions:**
- `startScan()` - Lines 118-161
- `stopScan()` - Lines 166-177

**Implementation Details:**
- ✅ Scan filters for device name "ESP32S3-LoRa"
- ✅ Optimized scan settings (SCAN_MODE_LOW_LATENCY)
- ✅ 15s timeout
- ✅ State updates: Scanning → Device found/Error
- ✅ Auto-proceeds to UC-1.2 when found

**Test:** Manual - verify scanning

---

### ✅ UC-1.2: Connect to ESP32S3 Device
**Status:** Fully Implemented
**File:** `data/ble/BleRepository.kt`
**Functions:**
- `connectToDevice()` - Lines 182-202
- `gattCallback` - Lines 207-298

**Implementation Details:**
- ✅ Stop scan before connecting
- ✅ GATT connection
- ✅ MTU negotiation (512 bytes)
- ✅ Service discovery (UUID: 0x1234)
- ✅ Find TX characteristic (0x5678)
- ✅ Find RX characteristic (0x5679)
- ✅ Enable notifications + CCCD write
- ✅ State machine: Connecting → NegotiatingMtu → DiscoveringServices → EnablingNotifications → Connected
- ✅ Error handling for all failure scenarios

**Test:** Manual - verify connection flow

---

### ✅ UC-1.3: Auto-Reconnect on Disconnection
**Status:** Fully Implemented
**File:** `presentation/chat/ChatViewModel.kt`
**Functions:**
- `sendMessage()` - Lines 156-186
- `observeBleConnection()` - Lines 67-89

**Implementation Details:**
- ✅ Send button enabled when disconnected
- ✅ Queue message in `pendingMessageText`
- ✅ Display "Reconnecting..." toast
- ✅ Initiate scan
- ✅ Auto-send queued message when connected
- ✅ Display "Connected! Sending message..." toast
- ✅ Clear input field

**Test:** Unit test recommended

---

### ✅ UC-1.4: Auto-Disconnect After Inactivity
**Status:** Fully Implemented
**File:** `data/ble/BleRepository.kt`
**Functions:**
- `scheduleAutoDisconnect()` - Lines 323-331
- `cancelAutoDisconnect()` - Lines 336-338
- `sendMessage()` - Lines 305-321 (calls scheduleAutoDisconnect)

**Implementation Details:**
- ✅ 30 second timeout (BleConstants.AUTO_DISCONNECT_DELAY_MS)
- ✅ Cancel previous timer on new activity
- ✅ Reschedule on each message send
- ✅ Auto-disconnect implementation

**Constants:** `data/ble/BleConstants.kt` - Line 15

**Test:** Manual - wait 30s after sending message

---

### ✅ UC-1.5: Validate Connection State on Resume
**Status:** Fully Implemented
**File:** `presentation/chat/ChatViewModel.kt`
**Function:** `validateConnectionState()` - Lines 277-281

**Implementation Details:**
- ✅ Called from ChatScreen on resume (LaunchedEffect)
- ✅ Updates GPS on resume
- ✅ Connection state automatically tracked via StateFlow

**Note:** Connection validation is passive (StateFlow observers handle it)

**Test:** Manual - background/foreground app

---

## 2. GPS/Location Management (3 use cases)

### ✅ UC-2.1: Request Single GPS Update
**Status:** Fully Implemented
**File:** `data/location/LocationRepository.kt`
**Function:** `requestSingleUpdate()` - Lines 40-68

**Implementation Details:**
- ✅ Event-driven (called when user sends message)
- ✅ FusedLocationProviderClient first
- ✅ Fallback to GPS provider
- ✅ Fallback to Network provider
- ✅ Auto-remove listeners after update
- ✅ Prevents memory leaks

**Called from:** `ChatViewModel.sendMessageInternal()` - Line 195

**Test:** Manual - send message and check logs

---

### ✅ UC-2.2: Get Last Known Location
**Status:** Fully Implemented
**File:** `data/location/LocationRepository.kt`
**Function:** `getLastKnownLocation()` - Lines 75-126

**Implementation Details:**
- ✅ Check cache (< 1 minute validity)
- ✅ Try Fused provider
- ✅ Fallback to GPS provider
- ✅ Fallback to Network provider
- ✅ Return best available or null

**Called from:**
- `ChatViewModel.updateGps()` - Line 130
- `ChatViewModel.sendMessageInternal()` - Line 198

**Test:** Unit test recommended

---

### ✅ UC-2.3: Display GPS Coordinates
**Status:** Fully Implemented
**File:** `presentation/chat/ChatViewModel.kt`
**Function:** `updateGps()` - Lines 128-135

**UI File:** `presentation/chat/ChatScreen.kt` - Lines 107-110 (GPS text display)

**Implementation Details:**
- ✅ Format: "lat, lon (provider)"
- ✅ Display "No GPS fix" if unavailable
- ✅ Called on: App startup, app resume, GPS update received
- ✅ Observable via StateFlow

**Test:** Manual - check GPS display updates

---

## 3. Message Sending (4 use cases)

### ✅ UC-3.1: Send Text Message with GPS
**Status:** Fully Implemented
**File:** `presentation/chat/ChatViewModel.kt`
**Functions:**
- `sendMessage()` - Lines 156-186 (validation + queue)
- `sendMessageInternal()` - Lines 191-232 (actual send)

**Implementation Details:**
- ✅ Validate length (max 50 chars)
- ✅ Validate character set
- ✅ Request fresh GPS update
- ✅ Get last known location
- ✅ Create TextMessage with seq, text, GPS (optional)
- ✅ Add to chat UI with PENDING status
- ✅ Serialize and send via BLE
- ✅ Disable send button until ACK/timeout
- ✅ Schedule ACK timeout (5s)
- ✅ Schedule auto-disconnect (30s)
- ✅ Retry on failure

**Test:** Manual - send message with/without GPS

---

### ✅ UC-3.2: Handle ACK Reception
**Status:** Fully Implemented
**File:** `presentation/chat/ChatViewModel.kt`
**Function:** `handleReceivedMessage()` - Lines 236-267 (ACK branch: 254-263)

**Implementation Details:**
- ✅ Deserialize ACK
- ✅ Update message status to DELIVERED
- ✅ Display toast: "✓ Message delivered (seq N)"
- ✅ Re-enable send button
- ✅ Clear pending ACK sequence
- ✅ Cancel ACK timeout

**Test:** Manual - verify ACK updates message status

---

### ✅ UC-3.3: Handle ACK Timeout
**Status:** Fully Implemented
**File:** `presentation/chat/ChatViewModel.kt`
**Function:** `scheduleAckTimeout()` - Lines 269-279

**Implementation Details:**
- ✅ 5 second timeout (BleConstants.ACK_TIMEOUT_MS)
- ✅ Re-enable send button on timeout
- ✅ Message remains in PENDING status
- ✅ User can send new messages

**Constants:** `data/ble/BleConstants.kt` - Line 16

**Test:** Manual - send message without paired device (ACK timeout)

---

### ✅ UC-3.4: Validate Message Input
**Status:** Fully Implemented
**File:** `presentation/chat/ChatViewModel.kt`
**Function:** `updateMessageInput()` - Lines 283-293

**UI File:** `presentation/chat/ChatScreen.kt` - Lines 173-183 (character counter display)

**Implementation Details:**
- ✅ Real-time character count
- ✅ Calculate packed byte size (6-bit encoding)
- ✅ Calculate total message size
- ✅ Display: "X/50 chars (Y bytes)"
- ✅ Color coding:
  - Red: ≥ 50 chars (exceeded)
  - Yellow/Tertiary: ≥ 45 chars (warning, 90% threshold)
  - Normal: < 45 chars
- ✅ Enable/disable send button based on:
  - Text not empty
  - Not waiting for ACK

**Test:** Manual - type and verify counter

---

## 4. Message Receiving (2 use cases)

### ✅ UC-4.1: Receive Text Message
**Status:** Fully Implemented
**File:** `presentation/chat/ChatViewModel.kt`
**Function:** `handleReceivedMessage()` - Lines 236-253 (TextMessage branch)

**BLE Reception:** `data/ble/BleRepository.kt` - `handleReceivedData()` - Lines 300-312

**Implementation Details:**
- ✅ Receive notification via BLE TX characteristic
- ✅ Deserialize message (Protocol)
- ✅ Extract text, seq, GPS (if present)
- ✅ Create ChatMessage (received = false)
- ✅ Add to chat display
- ✅ Store GPS coordinates for Maps click
- ✅ Display with timestamp

**Test:** Manual - receive message from another device

---

### ✅ UC-4.2: Open Location in Maps
**Status:** Fully Implemented
**File:** `presentation/chat/ChatScreen.kt`
**Lines:** 150-167 (onMapClick handler in MessageBubble)

**Implementation Details:**
- ✅ Click message bubble with GPS
- ✅ Create Google Maps intent with coordinates
- ✅ Check if Google Maps installed → open app
- ✅ Fallback to browser if Maps not installed
- ✅ Error handling with toast

**Domain Model:** `domain/model/ChatMessage.kt` - `canOpenMaps()` - Lines 20-21

**Test:** Manual - click message with GPS coordinates

---

## 5. Protocol & Serialization (3 use cases)

### ✅ UC-5.1: Serialize Text Message
**Status:** Fully Implemented
**File:** `data/protocol/LoRaProtocol.kt`
**Function:** `serialize()` - Lines 24-28, `serializeTextMessage()` - Lines 59-94

**Implementation Details:**
- ✅ Validate text length (max 50)
- ✅ Convert to uppercase
- ✅ Pack text using 6-bit encoding (packText)
- ✅ Create byte array:
  - [0]: Type (0x01)
  - [1]: Seq number
  - [2]: Character count
  - [3]: Packed byte length
  - [4..N]: Packed text
  - [N+1]: hasGPS flag
  - [N+2..N+9]: GPS (lat/lon, little-endian, microdegrees)
- ✅ Size: 5 + packed text + (9 if GPS)

**Test:** ✅ Unit tested - `LoRaProtocolTest.kt`

---

### ✅ UC-5.2: Deserialize Received Message
**Status:** Fully Implemented
**File:** `data/protocol/LoRaProtocol.kt`
**Functions:**
- `deserialize()` - Lines 33-42
- `deserializeTextMessage()` - Lines 104-127
- `deserializeAckMessage()` - Lines 129-132

**Implementation Details:**
- ✅ Read message type byte
- ✅ Route to type-specific deserializer
- ✅ TextMessage (0x01):
  - Read seq, charCount, packedLen
  - Read packed text bytes
  - Unpack using 6-bit decoding
  - Read hasGPS flag
  - Read GPS if present
- ✅ AckMessage (0x02):
  - Read sequence number
- ✅ Error handling (unknown type, insufficient data)

**Test:** ✅ Unit tested - `LoRaProtocolTest.kt`

---

### ✅ UC-5.3: Validate Character Support
**Status:** Fully Implemented
**File:** `data/protocol/LoRaProtocol.kt`
**Functions:**
- `isTextSupported()` - Lines 50-52
- `isCharacterSupported()` - Lines 57-59

**Implementation Details:**
- ✅ Character set: Space, A-Z, 0-9, .,!?-:;'"@#$%&*()[]{}=+/<>_
- ✅ Lowercase auto-converted to uppercase
- ✅ Validation before sending

**Called from:** `ChatViewModel.sendMessage()` - Lines 168-171

**Test:** ✅ Unit tested - `LoRaProtocolTest.kt`

---

## 6. Chat UI Display (3 use cases)

### ✅ UC-6.1: Display Chat Message
**Status:** Fully Implemented
**Files:**
- `data/repository/MessageRepository.kt` - `addMessage()` - Lines 18-20
- `presentation/components/MessageBubble.kt` - Full component

**Implementation Details:**
- ✅ Create ChatMessage with:
  - Text, sent/received flag, timestamp, seq, ACK status, GPS
- ✅ Add to StateFlow (reactive updates)
- ✅ Display in LazyColumn with MessageBubble
- ✅ Sent messages: Right-aligned, green background
- ✅ Received messages: Left-aligned, white/secondary background
- ✅ Show timestamp (HH:mm)
- ✅ Show ACK status (⏱ pending, ✓ delivered) for sent messages
- ✅ Show GPS indicator (📍) if has GPS
- ✅ Clickable if has GPS

**Test:** Manual - send and receive messages

---

### ✅ UC-6.2: Update ACK Status Indicator
**Status:** Fully Implemented
**File:** `data/repository/MessageRepository.kt`
**Function:** `updateAckStatus()` - Lines 25-33

**UI Component:** `presentation/components/MessageBubble.kt` - Lines 54-67 (ACK icon rendering)

**Implementation Details:**
- ✅ Find message by sequence number
- ✅ Update ackStatus field (PENDING → DELIVERED)
- ✅ StateFlow triggers recomposition
- ✅ Update icon:
  - PENDING: ⏱ (orange)
  - DELIVERED: ✓ (green)

**Called from:** `ChatViewModel.handleReceivedMessage()` - Line 257

**Test:** Manual - verify status changes on ACK

---

### ✅ UC-6.3: Auto-Scroll Chat
**Status:** Fully Implemented
**File:** `presentation/chat/ChatScreen.kt`
**Lines:** 66-71 (LaunchedEffect auto-scroll)

**Implementation Details:**
- ✅ LaunchedEffect triggered when messages.size changes
- ✅ Animate scroll to last item
- ✅ Only if messages not empty

**Test:** Manual - send multiple messages

---

## 7. Permissions Management (3 use cases)

### ✅ UC-7.1: Request BLE Permissions
**Status:** Fully Implemented
**File:** `presentation/chat/ChatScreen.kt`
**Lines:** 42-65 (Permission handling)

**Implementation Details:**
- ✅ Use Accompanist Permissions
- ✅ Request:
  - BLUETOOTH_SCAN (API 31+)
  - BLUETOOTH_CONNECT (API 31+)
  - ACCESS_FINE_LOCATION
- ✅ Request on startup
- ✅ Start scan after permissions granted
- ✅ Toast if denied

**Manifest:** `AndroidManifest.xml` - Lines 5-15

**Test:** Manual - fresh install, grant permissions

---

### ✅ UC-7.2: Request Location Permissions
**Status:** Fully Implemented
**File:** `presentation/chat/ChatScreen.kt`
**Lines:** 42-65 (Same permission handler)

**Implementation Details:**
- ✅ ACCESS_FINE_LOCATION requested
- ✅ Required for both BLE scanning AND GPS
- ✅ Start GPS updates after granted

**Manifest:** `AndroidManifest.xml` - Lines 13-15

**Test:** Manual - verify GPS works after permission

---

### ⚠️ UC-7.3: Handle Location Services Disabled
**Status:** Partially Implemented

**Current Implementation:**
- ✅ Location checks in `LocationRepository.isLocationEnabled()` - Lines 133-138
- ✅ BLE scan fails gracefully if location disabled
- ❌ No BroadcastReceiver for location provider changes
- ❌ No auto-retry when user enables location

**Recommendation:** Add BroadcastReceiver in future enhancement

**Test:** Manual - disable location services, verify error handling

---

## 8. Special UI Behaviors (2 use cases)

### ✅ UC-8.1: Dismiss Keyboard on Send
**Status:** Fully Implemented (by Compose framework)
**File:** `presentation/chat/ChatScreen.kt`
**Lines:** 202-206 (Send button clears input)

**Implementation Details:**
- ✅ Keyboard dismisses automatically when TextField loses focus
- ✅ Input cleared on send
- ✅ Compose handles keyboard management

**Note:** Old Java version had manual `hideSoftInputFromWindow()` - not needed in Compose

**Test:** Manual - send message, verify keyboard dismisses

---

### ✅ UC-8.2: Show Status Bar with Dark Icons
**Status:** Fully Implemented
**File:** `MainActivity.kt`
**Lines:** 22-28

**Implementation Details:**
- ✅ Edge-to-edge mode enabled
- ✅ Light status bar (dark icons)
- ✅ `isAppearanceLightStatusBars = true`

**Test:** Manual - verify status bar appearance

---

## Summary

### Implementation Status
- **Total Use Cases:** 25
- **✅ Fully Implemented:** 23
- **⚠️ Partially Implemented:** 1 (UC-7.3)
- **❌ Not Implemented:** 1 (UC-7.3 - auto-retry on location enable)

### Coverage: 92% Fully Implemented

### Recommendations

#### High Priority
1. **UC-7.3 Enhancement:** Add BroadcastReceiver for PROVIDERS_CHANGED_ACTION to auto-retry BLE scan when user enables location services

#### Testing Priorities
1. Unit tests for:
   - UC-1.3 (message queuing)
   - UC-2.2 (location fallback chain)
   - UC-3.1 (message sending flow)
2. Instrumentation tests for:
   - UC-6.3 (auto-scroll)
   - UC-7.1, UC-7.2 (permissions flow)

---

**Last Updated:** 2025-10-26
**Version:** 1.0.1
