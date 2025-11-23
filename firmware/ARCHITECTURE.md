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

| Platform | Architecture | Execution Model | BLE Stack | Power Management |
|----------|--------------|----------------|-----------|------------------|
| ESP32 | Xtensa/RISC-V | Loop-based (Arduino) | NimBLE | Deep sleep capable |
| nRF52 | ARM Cortex-M4 | Loop-based (Arduino) | Arduino BLE | SoftDevice power modes |

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
│ • Loop-based     │              │ • Loop-based     │
│ • NimBLE         │              │ • Arduino BLE    │
│ • SX1262/SX1278  │              │ • SX1262         │
└──────────────────┘              └──────────────────┘
```

## Unified Loop-Based Architecture

Both ESP32 and nRF52 implementations use the Arduino `setup()` and `loop()` pattern with non-blocking state machines:

**Main Execution Flow:**
```
setup()
  ├─ Initialize serial, LED, power, watchdog
  ├─ Create managers (BLE, LoRa, Storage, Activity)
  ├─ Configure BLE advertising
  ├─ Configure LoRa receiver
  └─ Register callbacks

loop()
  ├─ Reset watchdog
  ├─ Process LoRa events (RadioLib state machine)
  ├─ Process BLE→LoRa message queue
  ├─ Process LoRa→BLE message queue
  ├─ Update battery level (periodic, every 60s)
  └─ Check activity/power timeouts
```

**Architecture Characteristics:**

| Aspect | Implementation |
|--------|---------------|
| **Execution Model** | Single-threaded, non-blocking |
| **Message Passing** | MessageQueue (simple circular buffer) |
| **State Management** | Manager objects with internal state |
| **Timing** | `millis()` based, no blocking delays |
| **Callbacks** | Function pointers for BLE/LoRa events |
| **RAM Usage** | Low - no task stacks, simple queues |

**Platform Differences:**

| Feature | ESP32 | nRF52 |
|---------|-------|-------|
| BLE Implementation | NimBLE callbacks | Arduino BLE polling |
| BLE Message Handling | Callback-based | Queue polling in loop() |
| Watchdog | ESP32 Task WDT | nRF52 WDT library |
| Power Management | Deep sleep API | SoftDevice power modes |

## Message Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Android App (BLE Client)                         │
└────────────────────────────────┬────────────────────────────────────────┘
                                 │ BLE Write/Notify
                                 ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          BLEManager                                      │
│                                                                          │
│  ESP32: NimBLE callbacks        │  nRF52: Arduino BLE polling           │
│  ┌────────────────────────┐     │  ┌────────────────────────┐           │
│  │ onWrite() callback     │     │  │ loop() polls BLE       │           │
│  │   → handleBleMessage() │     │  │   → queue.push(msg)    │           │
│  └────────────────────────┘     │  └────────────────────────┘           │
│                                                                          │
└──────────────────────────────┬───────────────────────────────────────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │  bleToLoraQueue     │  (MessageQueue - circular buffer)
                    └─────────┬───────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          loop() - Main Execution                         │
│                                                                          │
│  1. Watchdog reset                                                      │
│  2. LoRaManager::process()  ─────────┐                                  │
│  3. Process bleToLoraQueue           │                                  │
│     └─> LoRaManager::send()          │                                  │
│  4. Process loraToBleQueue           │  RadioLib                        │
│     └─> BLEManager::sendMessage()    │  State Machine                   │
│  5. Update battery (periodic)         │  (TX/RX/Idle)                    │
│  6. Check timeouts                    │                                  │
│                                       │                                  │
└───────────────────────────────────────┼──────────────────────────────────┘
                                        │
                                        ▼
                              LoRa Radio Hardware
                                        │
                                        │ Interrupt (DIO0/DIO1)
                                        ▼
                              LoRa RX Callback
                                        │
                                        ▼
                            loraToBleQueue.push(msg)
                                        │
                                        ▼
                            (Processed in next loop())
```

## Message Flow Examples

### Example 1: Android → LoRa Message Flow

```
1. Android writes to BLE RX characteristic
        ↓
2. BLE callback (ESP32) or queue push (nRF52)
        ↓
3. bleToLoraQueue.push(msg)
        ↓
4. loop() processes queue
        ↓
5. LoRaManager::send(msg)
        ↓
6. RadioLib transmits over LoRa
        ↓
7. Remote device receives
```

