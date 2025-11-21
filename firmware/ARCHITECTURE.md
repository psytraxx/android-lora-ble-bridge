# Firmware Architecture

**Version:** 3.0 (Unified Multi-Platform)
**Last Updated:** November 2025

## Overview

The firmware uses a **trait-based architecture** that supports multiple hardware platforms from a single codebase:

- **Single `unified_main.cpp`** - One entry point for all platforms
- **Platform traits** - Compile-time polymorphism (no virtual functions)
- **Zero runtime overhead** - All platform selection done at compile-time
- **Supported platforms:** ESP32, nRF52
- **Supported radios:** SX1262 (autonomous duty cycle), SX1278 (continuous RX)

## Platform Support

| Platform | Architecture | Task Model | BLE Stack | Power Management |
|----------|--------------|-----------|-----------|------------------|
| ESP32 | Xtensa/RISC-V | FreeRTOS Tasks | NimBLE | Deep sleep, light sleep |
| nRF52 | ARM Cortex-M4 | Loop-based | Arduino BLE | SoftDevice power modes |

## Unified Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    unified_main.cpp                          │
│              (Single entry point for all platforms)          │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────┐
│                   PlatformTraits.h                           │
│     (Compile-time platform selection via #ifdef)             │
└─────┬─────────────────────────────────────┬─────────────────┘
      │                                     │
      ▼                                     ▼
┌──────────────────┐              ┌──────────────────┐
│ ESP32 Platform   │              │ nRF52 Platform   │
│                  │              │                  │
│ • FreeRTOS Tasks │              │ • Loop-based     │
│ • NimBLE         │              │ • Arduino BLE    │
│ • SX1262/SX1278  │              │ • SX1262         │
└──────────────────┘              └──────────────────┘
```

## Platform-Specific Architectures

### nRF52 Loop-Based Architecture

The nRF52 implementation uses a traditional Arduino loop pattern with non-blocking state machines:

**Main Loop Flow:**
```
setup()
  ├─ Initialize serial, LED, power
  ├─ Create managers (BLE, LoRa, Storage, Activity)
  ├─ Initialize BLE advertising
  └─ Initialize LoRa receiver

loop()
  ├─ Feed watchdog
  ├─ Process LoRa events (non-blocking)
  ├─ Process BLE→LoRa queue
  ├─ Process LoRa→BLE queue
  ├─ Update battery level (periodic)
  └─ Check power/activity timeouts
```

**Characteristics:**
- **Non-blocking:** All operations use polling, no blocking calls
- **Single-threaded:** No task switching overhead
- **Event-driven:** Managers maintain internal state machines
- **Lower RAM usage:** No FreeRTOS task stacks
- **Simpler debugging:** Sequential execution model

### ESP32 Task-Based Architecture

The ESP32 implementation uses FreeRTOS tasks for true concurrent operation:

**Key Advantages:**
- **Parallel processing:** BLE and LoRa operations run concurrently
- **Efficient sleeping:** Tasks sleep on events, not polling
- **Priority-based:** Time-critical LoRa operations get higher priority
- **Scalable:** Easy to add new concurrent operations

## ESP32 Task Communication Diagram

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

## Platform Traits System

The platform traits provide compile-time polymorphism without runtime overhead:

```cpp
// In unified_main.cpp
#if defined(ARDUINO_ARCH_ESP32)
#include "esp32/PlatformTraits.h"
using Platform = ESP32PlatformTraits;
#elif defined(ARDUINO_ARCH_NRF52)
#include "nrf52/PlatformTraits.h"
using Platform = NRF52PlatformTraits;
#endif

// Platform-specific types resolved at compile-time
static typename Platform::BLEManager *bleManager = nullptr;
static typename Platform::LoRaManager *loraManager = nullptr;
```

**Trait Types:**

| Trait | ESP32 Type | nRF52 Type |
|-------|-----------|-----------|
| BLEManager | ESP32 NimBLE impl | nRF52 Arduino BLE impl |
| LoRaManager | FreeRTOS task-based | Loop-based polling |
| PowerManager | Deep sleep capable | SoftDevice power modes |
| StorageManager | Preferences | Flash storage |
| ActivityManager | State tracking | State tracking |

**Static Methods (no object instance required):**
```cpp
Platform::initializeWatchdog();
Platform::initializePower();
Platform::initializeLED();
Platform::ledOn();
Platform::ledOff();
```

**Benefits:**
- ✅ **Zero overhead:** No virtual function calls
- ✅ **Compile-time selection:** Wrong platform code never compiled
- ✅ **Type safety:** Compiler catches platform mismatches
- ✅ **Code reuse:** Single main.cpp for all platforms
- ✅ **Maintainability:** Platform differences isolated to trait files

## ESP32 Task Stack Sizes

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

### ESP32 Resources

**Flash Memory:**
- **Firmware:** ~400-500 KB
- **Available:** 8 MB (typical board)
- **Usage:** ~6-7% of available flash

**RAM:**
- **Static allocations:** ~36 KB
- **Task stacks:** ~10 KB (3 tasks)
- **Queues:** ~5 KB (message queues)
- **Total:** ~51 KB (15.6% of 327 KB)
- **Available heap:** ~250+ KB

### nRF52 Resources

**Flash Memory:**
- **Firmware:** ~300-400 KB
- **Available:** 1 MB
- **Usage:** ~30-40% of available flash

**RAM:**
- **Static allocations:** ~20-30 KB
- **No task stacks:** Loop-based (lower overhead)
- **Message queues:** ~3 KB
- **Total:** ~30-40 KB (12-16% of 256 KB)
- **Available heap:** ~200+ KB

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

## Platform Selection Guide

**Choose ESP32 if you need:**
- ✅ Maximum concurrent performance
- ✅ Deep sleep with wake-on-LoRa
- ✅ More available RAM/Flash
- ✅ Built-in WiFi capability (future use)
- ✅ Support for both SX1262 and SX1278 radios

**Choose nRF52 if you need:**
- ✅ Simpler debugging (single-threaded)
- ✅ Lower power consumption in active mode
- ✅ Smaller form factor (XIAO board)
- ✅ Lower cost
- ✅ USB HID/Serial support

## Migration from Previous Versions

**From v2.0 (ESP32-only FreeRTOS):**
- ✅ Code still works - now in `esp32/` directory
- ✅ Added nRF52 support via platform traits
- ✅ Single `unified_main.cpp` replaces separate main files
- ✅ No breaking changes to ESP32 implementation

**Key Changes:**
- Moved platform-specific code to `esp32/` and `nrf52/` directories
- Added `PlatformTraits.h` for compile-time platform selection
- Unified main entry point supports both platforms
- Protocol implementation shared across platforms

**Last Updated:** 2025-11-21
**Architecture Version:** 3.0 (Unified Multi-Platform)
