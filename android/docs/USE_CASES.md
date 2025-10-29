# LoRa Bridge - Use Cases Documentation

This document outlines all use cases extracted from the existing Android application. These will guide the modern Kotlin/Compose re-implementation.

---

## 1. BLE Connection Management

### UC-1.1: Scan for ESP32S3 Device
**Actor:** System  
**Trigger:** App startup, or upon losing connection  
**Preconditions:**
- BLE permissions granted
- Bluetooth enabled
- Location services enabled (Android requirement for BLE scanning)
- Not currently connected to ESP32S3

**Flow:**
1. System continuously scans for ESP32S3 device (device name: "ESP32S3-LoRa") as long as not connected.
2. System uses optimized scan settings (low latency mode).
3. System displays "Scanning..." status.
4. When target device found, proceed to UC-1.2.
5. If not found, scanning continues until device is found or app is closed/backgrounded.

**Postconditions:**
- Device discovered and connected, or scanning continues.

---

### UC-1.2: Connect to ESP32S3 Device
**Actor:** System
**Trigger:** Device discovered from scan
**Preconditions:**
- Target device discovered
- BLE permissions granted

**Flow:**
1. System stops BLE scan
2. System initiates GATT connection
3. System displays "Connecting..." status
4. System negotiates MTU (512 bytes)
5. System discovers GATT services
6. System finds LoRa service (UUID: 0x1234)
7. System finds TX characteristic (UUID: 0x5678) for receiving notifications
8. System finds RX characteristic (UUID: 0x5679) for sending messages
9. System enables notifications on TX characteristic
10. System writes to CCCD descriptor to enable notifications server-side
11. System displays "Ready to send!" status
12. System sets connected state to true

**Postconditions:**
- GATT connection established
- Characteristics configured
- Notifications enabled
- App ready to send/receive messages

**Error Scenarios:**
- Connection fails: Display "Connection failed", clean up GATT
- Service not found: Display "LoRa service not found"
- MTU negotiation fails: Continue with default MTU

---

### UC-1.3: No Message Sending When Disconnected
**Actor:** User  
**Trigger:** User attempts to send message  
**Preconditions:**
- BLE not connected

**Flow:**
1. User attempts to send a message.
2. System detects disconnected state.
3. System prevents sending and disables send button.

**Postconditions:**
- Message is not sent or queued.

---

### UC-1.4: Auto-Disconnect After Inactivity
**Actor:** System
**Trigger:** Message sent successfully
**Preconditions:**
- BLE connected
- Message sent

**Flow:**
1. System schedules disconnect after 30 seconds
2. If new message sent before timeout, cancel previous timer and reschedule
3. After 30 seconds of no activity, system disconnects BLE
4. System displays "Disconnected" status

**Postconditions:**
- BLE disconnected to save power

---

### UC-1.5: Validate Connection State on Resume
**Actor:** System
**Trigger:** App comes to foreground
**Preconditions:**
- App resuming from background

**Flow:**
1. System checks actual GATT connection state
2. System checks if characteristics are valid
3. System updates UI connection status to match reality
4. System forces observers to fire for UI update

**Postconditions:**
- UI reflects actual connection state

---

## 2. GPS/Location Management

### UC-2.1: Request Single GPS Update
**Actor:** System
**Trigger:** User sends message
**Preconditions:**
- Location permissions granted
- GPS or Network provider enabled

**Flow:**
1. System requests single update from GPS provider
2. System also requests single update from Network provider (fallback)
3. When update received, system updates current location
4. System automatically removes location listeners

**Postconditions:**
- Current location updated (if available)
- Listeners removed to prevent memory leaks

---

### UC-2.2: Get Last Known Location
**Actor:** System
**Trigger:** Display GPS coordinates, send message
**Preconditions:**
- Location permissions granted

**Flow:**
1. If cached location exists and is recent (<1 minute), return it
2. Otherwise, try Fused location provider first
3. Fall back to GPS provider if fused unavailable
4. Fall back to Network provider if GPS unavailable
5. Return best available location or null

**Postconditions:**
- Best available location returned

---

### UC-2.3: Display GPS Coordinates
**Actor:** User
**Trigger:** App startup, app resume, GPS update received
**Preconditions:**
- Location permissions granted

