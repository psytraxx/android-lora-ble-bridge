# LoRa Power Optimization Recommendations

**Generated:** 2025-11-13
**Hardware:** ESP32-S3 with SX1262/SX1278 LoRa radio

---

## Current Power Consumption Analysis

### ESP32 Power Management ✅ (Well Optimized)

**Already Implemented:**
- ✅ WiFi completely disabled (~50-80 mA savings)
- ✅ Bluetooth Classic disabled (BLE only via NimBLE)
- ✅ CPU frequency scaling (20-160 MHz dynamic)
- ✅ Light sleep enabled during idle periods
- ✅ Deep sleep after 30s advertising timeout
- ✅ BLE TX power optimized (+3 dBm instead of +9 dBm max)
- ✅ Inactivity timeout (60s) forces BLE disconnect
- ✅ GPIO wake from deep sleep (LoRa DIO0 + button)

### LoRa Radio Configuration 🔴 (Needs Optimization)

**Current Settings:**
- **Hardware:** SX1278 (LilyGo) or SX1262 (Heltec)
- **Mode:** Continuous RX (radio always listening)
- **Power consumption:** ~10-15 mA continuously
- **Spreading Factor:** SF11 @ 250 kHz BW
- **TX Power:** 20 dBm (maximum)
- **Time on Air (50-byte packet):** ~2000 ms

**Issues Identified:**
1. Radio never enters sleep mode (wastes 10-15 mA 24/7)
2. WakeUp message sent before every transmission (unnecessary delay + power)
3. High spreading factor = long TX time = high power consumption per message
4. SX1262-specific power features not utilized

---

## Power Optimization Opportunities

### Priority 1: Quick Wins (Easy + High Impact) ⚡

#### 1.1 Remove Redundant WakeUp Message

**Problem:**
Every transmission sends a WakeUp message first (blocking), waits 1000ms, then sends actual message. This was intended for "duty-cycled receivers" but all receivers are currently in continuous RX mode.

**Current Code:** `src/LoRaManager.cpp:124-153`
```cpp
// Step 1: Send WakeUp message (blocking) to wake duty-cycled receivers
ESP_LOGI(TAG, "Sending WakeUp message...");
Message wakeUpMsg = Message::createWakeUp();
// ... sends WakeUp, waits 1000ms ...

// Step 2: Send actual message (non-blocking)
```

**Solution:** Remove WakeUp message transmission entirely
```cpp
// Simply go straight to actual message transmission
ESP_LOGI(TAG, "Starting transmission of %d bytes", len);
radio->clearPacketReceivedAction();
radio->setPacketSentAction(LoRaManager::onTransmitISR);
state = STATE_TRANSMITTING;
int txState = radio->startTransmit(const_cast<uint8_t *>(data), len);
```

**Benefits:**
- ✅ Eliminate 1000ms latency per message (instant user experience improvement)
- ✅ Save WakeUp message TX power (~120 mA × TX duration)
- ✅ ~25% reduction in total transmission cycle time

