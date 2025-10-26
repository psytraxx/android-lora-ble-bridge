# LoRa Bridge - State Diagrams

This document contains state diagrams for the LoRa Bridge application using Mermaid syntax.

---

## 1. BLE Connection State Diagram

```mermaid
stateDiagram-v2
    [*] --> Disconnected: App Start

    Disconnected --> CheckingPermissions: startScan()
    CheckingPermissions --> PermissionsDenied: Permissions Not Granted
    CheckingPermissions --> CheckingLocation: Permissions OK

    PermissionsDenied --> CheckingPermissions: User Grants Permissions
    PermissionsDenied --> [*]: App Exit

    CheckingLocation --> LocationDisabled: Location Services Off
    CheckingLocation --> Scanning: Location Services On

    LocationDisabled --> Scanning: User Enables Location
    LocationDisabled --> Disconnected: Cancel

    Scanning --> DeviceFound: ESP32S3-LoRa Discovered
    Scanning --> ScanTimeout: 15s Elapsed
    Scanning --> Disconnected: stopScan()

    ScanTimeout --> Disconnected: Retry/Cancel

    DeviceFound --> Connecting: connectToDevice()

    Connecting --> NegotiatingMTU: GATT Connected
    Connecting --> ConnectionFailed: Connection Error

    ConnectionFailed --> Disconnected: Cleanup

    NegotiatingMTU --> DiscoveringServices: MTU Negotiated
    NegotiatingMTU --> DiscoveringServices: MTU Failed (continue anyway)

    DiscoveringServices --> EnablingNotifications: Services Found
    DiscoveringServices --> ServiceDiscoveryFailed: Services Not Found

    ServiceDiscoveryFailed --> Disconnected: Cleanup

    EnablingNotifications --> Connected: Notifications Enabled
    EnablingNotifications --> NotificationSetupFailed: CCCD Write Failed

    NotificationSetupFailed --> Disconnected: Cleanup

    Connected --> SendingMessage: User Sends Message
    Connected --> DisconnectingInactive: 30s Inactivity Timeout
    Connected --> Disconnecting: User Disconnects
    Connected --> Disconnected: Device Disconnected

    SendingMessage --> WaitingForAck: Message Sent
    SendingMessage --> Connected: Send Failed (retry)

    WaitingForAck --> Connected: ACK Received
    WaitingForAck --> Connected: 5s Timeout
    WaitingForAck --> DisconnectingInactive: 30s Since Last Activity

    DisconnectingInactive --> Disconnected: Disconnect Complete
    Disconnecting --> Disconnected: Disconnect Complete

    Disconnected --> CheckingPermissions: Reconnect Request

    note right of Connected
        Ready to send/receive
        messages
    end note

    note right of WaitingForAck
        Send button disabled
        Message shows pending
    end note
```

---

## 2. Message Lifecycle State Diagram

```mermaid
stateDiagram-v2
    [*] --> Composing: User Types Message

    Composing --> Validating: User Clicks Send
    Composing --> Composing: Text Input Changes

    Validating --> InvalidInput: Text Empty/Invalid Chars
    Validating --> CheckingConnection: Text Valid

    InvalidInput --> Composing: User Fixes Input

    CheckingConnection --> Reconnecting: BLE Disconnected
    CheckingConnection --> RequestingGPS: BLE Connected

    Reconnecting --> Queued: Connection Initiated

    Queued --> RequestingGPS: BLE Connected
    Queued --> Composing: Connection Failed

    RequestingGPS --> Serializing: GPS Update Requested

    Serializing --> Sending: Binary Message Created

    Sending --> PendingAck: BLE Write Success
    Sending --> RetryingSend: BLE Write Failed

    RetryingSend --> Sending: Retry After 1s
    RetryingSend --> SendFailed: Max Retries

    SendFailed --> DisplayedPending: Show in Chat

    PendingAck --> DisplayedPending: Add to Chat UI

    DisplayedPending --> Delivered: ACK Received
    DisplayedPending --> TimedOut: 5s No ACK
    DisplayedPending --> DisplayedPending: Still Waiting

    Delivered --> [*]: Message Complete
    TimedOut --> [*]: Message Complete (unconfirmed)

    note right of PendingAck
        Send button disabled
        Waiting for ACK
    end note

    note right of Delivered
        Shows ✓ checkmark
        Send button enabled
    end note

    note right of TimedOut
        Shows ⏱ clock
        Send button enabled
    end note
```

