# FreeRTOS Multi-Task Architecture

## Task Communication Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Android App (BLE Client)                         │
└────────────────────────────────┬────────────────────────────────────────┘
                                 │ BLE
                                 ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          BLEManager (NimBLE)                             │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ MyServerCallbacks::onConnect()                                   │   │
│  │   → onBleConnected()                                            │   │
│  │   → appController.onBleConnected()                              │   │
│  │   → BleTask::notifyConnectionChange(true)                       │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ MyCharacteristicCallbacks::onWrite()                            │   │
│  │   → onMessageReceived()                                         │   │
│  │   → xQueueSend(bleToLoraQueue)                                  │   │
│  │   → LoraTask::notifyMessageQueued()                             │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└──────┬────────────────────────────────────┬─────────────────────────────┘
       │                                    │
       │ bleToLoraQueue                     │ BLE connection events
       │                                    │
       ▼                                    ▼
┌─────────────────────────────┐    ┌─────────────────────────────┐
│       LoRa Task             │    │       BLE Task              │
│    (Priority 4 - Highest)   │    │    (Priority 3 - Medium)    │
│                             │    │                             │
│  • Process BLE→LoRa queue   │    │  • Process LoRa→BLE queue   │
│  • LoRa TX/RX events        │    │  • Forward buffered msgs    │
│  • Send ACKs                │    │  • Connection handling      │
│  • ISR notifications        │    │  • LED blinking (RX)        │
│                             │    │                             │
│  Wakes on:                  │    │  Wakes on:                  │
│  ✓ ISR (RX/TX interrupt)    │    │  ✓ Message notification     │
│  ✓ BLE message queued       │    │  ✓ Connection change        │
│  ✓ Timeout (50ms)           │    │  ✓ Timeout (100ms)          │
└──────┬──────────────────────┘    └─────────┬───────────────────┘
       │                                     │
       │ loraToBleQueue                      │ State queries
       │                                     │
       └──────────┬──────────────────────────┘
                  │
                  ▼
       ┌─────────────────────────────────────┐
       │    ApplicationController            │
       │    (Pure State Machine)             │
       │                                     │
       │  • Mutex-protected state            │
       │  • Timestamp management             │
       │  • Thread-safe queries              │
       │                                     │
       │  States:                            │
       │  - DISCONNECTED_ADVERTISING         │
       │  - CONNECTED_ACTIVE                 │
       │                                     │
       │  Accessed by all tasks via:         │
       │  • getState()                       │
       │  • isConnected()                    │
       │  • getAdvertisingDuration()         │
       │  • getInactivityDuration()          │
       │  • notifyActivity()                 │
       └──────────────┬──────────────────────┘
                      │
                      │ State queries
                      │
                      ▼
       ┌─────────────────────────────────────┐
       │       Power Task                    │
       │    (Priority 2 - Lowest)            │
       │                                     │
       │  • Monitor advertising timeout      │
       │  • Monitor inactivity timeout       │
       │  • Trigger deep sleep               │
       │  • Trigger BLE disconnect           │
       │                                     │
       │  Wakes on:                          │
       │  ✓ Periodic timeout (1s)            │
       └─────────────────────────────────────┘
```

## Message Flow Examples

### Example 1: Android → LoRa Message Flow

```
┌──────────────┐
│ Android App  │
└──────┬───────┘
       │ 1. Write to RX characteristic
       ▼
┌──────────────────────────────────────────────┐
│ MyCharacteristicCallbacks::onWrite()         │
│   → BLEManager::onMessageReceived()          │
│   → Deserialize message                      │
│   → xQueueSend(bleToLoraQueue, msg, 0)       │
│   → LoraTask::notifyMessageQueued() ◄────────┼─── 2. Task notification
└──────────────────────────────────────────────┘
                                                │
       3. LoRa Task wakes up ◄──────────────────┘
       │
       ▼
