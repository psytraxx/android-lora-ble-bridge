# Deep Sleep Implementation

## Overview

The ESP32 LoRa-BLE Bridge now supports deep sleep mode to dramatically reduce power consumption during periods of inactivity. The module automatically enters deep sleep after 2 minutes of no activity and can wake ### Test 4: Message Persistence
1. Send 3 LoRa messages while no BLE connected
2. Device stores all 3 in RTC memory
3. Device enters deep sleep
4. Press BOOT button to wake up
5. Connect Android via BLE
6. All 3 messages delivered automatically

### Test 5: Multiple Sleep Cycles1. **Button 1 press** (GPIO 0 - BOOT button)
2. **Button 2 release** (GPIO 15 - EN button) 
3. **LoRa signal reception** (GPIO 32 - LoRa DIO0 interrupt pin)

## Key Features

### 1. Automatic Deep Sleep
- Enters deep sleep after **2 minutes** of inactivity
- Activity is tracked for:
  - BLE connections
  - BLE message reception
  - LoRa message reception
  - LoRa message transmission
  - Message forwarding to BLE

### 2. Message Persistence
- Messages received while sleeping or without BLE connection are **stored in RTC memory**
- RTC memory persists across deep sleep cycles
- Up to **10 messages** can be stored
- Messages are delivered automatically when:
  - Device wakes up AND
  - BLE connection is established

### 3. Wake-up Sources

#### Button 1 Wake-up (GPIO 0 - BOOT)
- Press the BOOT button to wake the device
- Active LOW trigger (wakes when pressed)
- Most reliable wake-up method
- After wake-up, any stored messages will be delivered when BLE connects

#### Button 2 Wake-up (GPIO 15 - EN)
- The EN button can also wake the device
- Due to ESP32 EXT1 limitations with mixed polarity, this button wakes on **release** (not press)
- Press and hold during sleep, then release to wake
- Shares EXT1 wake source with LoRa interrupt
- Alternative: Can be reconfigured for active HIGH with external pull-down resistor

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

### Deep Sleep Mode
- Ultra-low power: ~10-150 μA (depending on ESP32 variant)
- RTC memory active (stores messages)
- Wake-up sources monitoring
- **~99% power reduction** compared to active mode

## Usage Scenarios

### Scenario 1: Field Device Without Android Connection
1. Device receives LoRa message
2. Device wakes from deep sleep (if sleeping)
3. Message is stored in RTC memory
4. Device sends ACK via LoRa
5. After 2 minutes of no activity, returns to deep sleep
6. Messages remain in RTC memory
7. When operator presses button and connects Android:
   - Device wakes up
   - BLE connection established
   - All stored messages are delivered to Android

### Scenario 2: Receive Message While Sleeping
1. Device is in deep sleep
2. LoRa message arrives → LoRa interrupt triggers
3. Device wakes up
4. Receives and processes message
5. Sends ACK
6. Stores message in RTC memory (no BLE connection)
7. Returns to deep sleep after 2 minutes
8. Repeat for subsequent messages (up to 10 stored)

### Scenario 3: Active BLE Session
1. Android connected via BLE
2. Activity timer resets on every message
3. Device stays awake as long as messages flow
4. If no activity for 2 minutes:
   - Any pending messages stored to RTC memory
   - Device enters deep sleep
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
5. Enter deep sleep via `esp_deep_sleep_start()`

### Wake-up Process
1. ESP32 boots up
2. Check wake-up reason
3. Validate RTC memory data
4. Increment wake-up counter
5. Initialize all peripherals (LoRa, BLE, LED)
6. **Visual confirmation: LED blinks 3 times**
7. Resume normal operation
8. Deliver stored messages when BLE connects

## Configuration

### Sleep Timeout
To change the deep sleep timeout, edit `SleepManager.h`:
```cpp
#define DEEP_SLEEP_TIMEOUT_MS (2 * 60 * 1000)  // 2 minutes in milliseconds
```

### Message Buffer Size
To change the number of stored messages, edit `SleepManager.h`:
```cpp
#define MAX_STORED_MESSAGES 10  // Maximum messages in RTC memory
```

### Wake-up Button
Default buttons are GPIO 0 (BOOT) and GPIO 15 (EN). To change, edit `SleepManager.h`:
```cpp
#define WAKE_BUTTON_PIN_1 GPIO_NUM_0   // Primary button (active LOW)
#define WAKE_BUTTON_PIN_2 GPIO_NUM_15  // Secondary button (wakes on release)
```

**Note on Button 2 Behavior:**
Due to ESP32 classic limitations, EXT0 supports one pin with configurable polarity, while EXT1 supports multiple pins but with same polarity for all. Since we need:
- Button 1: Wake on LOW (pressed)
- Button 2: Wake on LOW (pressed) 
- LoRa: Wake on HIGH (interrupt)

We use:
- EXT0 for Button 1 (GPIO 0) - wake on LOW ✅
- EXT1 for Button 2 (GPIO 15) + LoRa (GPIO 32) - wake on ANY_HIGH