---

## 3. Message Reception State Diagram

```mermaid
stateDiagram-v2
    [*] --> Listening: BLE Connected

    Listening --> NotificationReceived: BLE TX Notification
    Listening --> Disconnected: BLE Disconnected

    NotificationReceived --> Deserializing: Parse Byte Array

    Deserializing --> TextMessageReceived: Type = 0x01
    Deserializing --> AckMessageReceived: Type = 0x02
    Deserializing --> DeserializationError: Invalid Format

    DeserializationError --> Listening: Log Error

    TextMessageReceived --> UnpackingText: Extract 6-bit Encoded Text

    UnpackingText --> CheckingGPS: Text Unpacked

    CheckingGPS --> ExtractingGPS: hasGPS = true
    CheckingGPS --> DisplayingMessage: hasGPS = false

    ExtractingGPS --> DisplayingMessage: GPS Coordinates Extracted

    DisplayingMessage --> AddingToChat: Create ChatMessage Object

    AddingToChat --> DisplayedInChat: RecyclerView Updated

    DisplayedInChat --> Listening: Ready for Next Message
    DisplayedInChat --> OpeningMaps: User Clicks Message (if GPS)

    OpeningMaps --> Listening: Maps Opened

    AckMessageReceived --> UpdatingAckStatus: Find Message by Seq

    UpdatingAckStatus --> Listening: UI Updated with ✓

    Disconnected --> [*]: Cleanup

    note right of DisplayedInChat
        Message shows on left
        White background
        Clickable if has GPS
    end note
```

---

## 4. GPS/Location State Diagram

```mermaid
stateDiagram-v2
    [*] --> NoPermission: App Start

    NoPermission --> Idle: Permissions Granted
    NoPermission --> [*]: Permissions Denied

    Idle --> RequestingUpdate: User Sends Message
    Idle --> GettingLastKnown: Display GPS / App Resume

    GettingLastKnown --> CheckingCache: getLastKnownLocation()

    CheckingCache --> ReturningCached: Cached < 1min old
    CheckingCache --> QueryingFused: No/Old Cache

    ReturningCached --> Idle: Location Available

    QueryingFused --> ReturningFused: Fused Available
    QueryingFused --> QueryingGPS: Fused Unavailable

    ReturningFused --> Idle: Location Available

    QueryingGPS --> ReturningGPS: GPS Available
    QueryingGPS --> QueryingNetwork: GPS Unavailable

    ReturningGPS --> Idle: Location Available

    QueryingNetwork --> ReturningNetwork: Network Available
    QueryingNetwork --> NoFix: No Providers

    ReturningNetwork --> Idle: Location Available

    NoFix --> Idle: No Location

    RequestingUpdate --> RegisteringListener: requestSingleUpdate()

    RegisteringListener --> WaitingForUpdate: Listeners Registered

    WaitingForUpdate --> UpdateReceived: GPS/Network Update
    WaitingForUpdate --> UpdateTimeout: Timeout

    UpdateReceived --> RemovingListener: Update Current Location
    UpdateTimeout --> RemovingListener: Proceed Without Update

    RemovingListener --> Idle: Listeners Removed

    note right of WaitingForUpdate
        Both GPS and Network
        listeners active
    end note

    note right of RemovingListener
        Auto-cleanup to prevent
        memory leaks
    end note
```

---

## 5. Overall Application State Diagram