┌──────────────────────────────────────────────┐
│ LoRa Task                                    │
│   → xQueueReceive(bleToLoraQueue, &msg)      │
│   → msg.serialize(buf)                       │
│   → loraManager->startTransmit(buf, len)     │
│   → LED blink (TX)                           │
│   → appController->notifyActivity()          │
└──────────────────────────────────────────────┘
       │
       ▼
   LoRa Radio TX
```

### Example 2: LoRa → Android Message Flow

```
   LoRa Radio RX (ISR)
       │
       ▼
┌──────────────────────────────────────────────┐
│ LoRaManager::onReceiveISR()                  │
│   → Set state to STATE_PACKET_RECEIVED       │
│   → vTaskNotifyGiveFromISR(loraTaskHandle) ◄─┼─── 1. ISR notification
└──────────────────────────────────────────────┘
                                                │
       2. LoRa Task wakes up ◄──────────────────┘
       │
       ▼
┌──────────────────────────────────────────────┐
│ LoRa Task                                    │
│   → loraManager->process()                   │
│   → receiveCallback(packet)                  │
│   → handlePacketReceived(packet)             │
│   → msg.deserialize(packet.buffer)           │
│   → Send ACK (if text message)               │
│   → xQueueSend(loraToBleQueue, &msg)         │
│   → BleTask::notifyMessageReceived() ◄───────┼─── 3. Task notification
│   → appController->notifyActivity()          │
└──────────────────────────────────────────────┘
                                                │
       4. BLE Task wakes up ◄───────────────────┘
       │
       ▼
┌──────────────────────────────────────────────┐
│ BLE Task                                     │
│   → xQueueReceive(loraToBleQueue, &msg)      │
│   → Check if connected & Android ready       │
│   → bleManager->sendMessage(msg)             │
│   → LED blink (RX)                           │
└──────────────────────────────────────────────┘
       │
       ▼
┌──────────────┐
│ Android App  │ (receives notification)
└──────────────┘
```

### Example 3: BLE Connection Event Flow

```
   BLE Client Connect
       │
       ▼
┌──────────────────────────────────────────────┐
│ MyServerCallbacks::onConnect()               │
│   → bleManager->onConnected(connHandle)      │
│   → connectCallback()  (registered in main)  │
└──────┬───────────────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────┐
│ onBleConnected() (in main.cpp)               │
│   → appController.onBleConnected()           │
│   → BleTask::notifyConnectionChange(true)    │
└──────┬───────────────────────────────────────┘
       │
       ▼
   ┌────────────────────────────┐
   │ ApplicationController       │
   │   → State machine lock      │
   │   → State = CONNECTED       │
   │   → Timestamp updates       │
   │   → State machine unlock    │
   └────────────────────────────┘
       │
       ▼
   ┌────────────────────────────┐
   │ BLE Task wakes up          │
   │   → Forward buffered msgs  │
   │     (after 500ms delay)    │
   └────────────────────────────┘
```

### Example 4: Power Management Timeout Flow

```
   Time passes (60 seconds idle)
       │
       ▼
┌──────────────────────────────────────────────┐
│ Power Task (wakes every 1 second)           │
│   → Check appController.getInactivityDuration()
│   → If > 60s: bleManager->disconnect()      │
└──────┬───────────────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────┐
│ MyServerCallbacks::onDisconnect()           │
│   → bleManager->onDisconnected(connHandle)   │
│   → disconnectCallback()                     │
└──────┬───────────────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────┐
│ onBleDisconnected() (in main.cpp)            │
│   → appController.onBleDisconnected()        │
│   → BleTask::notifyConnectionChange(false)   │
└──────┬───────────────────────────────────────┘
       │
       ▼
   ┌────────────────────────────┐
   │ ApplicationController       │
   │   → State = DISCONNECTED    │
   │   → Restart advertising     │
   │     timer                   │
   └────────────────────────────┘