**Flow:**
1. System gets last known location (UC-2.2)
2. If location available, format as "lat, lon (provider)"
3. If location unavailable, display "No GPS fix"
4. System updates GPS display TextView

**Postconditions:**
- GPS coordinates displayed OR "No GPS fix" shown

---

## 3. Message Sending

### UC-3.1: Send Text Message with GPS
**Actor:** User  
**Trigger:** User clicks send button  
**Preconditions:**
- BLE connected
- Message text entered (1-50 characters)
- Text contains only supported characters
- Location permissions granted

**Flow:**
1. User enters message text (auto-converts to uppercase)
2. User clicks send button
3. System validates text length (max 50 chars)
4. System validates character set (6-bit encoding)
5. System requests fresh GPS update (UC-2.1)
6. System gets last known location
7. System creates TextMessage with seq number, text, and GPS (if available)
8. System adds message to chat display with PENDING status
9. System serializes message using Protocol
10. System sends via BLE RX characteristic
11. System disables send button (waiting for ACK)
12. System schedules ACK timeout (5 seconds)
13. System schedules auto-disconnect (30 seconds)
14. System clears message input

**Postconditions:**
- Message displayed in chat with PENDING status
- Send button disabled until ACK or timeout
- Message transmitted via LoRa

**Error Scenarios:**
- Text too long: Truncate to 50 characters
- Invalid characters: Display "unsupported characters" toast
- Send fails: Display "Send failed - retrying..." and retry after 1s
- **If not connected:** Sending is not possible; send button is disabled and/or user is notified.

---

### UC-3.2: Handle ACK Reception
**Actor:** System
**Trigger:** ACK message received from ESP32
**Preconditions:**
- Message sent with pending ACK

**Flow:**
1. System receives ACK message via BLE TX characteristic
2. System deserializes ACK (gets sequence number)
3. System updates message status to DELIVERED in chat
4. System displays "✓ Message delivered (seq N)" toast
5. System re-enables send button
6. System clears pending ACK sequence

**Postconditions:**
- Message shows DELIVERED status with checkmark
- Send button enabled for new messages

---

### UC-3.3: Handle ACK Timeout
**Actor:** System
**Trigger:** 5 seconds elapsed without ACK
**Preconditions:**
- Message sent, waiting for ACK

**Flow:**
1. After 5 seconds, timeout fires
2. System re-enables send button
3. Message remains in PENDING status (clock icon)

**Postconditions:**
- User can send new messages
- Original message shows as pending (not delivered)

---

### UC-3.4: Validate Message Input
**Actor:** User
**Trigger:** User types in message input
**Preconditions:**
- Message input has focus

**Flow:**
1. User types character
2. System calculates character count
3. System calculates packed byte size (6-bit encoding)
4. System calculates total message size (header + packed text + GPS)
5. System displays "X/50 chars (Y bytes)"
6. If count ≥ 50: Display in red
7. Else if count ≥ 45 (90% threshold): Display in yellow/warning color
8. Else: Display in normal color
9. System enables/disables send button based on:
   - Text not empty
   - Connection active
   - Not waiting for ACK

**Postconditions:**
- Character counter updated with color coding
- Send button enabled/disabled appropriately

---

## 4. Message Receiving

### UC-4.1: Receive Text Message
**Actor:** System
**Trigger:** Notification received on BLE TX characteristic
**Preconditions:**
- BLE connected
- Notifications enabled

**Flow:**
1. System receives notification with byte data
2. System deserializes message using Protocol
3. If TextMessage:
   - System extracts text, sequence number, GPS (if present)
   - System adds message to chat display (received side)
   - If GPS present, store coordinates for Maps click
   - System displays message with timestamp at the bottom
   - Message scrolls into view if at bottom
4. If AckMessage:
   - Proceed to UC-3.2

**Postconditions:**
- Received message displayed in chat
- GPS coordinates stored if present

---

### UC-4.2: Open Location in Maps
**Actor:** User
**Trigger:** User clicks message with GPS coordinates
**Preconditions:**
- Message has GPS data
- Message displayed in chat

**Flow:**
1. User clicks message bubble
2. System creates Google Maps intent with coordinates
3. If Google Maps installed:
   - Open Maps app with marker at coordinates
