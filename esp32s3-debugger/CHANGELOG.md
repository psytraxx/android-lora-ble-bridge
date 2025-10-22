# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Dual Sleep Mode System**: Intelligent power management with two sleep modes
  - **Light Sleep (Automatic)**: Triggered after 30 seconds of inactivity
    - Preserves RAM and peripheral states (LoRa module stays initialized)
    - Wake source: LoRa message ONLY (GPIO 3 DIO0)
    - Power consumption: ~0.8-2mA
    - LoRa packets received during sleep are NOT lost
    - Execution continues after wake-up (no reboot)
    - No button wake-up (use long press before sleep to enter deep sleep instead)
  - **Deep Sleep (Manual)**: Triggered by long button press (2 seconds)
    - Ultra-low power mode with full system shutdown
    - Wake source: Button press (GPIO 14) ONLY
    - Power consumption: ~10-100μA
    - Device reboots on wake-up
    - Best for extended periods without LoRa communication

- **Button Control**: Long press detection for manual deep sleep
  - Short press (< 2s): Resets inactivity timer and restores display brightness
  - Long press (≥ 2s): Enters deep sleep with visual feedback
  - On-screen indicator: "Hold for deep sleep..." shown while holding
  - 50ms debounce for stable operation

- **Wake-up Handling**: Automatic detection and display of wake-up reason
  - Light sleep wake: Always shows "Woke: LoRa Message" (only wake source)
  - Deep sleep wake: Shows "Woke: Button (Deep Sleep)" on boot
  - Boot counter in RTC memory tracks wake cycles across deep sleep sessions

### Changed
- Display dimming now works in conjunction with sleep modes
  - Dims after 10 seconds of inactivity
  - Turns off completely before entering sleep
  - Automatically restored on wake-up from both sleep modes
- Activity timer resets on: LoRa message reception or button press
- LoRa receiver remains active until automatic light sleep timeout (30 seconds)
- Simplified wake-up logic: Button removed from light sleep wake sources for cleaner operation

### Technical Details

**Light Sleep Mode:**
- Wake source: EXT1 only (LoRa DIO0, GPIO 3, active HIGH)
- Preserves: RAM, CPU state, peripheral configurations, LoRa FIFO buffer
- Wake-up latency: ~100μs
- LoRa messages trigger wake and are processed automatically via callback
- Button does NOT wake from light sleep (simpler logic, prevents accidental wake)

**Deep Sleep Mode:**
- Wake source: EXT0 only (button, GPIO 14, active LOW)
- Resets: All RAM, CPU state, peripherals (full system reboot)
- Wake-up latency: ~100-200ms (includes boot time)
- LoRa module must be reinitialized after wake

**Power Consumption Estimates:**
- Active (LoRa RX + Display): ~30-40mA
- Display dimmed (10s timeout): ~25-30mA
- Light sleep (30s timeout): ~0.8-2mA (LoRa RX wake capable)
- Deep sleep (manual trigger): ~10-100μA (ultra-low power)

**Usage Scenarios:**
- **Normal Operation**: Device automatically cycles between active → dimmed → light sleep as needed
- **Extended Storage**: Long press button for manual deep sleep, wake with button press when needed
- **LoRa Reception**: Incoming messages wake device from light sleep, packet is received and processed
- **Manual Wake**: Not available from light sleep (LoRa only), prevents accidental interruption

## [Previous Versions]

### Initial Release
- LoRa receiver implementation for ESP32-S3
- TFT display support (LilyGo T-Display-S3)
- GPS and Text message reception
- Automatic acknowledgment sending
- Display dimming after 30 seconds
- LED indicator for received messages
- RSSI and SNR display
- Message history with scrolling