**Effort:** 5 minutes (delete lines 124-153)
**Risk:** None (WakeUp only needed for duty-cycled receivers, which don't exist)

---

#### 1.2 Add Radio Sleep Mode After TX/RX

**Problem:**
Radio stays in continuous RX mode even when idle, consuming 10-15 mA constantly.

**Solution:** Put radio to sleep when not actively transmitting/receiving

**Location 1:** After TX complete - `src/LoRaManager.cpp:198`
```cpp
// Return to receive mode
startReceive();
state = STATE_IDLE;

// ADD THIS:
#ifdef RADIO_SX1262
// For SX1262: optionally sleep radio until next event
// Uncomment when implementing duty-cycled RX:
// radio->sleep();
#endif
```

**Location 2:** After RX packet processing - `src/LoRaManager.cpp:262`
```cpp
// Note: No need to restart receive mode here - radio remains in RX mode after readData()
ESP_LOGI(TAG, "RX packet processing complete (radio still in RX mode)");

// ADD THIS (when implementing duty-cycled RX):
#ifdef RADIO_SX1262
// For SX1262: optionally sleep radio between receive windows
// radio->sleep();
#endif
```

**Power Savings:**
- Continuous RX: 10-15 mA
- Sleep mode: ~2 µA
- **Reduction: 99.98%** when idle

**Effort:** 15 minutes
**Risk:** Low (requires implementing wake-up logic for incoming messages)

**Note:** This is a prerequisite for Priority 3 (duty-cycled RX). For now, add as commented-out code.

---

### Priority 2: SX1262 Optimizations (Heltec Board Only) 🔋

#### 2.1 Configure Regulator Mode (LDO vs DC-DC)

**Available Feature:** SX1262 supports both LDO and DC-DC regulators

**Location:** `src/LoRaManager.cpp:73` (after successful initialization)
```cpp
if (state == RADIOLIB_ERR_NONE)
{
    // ADD THIS (SX1262 only):
    #ifdef RADIO_SX1262
    // Use LDO regulator for lower power consumption
    // Note: DC-DC is more efficient at high TX power, LDO better for RX/standby
    radio->setRegulatorLDO();
    ESP_LOGI(TAG, "SX1262 regulator set to LDO mode");
    #endif

    this->state = STATE_IDLE;
    ESP_LOGI(TAG, "Setup successful");
    // ...
}
```

**Power Savings:** 1-3 mA in RX/standby mode
**Effort:** 5 minutes
**Risk:** Low (test range to ensure no degradation)

---

#### 2.2 Disable TCXO Power (If Using XTAL)

**Background:** SX1262 can supply voltage to external TCXO via DIO3. If your board uses a crystal oscillator (XTAL) instead, this wastes power.

**Location:** `src/LoRaManager.cpp:73` (after successful initialization)
```cpp
#ifdef RADIO_SX1262
// Check hardware schematic: if using XTAL (not TCXO), disable DIO3 voltage
// radio->setTCXO(0, 0);  // Voltage = 0V, delay = 0
// ESP_LOGI(TAG, "SX1262 TCXO disabled (using XTAL)");
#endif
```

**Power Savings:** 1-2 mA (if applicable)
**Effort:** 5 minutes (requires checking hardware schematic)
**Risk:** Low (no effect if TCXO not present)

**Action Required:** Check Heltec WiFi LoRa V3 schematic to confirm XTAL vs TCXO

---

### Priority 3: Advanced Optimizations (Requires Design Changes) 🚀

#### 3.1 Duty-Cycled RX Mode

**Concept:** Instead of continuous RX, periodically wake radio, listen briefly, then sleep.

**Implementation:**
```cpp
// In LoRa task or LoRaManager process loop:
while (1) {
    // Wake radio to standby
    radio->standby();

    // Start receive for short window (100-500ms)
    radio->startReceive();
    vTaskDelay(pdMS_TO_TICKS(100));  // Listen for 100ms

    // Check if packet received
    if (state == STATE_PACKET_RECEIVED) {
        // Process packet (existing logic)
        processReceivedPacket();
    } else {
        // No packet, go to sleep
        radio->sleep();
        vTaskDelay(pdMS_TO_TICKS(2000));  // Sleep 2 seconds
    }
}
```

**Average Power Calculation:**
- Active RX (100ms): 15 mA
- Sleep (2000ms): 0.002 mA
- **Average:** (15 × 0.1 + 0.002 × 2) / 2.1 ≈ **0.7 mA**

**Power Savings:** 10-15 mA → 0.7 mA = **93-95% reduction**

**Trade-offs:**
- ❌ Message latency: Up to sleep interval duration (e.g., 2 seconds)
- ❌ Requires re-implementing WakeUp message logic for senders
- ❌ Complexity: synchronization between sender/receiver timing

**Effort:** 2-3 hours (redesign RX architecture)
**Risk:** Medium (requires thorough testing)

---

#### 3.2 Lower Spreading Factor (SF11 → SF9)

**Current:** SF11 @ 250 kHz BW
- Time on Air (50-byte packet): ~2000 ms
- TX power consumption: 120 mA × 2s = **240 mA·s per message**

**Alternative:** SF9 @ 250 kHz BW
- Time on Air (50-byte packet): ~500 ms
- TX power consumption: 120 mA × 0.5s = **60 mA·s per message**
- **Savings: 75% per transmission**

**Range Trade-off:**
- SF11: ~10-15 km line-of-sight
- SF9: ~8-12 km line-of-sight
- **Loss: ~10-15% range reduction**

**Configuration Change:** `platformio.ini:28`
```ini
# Current:
-DLORA_SPREADING_FACTOR=11

# Proposed:
-DLORA_SPREADING_FACTOR=9
```

**Effort:** 1 minute (rebuild + flash)
**Risk:** Low (easily reverted, test range in your environment)

---

#### 3.3 Adaptive TX Power

**Concept:** Reduce TX power when communicating with nearby devices

**Current:** Fixed 20 dBm (100 mW) for all transmissions

**Better:** Adjust based on RSSI feedback
```cpp
// After receiving message with good RSSI (e.g., > -80 dBm):
if (packet.rssi > -80) {
    radio->setOutputPower(14);  // 14 dBm (25 mW) for nearby device
} else {
    radio->setOutputPower(20);  // 20 dBm (100 mW) for distant device
}
```

**Power Savings:** ~30-50% TX power for close-range communications
**Effort:** 1-2 hours (implement RSSI tracking + power control)
**Risk:** Low (protocol already has ACK mechanism to detect failures)

---

## Implementation Roadmap

### Phase 1: Immediate (Week 1)
1. ✅ Remove WakeUp message (5 min, zero risk)
2. ✅ Add SX1262 regulator optimization (5 min, low risk)
3. ⚠️ Test TCXO configuration (5 min, check schematic first)

**Expected Savings:** 25% TX cycle time + 1-3 mA RX power

---

### Phase 2: Short-term (Week 2-3)
4. ⚠️ Lower spreading factor SF11 → SF9 (1 min + range testing)
5. ⚠️ Prepare radio sleep infrastructure (commented code placeholders)

**Expected Savings:** 75% TX power per message

---

### Phase 3: Long-term (Month 2+)
6. 🚀 Implement duty-cycled RX mode (2-3 hours + testing)
7. 🚀 Implement adaptive TX power (1-2 hours)

**Expected Savings:** 93-95% RX power reduction

---

## Estimated Total Power Savings

### Current Consumption (Rough Estimates)
- **Deep Sleep:** ~10-20 µA (RTC + wake sources)
- **BLE Advertising (30s):** ~10-30 mA average
- **BLE Connected + Idle:** ~5-15 mA (CPU light sleep + BLE)
- **LoRa Continuous RX:** ~10-15 mA (always on)
- **LoRa TX (SF11):** ~120 mA for ~2 seconds

### After All Optimizations
- **Deep Sleep:** ~10-20 µA (unchanged)
- **BLE Advertising (30s):** ~10-30 mA (unchanged)
- **BLE Connected + Idle:** ~5-10 mA (slightly better)
- **LoRa Duty-Cycled RX:** ~0.7 mA (was 10-15 mA) ✅ **93% reduction**
- **LoRa TX (SF9):** ~120 mA for ~0.5 seconds ✅ **75% reduction**

### Battery Life Impact

**Assumptions:**
- 3000 mAh battery
- Device profile: 90% idle RX, 5% TX, 5% deep sleep

**Before Optimizations:**
- Average current: (0.9 × 15 mA) + (0.05 × 120 mA) + (0.05 × 0.02 mA) ≈ **19.5 mA**
- Battery life: 3000 mAh / 19.5 mA ≈ **154 hours (~6.4 days)**

**After All Optimizations:**
- Average current: (0.9 × 0.7 mA) + (0.05 × 120 mA × 0.25) + (0.05 × 0.02 mA) ≈ **2.1 mA**
- Battery life: 3000 mAh / 2.1 mA ≈ **1428 hours (~60 days)** 🎉

**Improvement: ~9-10x battery life increase**

---

## References

- **RadioLib SX1262 Documentation:** https://jgromes.github.io/RadioLib/class_s_x1262.html
- **ESP-IDF Power Management:** https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/power_management.html
- **LoRa Time-on-Air Calculator:** https://www.loratools.nl/#/airtime

---

## Files to Modify

| Priority | File | Lines | Change |
|----------|------|-------|--------|
| P1 | `src/LoRaManager.cpp` | 124-153 | Remove WakeUp message |
| P2 | `src/LoRaManager.cpp` | 73 | Add SX1262 regulator config |
| P2 | `src/LoRaManager.cpp` | 73 | Add TCXO config (if applicable) |
| P2 | `platformio.ini` | 28 | Change SF11 → SF9 |
| P3 | `src/LoRaManager.cpp` | 198, 262 | Add radio sleep calls |
| P3 | `src/LoraTask.cpp` | 110+ | Implement duty-cycled RX loop |
| P3 | `src/LoRaManager.cpp` | New method | Implement adaptive TX power |

---

**Status:** Recommendations documented, ready for implementation
**Next Step:** Implement Priority 1 optimizations for immediate benefits