4. Else:
   - Open browser with Google Maps web URL
5. If error occurs, display "Error opening location" toast

**Postconditions:**
- Map application opened showing coordinates

---

## 5. Protocol & Serialization

### UC-5.1: Serialize Text Message
**Actor:** System
**Trigger:** Sending message (UC-3.1)
**Preconditions:**
- Valid text (1-50 chars, supported charset)
- Optional GPS coordinates

**Flow:**
1. System validates text length (max 50)
2. System converts text to uppercase
3. System packs text using 6-bit encoding:
   - Each character → 6 bits
   - 50 chars × 6 bits = 300 bits = 38 bytes max
4. System creates byte array:
   - [0]: Message type (0x01)
   - [1]: Sequence number
   - [2]: Character count
   - [3]: Packed byte length
   - [4..N]: Packed text bytes
   - [N+1]: hasGPS flag (0 or 1)
   - [N+2..N+9]: GPS data if present (lat:4 bytes, lon:4 bytes, little-endian)
5. System returns serialized byte array

**Postconditions:**
- Binary message ready for BLE transmission
- Size: 5 + packed text + (9 if GPS) bytes

---

### UC-5.2: Deserialize Received Message
**Actor:** System
**Trigger:** BLE notification received (UC-4.1)
**Preconditions:**
- Valid byte array received

**Flow:**
1. System reads message type byte
2. Based on type:
   - **0x01 (Text):**
     - Read seq, charCount, packedLen
     - Read packed text bytes
     - Unpack using 6-bit decoding
     - Read hasGPS flag
     - If hasGPS, read lat/lon (little-endian)
     - Return TextMessage object
   - **0x02 (ACK):**
     - Read sequence number
     - Return AckMessage object

**Postconditions:**
- Message object created (TextMessage or AckMessage)

**Error Scenarios:**
- Unknown type: Throw exception
- Insufficient data: Throw exception
- Invalid 6-bit value: Throw exception

---

### UC-5.3: Validate Character Support
**Actor:** System
**Trigger:** User typing message, before sending
**Preconditions:**
- Message text entered

**Flow:**
1. System checks each character against charset
2. Charset: Space, A-Z, 0-9, .,!?-:;'"@#$%&*()[]{}=+/<>_
3. Lowercase letters automatically converted to uppercase
4. If any character not in charset, return false

**Postconditions:**
- Boolean result indicating validity

---

## 6. Chat UI Display

### UC-6.1: Display Chat Message
**Actor:** System
**Trigger:** Message sent or received
**Preconditions:**
- Message created (sent or received)

**Flow:**
1. System creates ChatMessage object with:
   - Text content
   - Sent/received flag
   - Timestamp
   - Sequence number
   - GPS data (optional)
   - ACK status (PENDING for sent, NONE for received)
2. System adds to RecyclerView adapter
3. System displays message bubble:
   - **Sent (right-aligned, green):**
     - Show timestamp
     - Show ACK status icon (⏱ pending, ✓ delivered)
     - Make clickable if has GPS
   - **Received (left-aligned, white with border):**
     - Show timestamp
     - No ACK icon
     - Make clickable if has GPS
4. System auto-scrolls to latest message

**Postconditions:**
- Message visible in chat interface
- RecyclerView scrolled to bottom

---

### UC-6.2: Update ACK Status Indicator
**Actor:** System
**Trigger:** ACK received (UC-3.2) or timeout (UC-3.3)
**Preconditions:**
- Sent message exists in chat

**Flow:**
1. System finds message by sequence number
2. System updates ackStatus field
3. System notifies RecyclerView adapter
4. System updates icon:
   - PENDING: ⏱ (clock, yellow/orange)
   - DELIVERED: ✓ (checkmark, green)

**Postconditions:**
- Message displays updated ACK status

---

### UC-6.3: Auto-Scroll Chat
**Actor:** System
**Trigger:** New message added
**Preconditions:**
- Message added to adapter

**Flow:**
1. System triggers scroll callback
2. System posts scroll action to RecyclerView
3. System smooth scrolls to last position

**Postconditions:**
- Chat scrolled to show latest message

---

## 7. Permissions Management

