# ESP32 Unified Firmware - Requirements & Implementation Plan

**Version:** 1.0
**Date:** 2025-11-01
**Target Hardware:** ESP32-S3 (LilyGo T-Display-S3) / ESP32 Generic

## Overview

Unified firmware for ESP32-S3 combining BLE Bridge and Display/Debugger functionality using PlatformIO build environments with feature flags. Single codebase, optimized binaries.
  
---

## Build Environments

### 1. Bridge Mode - ESP32 (`env:bridge-esp32`)
- **Hardware:** Generic ESP32 development board
- **Purpose:** BLE-to-LoRa bridge for Android app
- **Features:** BLE GATT server, message buffering, LED feedback (optional)
- **Libraries:** RadioLib, NimBLE-Arduino
- **Buffer Size:** 10 messages
- **LoRa Pins:** SCK=18, MISO=19, MOSI=23, SS=5, RST=12, DIO0=32
- **Wake Button:** GPIO 0 (Boot button)

### 2. Bridge Mode - ESP32-S3 (`env:bridge-esp32s3`)
- **Hardware:** LilyGo T-Display-S3 (display not used)
- **Purpose:** BLE-to-LoRa bridge for Android app
- **Features:** BLE GATT server, message buffering, LED feedback (optional)
- **Libraries:** RadioLib, NimBLE-Arduino
- **Buffer Size:** 10 messages
- **LoRa Pins:** SCK=12, MISO=13, MOSI=11, SS=10, RST=43, DIO0=3
- **Wake Button:** GPIO 0 or GPIO 14

### 3. Debugger Mode (`env:debugger`)
- **Hardware:** LilyGo T-Display-S3 (ESP32-S3 only)
- **Purpose:** LoRa receiver with display and test message capability
- **Features:** TFT display, message history, RSSI/SNR display, button test messages
- **Libraries:** RadioLib, GFX Library for Arduino
- **Buffer Size:** 20 messages
- **LoRa Pins:** SCK=12, MISO=13, MOSI=11, SS=10, RST=43, DIO0=3
- **Wake Button:** GPIO 14
- **Display:** ST7789 TFT (170x320), Backlight=GPIO38, Power=GPIO15

---

## Unified Power Management Requirements

**Same behavior for both Bridge and Debugger modes:**

### Wake/Sleep Timing
- **Inactivity Timeout:** 30 seconds → Enter light sleep
- **Activity Events:** Reset timeout on:
  - LoRa message received
  - LoRa message transmitted
  - BLE connection established (Bridge mode)
  - BLE data received/sent (Bridge mode)
  - Test message sent via button (Debugger mode)
  - Button press (any mode)

### Wake Sources
- **Button Wake:**
  - ESP32 generic: GPIO 0 (Boot button)
  - ESP32-S3 T-Display: GPIO 14 or GPIO 0
  - Trigger: LOW (active low with pullup)
  - Wake Type: EXT0

- **LoRa Wake:**
  - ESP32 generic: GPIO 32 (DIO0)
  - ESP32-S3 T-Display: GPIO 3 (DIO0)
  - Trigger: HIGH (interrupt on packet received)
  - Wake Type: GPIO wakeup bitmask

### RTC Data Persistence
- **MessageBuffer:** Entire buffer in RTC memory
- **Boot Count:** Track wake cycles
- **Power State:** Current state for resume after wake
- **Preparation:** Structure supports future deep sleep migration

### Power Optimization
- **CPU Frequency:** 160 MHz (not 240 MHz)
- **WiFi:** Disabled
- **Bluetooth Classic:** Disabled (BLE only in bridge mode)
- **LoRa Duty Cycle:** SF10 @ BW31.25kHz for moderate time-on-air
- **Sleep Mode:** Light sleep (can be upgraded to deep sleep later)
- **LED:** Optional (conditional compile)

---

## Unified MessageBuffer Specification

### Purpose
Single message buffer shared by both modes, persisted in RTC memory across sleep cycles.

### Structure

```cpp
struct BufferedMessage
{
    Message msg;          // Protocol message (Text or Ack)
    int16_t rssi;         // Signal strength (dBm)
    float snr;            // Signal-to-noise ratio (dB)
    uint32_t timestamp;   // millis() when received/queued
    bool isValid;         // Slot contains valid data
};
```

### Buffer Sizes
- **Bridge Mode:** 10 slots (~740 bytes RTC RAM)
- **Debugger Mode:** 20 slots (~1480 bytes RTC RAM)
- **Configurable:** Via `MESSAGE_BUFFER_SIZE` build flag

