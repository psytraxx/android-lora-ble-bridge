# Light Sleep Implementation

## Overview

The ESP32 LoRa-BLE Bridge now supports light sleep mode to reduce power consumption during periods of inactivity. The module automatically enters light sleep after 2 minutes of no activity and can wake up automatically when a LoRa message is received.

## Key Features

### 1. Automatic Light Sleep
- Enters light sleep after **2 minutes** of inactivity
- Activity is tracked for:
  - BLE connections
  - BLE message reception
  - LoRa message reception
  - LoRa message transmission
  - Message forwarding to BLE

### 2. Message Persistence
- Messages received while sleeping or without BLE connection are **stored in RTC memory**
- RTC memory persists across light sleep cycles
- Up to **10 messages** can be stored
- Messages are delivered automatically when:
  - Device wakes up AND
  - BLE connection is established

### 3. Wake-up Sources

#### LoRa Wake-up (GPIO 32)
- Device automatically wakes when a LoRa signal is received
- The LoRa interrupt (DIO0) triggers wake-up
- Message is received, acknowledged, and stored if no BLE connection exists
- Device can return to sleep or stay awake depending on activity

## Power Consumption

### Active Mode
- Full operation: ~80-120 mA
- CPU at 160 MHz
- BLE advertising and LoRa in receive mode

### Light Sleep Mode
- Low power: ~0.8-5 mA (depending on peripherals kept active)
- RTC memory active (stores messages)
- Wake-up sources monitoring
- **Faster wake-up** compared to deep sleep (~few milliseconds)
- **Maintains more state** - faster resume to operation
- **~90-95% power reduction** compared to active mode

## Usage Scenarios

### Scenario 1: Field Device Without Android Connection
1. Device receives LoRa message
2. Device wakes from light sleep (if sleeping)
3. Message is stored in RTC memory
4. Device sends ACK via LoRa
5. After 2 minutes of no activity, returns to light sleep
6. Messages remain in RTC memory
7. When operator connects Android via BLE:
   - BLE connection triggers message delivery
   - All stored messages are delivered to Android

### Scenario 2: Receive Message While Sleeping
1. Device is in light sleep
2. LoRa message arrives → LoRa interrupt triggers
3. Device wakes up
4. Receives and processes message
5. Sends ACK
6. Stores message in RTC memory (no BLE connection)
7. Returns to light sleep after 2 minutes
8. Repeat for subsequent messages (up to 10 stored)

### Scenario 3: Active BLE Session
1. Android connected via BLE
2. Activity timer resets on every message
3. Device stays awake as long as messages flow
4. If no activity for 2 minutes:
   - Any pending messages stored to RTC memory
   - Device enters light sleep
5. Next wake-up delivers all stored messages

## Implementation Details

### RTC Memory Structure
```cpp
typedef struct {
    uint32_t magic;              // 0xDEADBEEF - validates data
    uint8_t messageCount;        // Number of stored messages (0-10)
    Message messages[10];        // Message buffer
    uint32_t wakeupCount;        // Debug: count of wake-ups
    uint32_t lastActivityTime;   // Reserved for future use
} RTC_DATA_ATTR SleepData;
```

### Activity Timer
- Resets on:
  - BLE connection established
  - BLE message received
  - LoRa message received
  - LoRa message transmitted
  - Message delivered to BLE
- Timeout: 2 minutes (120,000 ms)
- Checked in main loop

### Sleep Entry Process
1. Check if 2 minutes passed since last activity
2. Store any pending messages from queues to RTC memory
3. Print debug information (stored messages, wake-up count)
4. Configure wake-up sources
5. Enter light sleep via `esp_light_sleep_start()`

### Wake-up Process
1. ESP32 wakes from light sleep (maintains system state)
2. Check wake-up reason
3. Validate RTC memory data
4. Increment wake-up counter
5. Resume operation from sleep point
6. **Visual confirmation: LED blinks 3 times**
7. Continue normal operation
8. Deliver stored messages when BLE connects

## Configuration

### Sleep Timeout
To change the light sleep timeout, edit `SleepManager.h`:
```cpp
#define LIGHT_SLEEP_TIMEOUT_MS (2 * 60 * 1000)  // 2 minutes in milliseconds
```

### Message Buffer Size
To change the number of stored messages, edit `SleepManager.h`:
```cpp
#define MAX_STORED_MESSAGES 10  // Maximum messages in RTC memory
```

## Monitoring and Debug