### UC-7.1: Request BLE Permissions
**Actor:** User
**Trigger:** App startup
**Preconditions:**
- Android 12+ requires BLUETOOTH_SCAN and BLUETOOTH_CONNECT
- Android <12 requires ACCESS_FINE_LOCATION

**Flow:**
1. System checks if permissions granted
2. If not granted, display permission request dialog
3. User grants or denies permissions
4. If granted, proceed to start BLE scan (UC-1.1)
5. If denied, display "Permissions required" toast

**Postconditions:**
- Permissions granted OR user notified of requirement

---

### UC-7.2: Request Location Permissions
**Actor:** User
**Trigger:** App startup, GPS request
**Preconditions:**
- ACCESS_FINE_LOCATION needed for GPS

**Flow:**
1. System checks if permission granted
2. If not granted, display permission request dialog
3. User grants or denies
4. If granted, allow GPS operations
5. If denied, GPS features unavailable

**Postconditions:**
- Location permissions granted OR GPS disabled

---

### UC-7.3: Handle Location Services Disabled
**Actor:** User
**Trigger:** Location services disabled in system settings
**Preconditions:**
- BLE permissions granted

**Flow:**
1. System detects location services disabled
2. System displays "Location services disabled (required for BLE)" status
3. System shows toast: "Please enable Location services"
4. System registers BroadcastReceiver for location provider changes
5. When user enables location in settings:
   - System receives broadcast
   - System displays "Location enabled! Scanning..." toast
   - System initiates BLE scan (UC-1.1)

**Postconditions:**
- App waits for location services to be enabled

---

## 8. Special UI Behaviors

### UC-8.1: Dismiss Keyboard on Send
**Actor:** System
**Trigger:** User sends message or presses Done/Send on keyboard
**Preconditions:**
- Keyboard visible

**Flow:**
1. User triggers send or keyboard action
2. System hides soft keyboard
3. System clears focus from EditText

**Postconditions:**
- Keyboard hidden
- Input unfocused

---

### UC-8.2: Show Status Bar with Dark Icons
**Actor:** System
**Trigger:** App startup
**Preconditions:**
- Activity created

**Flow:**
1. System ensures status bar visible
2. System sets light status bar appearance (dark icons)
3. System sets Action Bar title to "LoRa Chat"

**Postconditions:**
- Status bar configured for light theme

---

## Summary Statistics

- **Total Use Cases:** 31
- **Feature Areas:** 8
  1. BLE Connection (5 use cases)
  2. GPS/Location (3 use cases)
  3. Message Sending (4 use cases)
  4. Message Receiving (2 use cases)
  5. Protocol/Serialization (3 use cases)
  6. Chat UI (3 use cases)
  7. Permissions (3 use cases)
  8. Special UI (2 use cases)

---

## Modern Architecture Mapping

These use cases will be implemented using:

### Domain Layer (Use Cases)
- `SendMessageUseCase`
- `ReceiveMessageUseCase`
- `ConnectBleUseCase`
- `RequestGpsUpdateUseCase`
- `SerializeMessageUseCase`
- `DeserializeMessageUseCase`

### Data Layer (Repositories)
- `BleRepository` - Manages BLE scanning, connection, GATT operations
- `LocationRepository` - Manages GPS/location updates
- `MessageRepository` - Manages message history (could add persistence later)
- `ProtocolRepository` - Handles serialization/deserialization

### UI Layer (Compose + ViewModels)
- `MainViewModel` - Orchestrates use cases, manages UI state
- `ChatScreen` - Main chat UI with Compose
- `MessageBubble` - Composable for individual messages
- `ConnectionStatus` - Composable for BLE status display
- `GpsDisplay` - Composable for GPS coordinates

### State Management
- `ChatUiState` (StateFlow) - Messages list, connection status, GPS display
- `MessageSendState` (StateFlow) - Send button enabled, ACK waiting
- `ConnectionState` (StateFlow) - BLE connection status

---

## Notes for Implementation

1. **Coroutines**: All BLE and GPS operations should use coroutines
2. **Flow/StateFlow**: Replace LiveData with Flow/StateFlow
3. **Dependency Injection**: Use Hilt for DI
4. **Testing**: Each use case should have unit tests
5. **Error Handling**: Use sealed classes for Result types
6. **Lifecycle**: Proper lifecycle handling with Compose
