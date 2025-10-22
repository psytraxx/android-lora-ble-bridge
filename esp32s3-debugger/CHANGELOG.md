# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Deep Sleep Mode**: Device now enters deep sleep after 2 minutes of inactivity to conserve power
  - Configurable sleep timeout (default: 120 seconds)
  - Wake-up on button press (GPIO 0 - Boot button)
  - Wake-up on LoRa message reception (via DIO0 interrupt on GPIO 44)
  - Boot counter in RTC memory to track wake cycles
  - Visual feedback on display before entering sleep mode
  - Wake-up reason display on boot (button, LoRa message, or cold boot)

- **Button Support**: Added support for LilyGo T-Display-S3 buttons
  - Button 1 (GPIO 0): Boot button - used for wake-up and activity reset
  - Button 2 (GPIO 14): User button - used for activity reset
  - Button press resets inactivity timer
  - Button press restores display brightness when dimmed
  - 50ms debounce for stable button handling

### Changed
- Display dimming now works in conjunction with sleep mode
  - Dims after 30 seconds of inactivity
  - Turns off completely before entering deep sleep
  - Brightness restored on wake-up or button press
- Activity timer now tracks both LoRa messages and button presses
- LoRa receiver remains active until sleep timeout is reached

### Technical Details
- Sleep Mode: ESP32 deep sleep with EXT0 and EXT1 wake-up sources
- Power Consumption: Significantly reduced during sleep periods
- Wake-up Latency: ~100-200ms from deep sleep to active reception
- Button Wake-up: Active LOW detection on GPIO 0
- LoRa Wake-up: Active HIGH detection on GPIO 44 (DIO0)

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