This means Button 2 wakes when it goes HIGH (released), not when pressed. Workarounds:
1. Press and hold Button 2, then release to wake
2. Add external pull-down resistor to Button 2 to invert logic (active HIGH)
3. Use only Button 1 (BOOT) for reliable press-to-wake behavior

## Monitoring and Debug

### Visual Feedback
**LED Blink Patterns:**
- **3 blinks on boot**: Device woke from deep sleep
- **1 blink**: Incoming LoRa message received
- **2 blinks**: Outgoing LoRa message transmitted

### Serial Output
The device prints detailed information about sleep operations:

**On wake-up:**
```
Wake-up caused by: Button press (EXT0)
Wake-up count: 5
Stored messages: 3
```

Or:
```
Wake-up caused by: LoRa interrupt (EXT1)
Wake-up count: 5
Stored messages: 3
```

**Before sleep:**
```
===================================
ENTERING DEEP SLEEP
===================================
Stored messages: 2
Wake-up count: 4
Wake-up sources:
  - Button 1 (BOOT) on GPIO 0 (press to wake)
  - Button 2 (EN) on GPIO 15 (release to wake - due to pull-up)
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

4. **No deep sleep during active BLE**
   - While BLE is connected and active, no sleep
   - This is intentional for responsive communication

## Testing

### Test 1: Button 1 Wake-up (BOOT)
1. Power on device
2. Wait 2+ minutes (no activity)
3. Device enters deep sleep (LED off)
4. Press BOOT button (GPIO 0)
5. Device wakes up and reinitializes
6. **Observe: LED blinks 3 times** (wake-up confirmation)

### Test 2: Button 2 Wake-up (EN)
1. Device in deep sleep
2. Press and hold EN button (GPIO 15)
3. Release the button
4. Device wakes up (wakes on release due to pull-up configuration)
5. **Observe: LED blinks 3 times** (wake-up confirmation)

### Test 3: LoRa Wake-up
1. Device in deep sleep
2. Send LoRa message from another device
3. Device wakes up automatically
4. **Observe: LED blinks 3 times** (wake-up confirmation)
5. **Observe: LED blinks 1 time** (message received)
6. Receives message and stores it
7. Check serial output for confirmation

### Test 4: Message Persistence
1. Send 3 LoRa messages while no BLE connected
2. Device stores all 3 in RTC memory
3. Device enters deep sleep
4. Press button to wake up
5. Connect Android via BLE
6. All 3 messages delivered automatically

### Test 4: Multiple Sleep Cycles
1. Send LoRa message → device wakes, stores, sleeps
2. Wait, send another → device wakes, stores (2 total), sleeps
3. Send another → device wakes, stores (3 total), sleeps
4. Press button and connect BLE
5. All 3 messages delivered

## Power Saving Calculations

### Example: Field Deployment (24 hours)

**Without deep sleep:**
- Active 24h: 100mA × 24h = 2400 mAh

**With deep sleep (assume 10 wake-ups, 5 min active each):**
- Active: 100mA × (50min / 60) = 83 mAh
- Sleep: 0.1mA × (1390min / 60) = 2.3 mAh
- **Total: ~85 mAh** (96.5% reduction!)

With a 2000 mAh battery:
- Without sleep: ~20 hours
- With sleep: ~23 days (with light usage)

## Future Enhancements

Possible improvements:
1. Configurable timeout via BLE command
2. Light sleep during BLE idle (less aggressive)
3. Flash storage for more message persistence
4. Scheduled wake-ups (timer-based)
5. Battery level monitoring with sleep adjustment
6. Message priority (keep important messages longer)

## Troubleshooting

**Device not waking on Button 1 (BOOT):**
- Check GPIO 0 is configured correctly
- Ensure button connects GPIO 0 to GND when pressed
- This is the most reliable wake method

**Device not waking on Button 2 (EN):**
- Remember: Button 2 wakes on **release**, not press
- Press and hold the button, then release to wake
- Check GPIO 15 configuration
- Consider using only Button 1 for simpler operation
- Or add external pull-down resistor to GPIO 15 for active HIGH operation

**Device not waking on LoRa:**
- Check LoRa DIO0 is connected to GPIO 32
- Ensure LoRa module is powered during sleep
- Verify LoRa interrupt configuration

**Messages not stored:**
- Check serial output for "buffer full" messages
- Increase `MAX_STORED_MESSAGES` if needed
- Verify RTC memory is valid (magic number check)

**Messages not delivered:**
- Ensure BLE connection is established
- Check that device is awake
- Monitor serial output for delivery confirmations

## Summary

The deep sleep implementation provides:
- ✅ Automatic sleep after 2 minutes of inactivity
- ✅ Wake on button press (GPIO 0 - BOOT)
- ✅ Wake on button release (GPIO 15 - EN)*
- ✅ Wake on LoRa signal (GPIO 32)
- ✅ Message persistence across sleep cycles
- ✅ Automatic delivery when BLE connects
- ✅ Up to 96% power savings
- ✅ No message loss during sleep
- ✅ Multiple sleep/wake cycles supported

*Note: Button 2 (EN) wakes on release due to ESP32 EXT1 polarity limitations. Use Button 1 (BOOT) for press-to-wake.

Perfect for battery-powered field deployments! 🔋
