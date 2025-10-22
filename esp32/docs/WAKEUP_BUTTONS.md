# Wake-up Button Quick Reference

## Available Wake-up Sources

### 1. Button 1 - BOOT Button (GPIO 0) ⭐ RECOMMENDED
- **How to wake:** Simply press the button
- **Trigger:** Active LOW (wakes when pressed)
- **Wake source:** EXT0
- **Best for:** Quick and intuitive wake-up
- **Location:** Usually labeled "BOOT" or "IO0" on ESP32 dev boards

### 2. Button 2 - EN Button (GPIO 15)
- **How to wake:** Press and hold, then release
- **Trigger:** Active HIGH (wakes when released)
- **Wake source:** EXT1 (shared with LoRa)
- **Best for:** Alternative wake-up option
- **Note:** Due to pull-up resistor, wakes on button release, not press
- **Location:** Often the "EN" or "RESET" area on ESP32 boards

### 3. LoRa Interrupt (GPIO 32)
- **How to wake:** Automatic when LoRa message received
- **Trigger:** Active HIGH (LoRa DIO0 interrupt)
- **Wake source:** EXT1 (shared with Button 2)
- **Best for:** Automatic wake on incoming LoRa messages
- **Hardware:** Requires LoRa DIO0 connected to GPIO 32

## Why Button 2 Wakes on Release

The ESP32 classic has hardware limitations for deep sleep wake-up:

### EXT0 Wake Source
- Supports: **1 GPIO pin**
- Configurable polarity: LOW or HIGH
- Used for: Button 1 (GPIO 0) - wake on LOW (pressed)

### EXT1 Wake Source  
- Supports: **Multiple GPIO pins**
- Limitation: **All pins must use same trigger mode**
  - ALL_LOW: Wakes when ALL pins are LOW (AND logic)
  - ANY_HIGH: Wakes when ANY pin is HIGH (OR logic)

### Our Configuration
We need:
- Button 1: Wake on LOW (pressed) ✓ via EXT0
- Button 2: Wake on LOW (pressed) ✗ 
- LoRa: Wake on HIGH (interrupt) ✓ via EXT1

Since Button 2 and LoRa share EXT1, and they have opposite polarities, we must choose:
- Option A: Configure EXT1 for ANY_HIGH (LoRa works, Button 2 wakes on release)
- Option B: Configure EXT1 for ALL_LOW (Button 2 works, LoRa doesn't work)

We chose **Option A** to keep LoRa wake-up working.

## Workarounds for Button 2

If you need Button 2 to wake on press (not release):

### Hardware Solution
Add an external pull-down resistor to GPIO 15:
1. Remove or disable internal pull-up
2. Add 10kΩ resistor from GPIO 15 to GND
3. Configure button for active HIGH (pressed = HIGH)
4. Now both Button 2 and LoRa wake on HIGH (press and interrupt)

### Software Solution
Modify `SleepManager.cpp`:
```cpp
// Remove Button 2 from EXT1, only use Button 1 and LoRa
pinMode(WAKE_BUTTON_PIN_1, INPUT_PULLUP);
pinMode(loraIntPin, INPUT);

esp_sleep_enable_ext0_wakeup(WAKE_BUTTON_PIN_1, 0);  // Button 1 on LOW
esp_sleep_enable_ext1_wakeup(1ULL << loraIntPin, ESP_EXT1_WAKEUP_ANY_HIGH);  // LoRa only
```

### Alternative: Use ESP32-S2/S3/C3
Newer ESP32 variants support GPIO wake-up with individual pin configuration, eliminating this limitation.

## Usage Recommendations

### For Field Deployment
- **Primary:** Use Button 1 (BOOT) for manual wake-up
- **Secondary:** LoRa automatic wake-up on message reception
- **Skip:** Button 2 unless hardware modified

### For Development/Testing  
- **Button 1:** Quick wake-up for debugging
- **Button 2:** Test release-to-wake behavior
- **LoRa:** Test automatic wake on message

### For Production
Consider:
1. Using only Button 1 + LoRa (simplest)
2. Adding external pull-down to Button 2 if needed
3. Upgrading to ESP32-S3 for better GPIO wake flexibility
4. Custom hardware with dedicated wake button

## Summary Table

| Wake Source | GPIO | Press/Release | Shared | Reliable |
|-------------|------|---------------|--------|----------|
| Button 1 (BOOT) | 0 | **Press** | No | ⭐⭐⭐⭐⭐ |
| Button 2 (EN) | 15 | Release* | EXT1 | ⭐⭐⭐ |
| LoRa Interrupt | 32 | Automatic | EXT1 | ⭐⭐⭐⭐⭐ |

*With default configuration. Can be changed with hardware modification.

## Quick Test

To test all wake sources:

1. **Upload firmware and monitor serial output**
2. **Wait 2+ minutes** for device to enter deep sleep
3. **Test Button 1:** Press BOOT → Should wake immediately with **3 LED blinks**
4. **Wait for sleep again**
5. **Test Button 2:** Press and hold EN, then release → Should wake on release with **3 LED blinks**
6. **Wait for sleep again**  
7. **Test LoRa:** Send message from another LoRa device → Should wake automatically with **3 LED blinks**

Check serial monitor after each wake to see which source triggered it!

### LED Blink Patterns
- **3 blinks**: Wake-up from deep sleep
- **1 blink**: Incoming LoRa message
- **2 blinks**: Outgoing LoRa message