### Example 2: LoRa → Android Message Flow

```
1. LoRa radio receives packet (interrupt)
        ↓
2. LoRaManager callback triggered
        ↓
3. loraToBleQueue.push(msg)
        ↓
4. loop() processes queue on next iteration
        ↓
5. BLEManager::sendMessage(msg)
        ↓
6. Android receives BLE notification
```

### Example 3: Message Queuing

```cpp
// Simple circular buffer queue
class MessageQueue {
    Message buffer[QUEUE_SIZE];
    uint8_t head, tail, count;

    bool push(const Message& msg) {
        if (count >= QUEUE_SIZE) return false;
        buffer[head] = msg;
        head = (head + 1) % QUEUE_SIZE;
        count++;
        return true;
    }

    bool pop(Message& msg) {
        if (count == 0) return false;
        msg = buffer[tail];
        tail = (tail + 1) % QUEUE_SIZE;
        count--;
        return true;
    }
};
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

## Interrupt Handling

LoRa radio uses hardware interrupts for RX/TX events:

```cpp
// Interrupt handler (IRAM_ATTR for ESP32 fast execution)
void IRAM_ATTR onLoRaInterrupt() {
    // Minimal work in ISR
    loraManager->setFlag();  // Set volatile flag
}

// Processing in main loop()
void loop() {
    if (loraManager->hasFlag()) {
        loraManager->process();  // Handle RX/TX completion
    }
}
```

**Key Points:**
- **Minimal ISR work:** Just set a flag, return quickly
- **Processing in loop():** Actual packet handling happens in main context
- **No blocking:** All operations non-blocking to avoid watchdog timeouts
- **Platform-specific:** ESP32 uses IRAM_ATTR for fast ISR execution

## Resource Usage

### ESP32 Resources

**Flash Memory:**
- **Firmware:** ~400-500 KB
- **Available:** 8 MB (typical board)
- **Usage:** ~6-7% of available flash

**RAM:**
- **Static allocations:** ~30-40 KB
- **Manager objects:** ~5-10 KB (BLE, LoRa, Storage)
- **Message queues:** ~2 KB (circular buffers)
- **NimBLE stack:** ~20-30 KB
- **Total:** ~60-80 KB (18-24% of 327 KB)
- **Available heap:** ~240+ KB

### nRF52 Resources

**Flash Memory:**
- **Firmware:** ~300-400 KB
- **Available:** 1 MB
- **Usage:** ~30-40% of available flash

**RAM:**
- **Static allocations:** ~20-30 KB
- **Manager objects:** ~5-10 KB
- **Message queues:** ~2 KB
- **Arduino BLE stack:** ~15-25 KB
- **SoftDevice:** ~10-15 KB
- **Total:** ~50-80 KB (20-31% of 256 KB)
- **Available heap:** ~180+ KB

### CPU Usage (Loop-Based)
- **Idle:** Minimal (loop() yields to watchdog)
- **BLE active:** Fast callbacks/polling (~5-10% avg)
- **LoRa TX/RX:** RadioLib state machine (~10-20% during transmission)
- **Overall:** <20% average, efficient power management

## Timing Characteristics

| Event | Latency | Notes |
|-------|---------|-------|
| LoRa RX interrupt → Flag | <1 ms | Hardware interrupt |
| BLE write → Callback | <10 ms | Stack processing |
| Message BLE→LoRa | One loop iteration | Typically <10ms |
| Message LoRa→BLE | One loop iteration | Typically <10ms |
| Loop iteration | 1-10 ms | Depends on activity |
| Watchdog timeout | 5-10 s | Platform watchdog |

## Debugging Tips

### Monitor Loop Performance

```cpp
void loop() {
    unsigned long start = millis();

    // ... normal loop code ...

    unsigned long duration = millis() - start;
    if (duration > 100) {
        Serial.printf("Slow loop: %lu ms\n", duration);
    }
}
```

### Monitor Queue Usage

```cpp
// Check queue fill levels
Serial.printf("BLE→LoRa queue: %d/%d\n",
    bleToLoraQueue.getCount(),
    bleToLoraQueue.getCapacity());
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