```

## Task Synchronization Primitives

### Mutex (ApplicationController)

```cpp
// All state access is mutex-protected
xSemaphoreTake(stateMutex, portMAX_DELAY);
state = AppState::CONNECTED_ACTIVE;
connectionEstablishedMillis = getCurrentTimeMillis();
xSemaphoreGive(stateMutex);
```

### Task Notifications (Zero Overhead Signaling)

```cpp
// From normal context
void LoraTask::notifyMessageQueued() {
    xTaskNotifyGive(loraTaskHandle);
}

// From ISR context
void LoraTask::notifyPacketReceived() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(loraTaskHandle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// In task loop
ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)); // Wait max 50ms
```

### Queues (Message Passing)

```cpp
// Producer (BLE Manager)
xQueueSend(bleToLoraQueue, &msg, 0); // Non-blocking

// Consumer (LoRa Task)
if (xQueueReceive(bleToLoraQueue, &msg, 0) == pdTRUE) {
    // Process message
}
```

## Task Stack Sizes

Chosen based on worst-case usage analysis:

| Task | Stack Size | Reasoning |
|------|-----------|-----------|
| BLE Task | 4096 bytes | NimBLE callbacks + message processing |
| LoRa Task | 4096 bytes | RadioLib operations + watchdog + serialization |
| Power Task | 2048 bytes | Simple timeout checks, minimal processing |

## Task Priorities

| Task | Priority | Justification |
|------|----------|--------------|
| LoRa Task | 4 (Highest) | Time-critical radio operations, ISR response |
| BLE Task | 3 (Medium) | Important but bufferable |
| Power Task | 2 (Lowest) | 1-second granularity, can be preempted |
| FreeRTOS Idle | 0 | System default |

## Critical Sections

### ISR → Task Communication

LoRa interrupts use ISR-safe notifications:

```cpp
void IRAM_ATTR LoRaManager::onReceiveISR() {
    rxInterruptCount++;
    state = STATE_PACKET_RECEIVED;
    LoraTask::notifyPacketReceived(); // ISR-safe
}
```

### State Machine Access

All ApplicationController methods use mutex:

```cpp
AppState ApplicationController::getState() const {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    AppState currentState = state;
    xSemaphoreGive(stateMutex);
    return currentState;
}
```

## Resource Usage

### Flash Memory
- **Base firmware:** ~705 KB
- **Task overhead:** ~3 KB (0.4%)
- **Total:** ~708 KB (67.6% of 1MB)

### RAM
- **Static allocations:** ~36 KB
- **Task stacks:** ~10 KB (3 tasks)
- **Queues:** ~5 KB (25 messages × 160 bytes)
- **Total:** ~51 KB (15.6% of 327 KB)

### CPU Usage (Estimated)
- **Idle:** <1% (tasks sleep on events)
- **BLE active:** ~10-20% (message processing)
- **LoRa TX/RX:** ~30-40% (radio operations)
- **Power task:** <0.1% (runs 1s intervals)

## Timing Characteristics

| Event | Latency | Notes |
|-------|---------|-------|
| LoRa RX → Task wake | <1 ms | ISR notification |
| BLE write → Task wake | <10 ms | NimBLE stack delay |
| Message BLE→LoRa | <50 ms | Queue + processing |
| Message LoRa→BLE | <100 ms | Android ready check |
| Deep sleep trigger | 30 s ± 1 s | Power task granularity |
| Disconnect trigger | 60 s ± 1 s | Power task granularity |

## Debugging Tips

### Enable Task Stats

```cpp
// In main.cpp setup()
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    vTaskGetRunTimeStats(buffer);
    Serial.println( "Task stats:\n%s", buffer)
#endif
```

### Monitor Stack Usage

```cpp
// In each task loop
UBaseType_t highWater = uxTaskGetStackHighWaterMark(NULL);
Serial.println( "Stack high water mark: %d bytes", highWater)
```

**Last Updated:** 2025-11-13
**Architecture Version:** 2.0 (FreeRTOS Multi-Task)