### API Features
- **FIFO Access:** `add()` / `get()` for BLE bridge mode
- **Random Access:** `getMessageAt(index)` for display mode
- **Metadata:** RSSI, SNR, timestamp for debugging
- **Drop-Oldest Policy:** When full, overwrite oldest message
- **Persistence:** Survives light/deep sleep cycles

### Usage Patterns

**Bridge Mode:**
```cpp
// Queue LoRa messages for BLE transmission
messageBuffer.add(msg, rssi, snr);

// Dequeue for BLE client
Message msg;
if (messageBuffer.get(msg)) {
    bleManager->sendData(msg);
}
```

**Display Mode:**
```cpp
// Add and display immediately
messageBuffer.add(msg, rssi, snr);
const BufferedMessage* newest = messageBuffer.getNewest();
displayManager->addMessage(newest->msg.textData.text,
                          newest->rssi, newest->snr);

// Show history on wake from sleep
for (int i = 0; i < messageBuffer.getCount(); i++) {
    const BufferedMessage* bm = messageBuffer.getMessageAt(i);
    displayManager->addMessage(bm->msg.textData.text,
                              bm->rssi, bm->snr);
}
```

---

## Directory Structure

```
esp32-unified/
├── platformio.ini              # Three build environments
├── src/
│   ├── main.cpp               # Unified entry point with #ifdef blocks
│   ├── BLEManager.cpp         # From esp32/ (ENABLE_BLE)
│   ├── DisplayManager.cpp     # From esp32s3-debugger/ (ENABLE_DISPLAY)
│   ├── PowerController.cpp    # Unified power management
│   └── MessageBuffer.cpp      # Unified RTC-persistent buffer
├── include/
│   ├── BLEManager.h
│   ├── DisplayManager.h
│   ├── PowerController.h
│   ├── MessageBuffer.h
│   └── LEDManager.h
└── lib/
    └── Protocol/ -> ../../shared/Protocol  # Symlink to shared protocol
```

---

## Feature Flags

### Core Flags
- `ENABLE_BLE` - Compile BLE bridge functionality
- `ENABLE_DISPLAY` - Compile display/debugger functionality
- `ENABLE_TEST_BUTTON` - Enable button test message sending
- `MESSAGE_BUFFER_SIZE` - Buffer capacity (10 or 20)

### LoRa Configuration
- `LORA_FREQ` - Frequency in MHz (433.92)
- `LORA_BW` - Bandwidth in kHz (31.25)
- `LORA_SF` - Spreading factor (10)
- `LORA_CR` - Coding rate (5)
- `LORA_TX_POWER` - TX power in dBm (20)

### Pin Definitions (ESP32-S3 T-Display)
- `LORA_SCK=12`, `LORA_MISO=13`, `LORA_MOSI=11`
- `LORA_SS=10`, `LORA_RST=43`, `LORA_DIO0=3`
- `PIN_LCD_BL=38`, `POWER_ON=15`
- `WAKE_BUTTON=14` (or GPIO 0 for boot button)

---

## Implementation Todo List

### Phase 1: Project Setup
- [ ] Create `esp32-unified/` directory
- [ ] Create `platformio.ini` with three environments
- [ ] Symlink or copy shared Protocol library
- [ ] Create directory structure (src/, include/, lib/)

### Phase 2: Core Components
- [ ] Implement unified MessageBuffer with RTC persistence
  - [ ] MessageBuffer.h with BufferedMessage struct
  - [ ] MessageBuffer.cpp with circular buffer logic
  - [ ] Unit tests (optional, on-device testing)

- [ ] Implement unified PowerController
  - [ ] Track last activity timestamp
  - [ ] Configurable inactivity timeout (30s default)
  - [ ] Light sleep entry/exit
  - [ ] Wake source configuration (button + LoRa DIO0)
  - [ ] Activity reset API (resetActivityTimer)
  - [ ] RTC state persistence