### Visual Feedback
**LED Blink Patterns:**
- **3 blinks on boot**: Device woke from light sleep
- **1 blink**: Incoming LoRa message received
- **2 blinks**: Outgoing LoRa message transmitted

### Serial Output
The device prints detailed information about sleep operations:

**On wake-up:**
```
Wake-up caused by: LoRa interrupt (EXT1)
Wake-up count: 5
Stored messages: 3
```

**Before sleep:**
```
===================================
ENTERING LIGHT SLEEP
===================================
Stored messages: 2
Wake-up count: 4
Wake-up sources:
  - LoRa interrupt on GPIO 32
===================================
```

### Message Storage
```
Message stored in RTC memory (3/10)
```

### Message Delivery
```
Delivered stored message from RTC memory to BLE
Message retrieved from RTC memory (2 remaining)
```

## Limitations

1. **Maximum 10 messages** in RTC memory
   - Oldest messages are dropped if buffer is full
   - Adjust `MAX_STORED_MESSAGES` if needed

2. **BLE connection required for delivery**
   - Messages persist until BLE connection established
   - No automatic delivery via LoRa

3. **Power consumption during wake**
   - Device must be awake to send stored messages
   - Stays awake for 2 minutes or until all messages delivered

4. **No light sleep during active BLE**
   - While BLE is connected and active, no sleep
   - This is intentional for responsive communication

5. **Faster wake-up than deep sleep**
   - Light sleep maintains more state
   - Resume time is in milliseconds vs seconds for deep sleep
   - Better for frequent wake/sleep cycles

## Testing

### Test 1: LoRa Wake-up
1. Device in light sleep
2. Send LoRa message from another device
3. Device wakes up automatically
4. **Observe: LED blinks 3 times** (wake-up confirmation)
5. **Observe: LED blinks 1 time** (message received)
6. Receives message and stores it
7. Check serial output for confirmation

### Test 2: Message Persistence
1. Send 3 LoRa messages while no BLE connected
2. Device stores all 3 in RTC memory
3. Device enters light sleep
4. Connect Android via BLE
5. All 3 messages delivered automatically

### Test 3: Multiple Sleep Cycles
1. Send LoRa message → device wakes, stores, sleeps
2. Wait, send another → device wakes, stores (2 total), sleeps
3. Send another → device wakes, stores (3 total), sleeps
4. Connect BLE with Android
5. All 3 messages delivered

## Power Saving Calculations

### Example: Field Deployment (24 hours)

**Without light sleep:**
- Active 24h: 100mA × 24h = 2400 mAh

**With light sleep (assume 10 wake-ups, 5 min active each):**
- Active: 100mA × (50min / 60) = 83 mAh
- Sleep: 2mA × (1390min / 60) = 46 mAh
- **Total: ~129 mAh** (95% reduction!)

With a 2000 mAh battery:
- Without sleep: ~20 hours
- With light sleep: ~15 days (with light usage)

**Note**: Light sleep uses more power than deep sleep (~2-5mA vs 10-150μA), but provides:
- Much faster wake-up (milliseconds vs seconds)
- Maintains more system state
- Better for frequent wake/sleep cycles
- More responsive to interrupts

## Future Enhancements

Possible improvements:
1. Configurable timeout via BLE command
2. Dynamic sleep mode selection (light vs deep sleep based on usage patterns)
3. Flash storage for more message persistence
4. Scheduled wake-ups (timer-based)
5. Battery level monitoring with sleep adjustment
6. Message priority (keep important messages longer)
7. Automatic mode switching between light and deep sleep

## Troubleshooting

**Device not waking on LoRa:**
- Check LoRa DIO0 is connected to GPIO 32
- Ensure LoRa module is powered during sleep
- Verify LoRa interrupt configuration
- Test with a strong LoRa signal

**Messages not stored:**
- Check serial output for "buffer full" messages
- Increase `MAX_STORED_MESSAGES` if needed
- Verify RTC memory is valid (magic number check)

**Messages not delivered:**
- Ensure BLE connection is established
- Check that device is awake
- Monitor serial output for delivery confirmations

## Summary

The light sleep implementation provides:
- ✅ Automatic sleep after 2 minutes of inactivity
- ✅ Wake on LoRa signal (GPIO 32)
- ✅ Message persistence across sleep cycles
- ✅ Automatic delivery when BLE connects
- ✅ Up to 95% power savings
- ✅ No message loss during sleep
- ✅ Multiple sleep/wake cycles supported
- ✅ Fast wake-up (milliseconds)
- ✅ Maintains system state

Perfect for battery-powered field deployments with frequent wake/sleep cycles! 🔋