```mermaid
stateDiagram-v2
    [*] --> Initializing: App Launch

    Initializing --> RequestingPermissions: onCreate()

    RequestingPermissions --> PermissionsDenied: User Denies
    RequestingPermissions --> Idle: All Permissions Granted

    PermissionsDenied --> Idle: User Grants Later
    PermissionsDenied --> [*]: App Exit

    Idle --> Scanning: Auto-scan for BLE
    Idle --> Background: App Paused

    Scanning --> Connecting: Device Found
    Scanning --> Idle: Scan Failed/Timeout

    Connecting --> Ready: Connection Success
    Connecting --> Idle: Connection Failed

    Ready --> Composing: User Types Message
    Ready --> Ready: GPS Update Received
    Ready --> InactiveConnected: No Activity

    Composing --> Sending: User Sends
    Composing --> Ready: User Clears Input

    Sending --> AwaitingAck: Message Sent
    Sending --> Ready: Send Failed

    AwaitingAck --> Ready: ACK/Timeout
    AwaitingAck --> InactiveConnected: 30s Elapsed

    InactiveConnected --> Idle: Auto-disconnect
    InactiveConnected --> Composing: User Activity

    Ready --> ReceivingMessage: BLE Notification

    ReceivingMessage --> Ready: Message Displayed

    Ready --> Idle: Manual Disconnect
    Ready --> Idle: Device Disconnected

    Background --> Validating: App Resumed

    Validating --> Ready: Connection Valid
    Validating --> Idle: Connection Invalid

    Idle --> [*]: App Destroyed
    Ready --> [*]: App Destroyed

    note right of Ready
        Connected & Idle
        - Can send messages
        - Can receive messages
        - GPS displayed
    end note

    note right of AwaitingAck
        - Send button disabled
        - Message pending (⏱)
        - Waiting for confirmation
    end note
```

---

## 6. UI State Machine

```mermaid
stateDiagram-v2
    [*] --> Loading: Screen Created

    Loading --> CheckingState: ViewModel Initialized

    CheckingState --> DisconnectedState: BLE Disconnected
    CheckingState --> ConnectedState: BLE Connected

    DisconnectedState --> ScanningState: Scanning Started
    DisconnectedState --> DisconnectedState: Status Updates

    ScanningState --> ConnectingState: Device Found
    ScanningState --> DisconnectedState: Scan Timeout

    ConnectingState --> ConnectedState: Connection Success
    ConnectingState --> DisconnectedState: Connection Failed

    ConnectedState --> SendingState: User Sends Message
    ConnectedState --> ReceivingState: Message Received
    ConnectedState --> DisconnectedState: Disconnected

    SendingState --> WaitingState: Message Sent
    SendingState --> ConnectedState: Send Failed

    WaitingState --> ConnectedState: ACK Received
    WaitingState --> ConnectedState: Timeout

    ReceivingState --> ConnectedState: Message Displayed

    DisconnectedState --> Background: App Minimized
    ConnectedState --> Background: App Minimized

    Background --> CheckingState: App Resumed

    note right of DisconnectedState
        UI State:
        - Connection status: "❌ Disconnected"
        - Send button: Disabled
        - GPS: "No GPS fix" or last known
    end note

    note right of ConnectedState
        UI State:
        - Connection status: "✅ Ready to send!"
        - Send button: Enabled (if text)
        - GPS: Current coordinates
        - Messages: Scrollable list
    end note

    note right of WaitingState
        UI State:
        - Send button: Disabled
        - Latest message: ⏱ pending
        - Cannot send until ACK/timeout
    end note
```

---

## State Diagram Legend

### Connection Status Icons
- 🔍 Scanning...
- 📡 Connecting...
- 🔗 Negotiating...
- 🔧 Discovering services...
- ✅ Ready to send!
- ❌ Disconnected / Error states

### Message Status Icons
- ⏱ Pending (waiting for ACK)
- ✓ Delivered (ACK received)
- (no icon) Received message

### Colors (for implementation)
- Green: Success states
- Red: Error states
- Yellow/Orange: Warning states
- Blue: Active/processing states
- Gray: Inactive/waiting states

---

## Implementation Notes

1. **State Management**: Use sealed classes for each state machine:
   ```kotlin
   sealed class BleConnectionState {
       object Disconnected : BleConnectionState()
       object Scanning : BleConnectionState()
       object Connecting : BleConnectionState()
       object Connected : BleConnectionState()
       data class Error(val message: String) : BleConnectionState()
   }
   ```

2. **State Transitions**: Emit state changes via StateFlow:
   ```kotlin
   private val _connectionState = MutableStateFlow<BleConnectionState>(Disconnected)
   val connectionState: StateFlow<BleConnectionState> = _connectionState.asStateFlow()
   ```

3. **UI Reactions**: Collect states in Compose:
   ```kotlin
   val connectionState by viewModel.connectionState.collectAsState()
   when (connectionState) {
       is Connected -> ShowConnectedUI()
       is Scanning -> ShowScanningUI()
       // ...
   }
   ```

4. **Testing**: Each state transition should have unit tests verifying:
   - Valid transitions allowed
   - Invalid transitions prevented
   - Side effects triggered correctly