### Phase 3: Mode-Specific Components
- [ ] Migrate BLEManager from esp32/
  - [ ] Conditional compilation (#ifdef ENABLE_BLE)
  - [ ] Integration with unified MessageBuffer
  - [ ] Activity reporting to PowerController

- [ ] Migrate DisplayManager from esp32s3-debugger/
  - [ ] Conditional compilation (#ifdef ENABLE_DISPLAY)
  - [ ] Read from unified MessageBuffer
  - [ ] RSSI/SNR display
  - [ ] Message history scrolling

### Phase 4: Main Application
- [ ] Unified main.cpp
  - [ ] Common setup (LoRa, power management, RTC check)
  - [ ] BLE initialization (#ifdef ENABLE_BLE)
  - [ ] Display initialization (#ifdef ENABLE_DISPLAY)
  - [ ] Button handler (#ifdef ENABLE_TEST_BUTTON)
  - [ ] Unified loop with activity tracking
  - [ ] LoRa receive handler (writes to MessageBuffer)
  - [ ] Power management integration

### Phase 5: Testing & Validation
- [ ] Build all three environments
  - [ ] `pio run -e bridge-esp32` - Verify ESP32 bridge builds
  - [ ] `pio run -e bridge-esp32s3` - Verify ESP32-S3 bridge builds
  - [ ] `pio run -e debugger` - Verify debugger builds

- [ ] Flash and test bridge mode (ESP32)
  - [ ] BLE advertising visible
  - [ ] Android app connects successfully
  - [ ] Messages queued in buffer
  - [ ] BLE transmission works
  - [ ] Light sleep after 30s inactivity
  - [ ] Wake on button and LoRa
  - [ ] Buffer persists across sleep

- [ ] Flash and test bridge mode (ESP32-S3)
  - [ ] BLE advertising visible
  - [ ] Android app connects successfully
  - [ ] Messages queued in buffer
  - [ ] BLE transmission works
  - [ ] Light sleep after 30s inactivity
  - [ ] Wake on button (GPIO 0 or GPIO 14) and LoRa (GPIO 3)
  - [ ] Buffer persists across sleep

- [ ] Flash and test debugger mode
  - [ ] Display initializes correctly
  - [ ] LoRa messages appear on screen
  - [ ] RSSI/SNR displayed
  - [ ] Button sends test message
  - [ ] Message history scrolls
  - [ ] Light sleep after 30s inactivity
  - [ ] Buffer persists across sleep
  - [ ] History restored on wake

### Phase 6: Documentation & Cleanup
- [ ] Update CLAUDE.md with unified firmware info
- [ ] Update protocol.md if needed
- [ ] Create README.md in esp32-unified/
- [ ] Document power consumption measurements
- [ ] Archive or mark old esp32/ and esp32s3-debugger/ as deprecated

---

## Build Commands

```bash
cd esp32-unified

# Bridge Mode - ESP32
 ~/.platformio/penv/bin/pio run -e bridge-esp32
 ~/.platformio/penv/bin/pio run -e bridge-esp32 --target upload
 ~/.platformio/penv/bin/pio device monitor

# Bridge Mode - ESP32-S3
 ~/.platformio/penv/bin/pio run -e bridge-esp32s3
 ~/.platformio/penv/bin/pio run -e bridge-esp32s3 --target upload
 ~/.platformio/penv/bin/pio device monitor

# Debugger Mode - ESP32-S3
 ~/.platformio/penv/bin/pio run -e debugger
 ~/.platformio/penv/bin/pio run -e debugger --target upload
 ~/.platformio/penv/bin/pio device monitor
```

---

## Memory Budget

### Flash
- **Bridge Mode (ESP32/ESP32-S3):** ~800 KB (RadioLib + NimBLE + core)
- **Debugger Mode (ESP32-S3):** ~900 KB (RadioLib + GFX + core)
- **Available (ESP32):** 4 MB typical
- **Available (ESP32-S3):** 8 MB (plenty of headroom)

### RAM
- **BLE Stack:** ~100 KB (bridge mode only)
- **Display Buffer:** ~20 KB (debugger mode only)
- **Message Buffer:** 0.7-1.5 KB (depends on mode)
- **Available:** 512 KB SRAM (sufficient)

### RTC Memory
- **Message Buffer (Bridge):** ~740 bytes
- **Message Buffer (Debugger):** ~1480 bytes
- **Power State:** ~100 bytes
- **Boot Count:** 4 bytes
- **Available:** 8 KB RTC slow memory (sufficient)

---

## Power Consumption Estimates

### Bridge Mode
- **Active (BLE connected):** ~80 mA @ 3.3V
- **Advertising (no connection):** ~50 mA @ 3.3V
- **Light Sleep:** ~2 mA @ 3.3V
- **Wake-up latency:** ~10 ms

### Debugger Mode
- **Active (display on):** ~120 mA @ 3.3V (backlight dominant)
- **Active (display dim):** ~70 mA @ 3.3V
- **Light Sleep:** ~2 mA @ 3.3V
- **Wake-up latency:** ~10 ms

### Battery Life (2500 mAh)
- **Bridge (typical):** 70-100 hours (mixed active/sleep)
- **Debugger (typical):** 50-80 hours (display usage dependent)
- **Deep sleep future:** 300+ hours (when migrated)

---

## Future Enhancements

### Short Term
- [ ] Configurable inactivity timeout (runtime or build flag)
- [ ] LED patterns for different states (optional)
- [ ] Over-the-air (OTA) updates for both modes
- [ ] RSSI/SNR statistics on display

### Medium Term
- [ ] Migrate from light sleep to deep sleep
  - Requires careful handling of LoRa state
  - Display refresh on wake
  - BLE reconnection logic
- [ ] Message acknowledgment retry logic
- [ ] GPS coordinate display on map widget

### Long Term
- [ ] Bluetooth mesh for multi-hop routing
- [ ] LoRaWAN support for gateway mode
- [ ] Web interface via WiFi (config mode)
- [ ] SD card logging (if hardware supports)

---

## Testing Checklist

### Pre-Flight Checks
- [ ] Protocol version matches Android app
- [ ] LoRa parameters identical on all devices
- [ ] Antenna connected (433 MHz, 17cm quarter-wave)
- [ ] Battery charged or USB power connected
- [ ] Serial monitor working (115200 baud)

### Bridge Mode Tests
- [ ] BLE advertising shows "ESP32S3-LoRa"
- [ ] Android app discovers and connects
- [ ] Send text message → appears on debugger device
- [ ] Receive message from debugger → appears in Android
- [ ] GPS coordinates transmitted correctly
- [ ] ACK messages handled
- [ ] Device sleeps after 30s no activity
- [ ] Wake on button press
- [ ] Wake on incoming LoRa message
- [ ] Buffer persists across sleep (send message, sleep, wake, verify buffered)

### Debugger Mode Tests
- [ ] Display shows boot message
- [ ] Receive text message → appears on display
- [ ] RSSI and SNR displayed correctly
- [ ] Button press sends test message
- [ ] Message history scrollable (if >20 messages)
- [ ] Device sleeps after 30s no activity
- [ ] Wake on button press
- [ ] Wake on incoming LoRa message
- [ ] Message history restored on wake from sleep

### Range & RF Tests
- [ ] Line-of-sight range: 5-10 km (rural)
- [ ] Urban range: 1-3 km (obstructions)
- [ ] Indoor range: 100-300 m (walls)
- [ ] RSSI values reasonable (-120 to -40 dBm)
- [ ] SNR values positive for good link (>0 dB)

---

## Known Limitations

1. **No backward compatibility** - Protocol v3.0 required on all devices
2. **EU power limit** - Current 20 dBm exceeds EU 433 MHz limit (2 dBm max)
3. **No encryption** - Messages transmitted in plain text
4. **No message persistence to flash** - Only RTC (lost on full power cycle)
5. **No multi-device pairing** - BLE bridge connects to one Android at a time
6. **Fixed buffer size** - Compile-time only, not runtime configurable

---

## Regulatory Compliance

**IMPORTANT:** Verify TX power and duty cycle for your region.

### Current Configuration (20 dBm)
- ✅ **US 433 MHz:** Compliant (max 17 dBm, using 20 dBm = need to reduce)
- ✅ **Australia 433 MHz:** Compliant (max 14 dBm, using 20 dBm = need to reduce)
- ❌ **EU/Switzerland 433 MHz:** NON-COMPLIANT (max 2 dBm, using 20 dBm)

### Recommended Action
```cpp
// Add region selection in platformio.ini
-DREGION_EU          // 2 dBm TX power
-DREGION_US          // 17 dBm TX power
-DREGION_AU          // 14 dBm TX power
```

### Duty Cycle (EU 433 MHz: 1%)
- Maximum 36 seconds transmission per hour
- Current airtime per message: ~TBD ms (SF10 @ BW31.25kHz)
- Calculate: https://www.loratools.nl/#/airtime

---

## Success Criteria

The unified firmware is considered successful when:

1. ✅ Single codebase builds three variants without errors
2. ✅ Bridge mode passes all BLE and LoRa tests
3. ✅ Debugger mode displays messages with RSSI/SNR
4. ✅ Both modes enter light sleep after 30s inactivity
5. ✅ Both modes wake reliably on button and LoRa interrupt
6. ✅ MessageBuffer persists across sleep cycles
7. ✅ Power consumption matches estimates (±10%)
8. ✅ Range tests achieve 5+ km line-of-sight
9. ✅ Code is maintainable with clear #ifdef separation
10. ✅ Documentation is complete and accurate

---

## References

- **Protocol Specification:** `/protocol.md`
- **Current ESP32 Firmware:** `/esp32/`
- **Current Debugger Firmware:** `/esp32s3-debugger/`
- **Android App:** `/android/`
- **RadioLib Documentation:** https://github.com/jgromes/RadioLib
- **NimBLE Documentation:** https://github.com/h2zero/NimBLE-Arduino
- **LoRa Airtime Calculator:** https://www.loratools.nl/#/airtime

---

**Status:** Ready for implementation
**Next Step:** Create `esp32-unified/` directory and begin Phase 1
