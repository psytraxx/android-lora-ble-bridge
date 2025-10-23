# ESP32 LoRa-BLE Bridge Implementation

## Overview
This firmware implements a low-power LoRa↔BLE bridge for communication with an Android app.  
It is designed to minimize energy consumption while maintaining reliable message delivery.

---

## Use Cases

### 1. Sending (Android → ESP32 → LoRa)
- The Android app connects to the ESP32 via BLE only when sending a message.
- The ESP32 transmits the message over LoRa.
- The ESP32 waits briefly (a few seconds) for a LoRa ACK.
- The BLE connection remains open during this period.
- After receiving the ACK or after a timeout, the BLE connection is automatically closed to save power.

### 2. Receiving (LoRa → ESP32 → Android)
- The ESP32 continuously listens for LoRa messages.
- When a message is received:
  - If BLE is connected, it forwards the message immediately.
  - If BLE is not connected, it buffers the message(s) and starts BLE advertising.
  - Once the Android app connects, buffered messages are sent.
  - After a short inactivity period (8 seconds by default), BLE disconnects automatically.

---

## Power Management

### BLE
- BLE advertising is started only when needed.
- BLE advertising stops automatically after inactivity.
- BLE connections are closed automatically after 8 seconds of inactivity.

### LoRa
- LoRa remains in continuous RX mode for message reception.
- TX operations are short and followed by immediate return to RX mode.

### CPU and Wi-Fi
- The CPU frequency is reduced to 160 MHz for power savings.
- Wi-Fi is disabled (`esp_wifi_stop()` and `esp_wifi_deinit()`).
- Modem sleep (`esp_wifi_set_ps(WIFI_PS_MIN_MODEM)`) is used instead of light sleep.

---

## Summary of Energy-Saving Features
| Component | Strategy |
|------------|-----------|
| BLE | Dynamic advertising, inactivity timeout, auto-disconnect |
| LoRa | Continuous RX, short TX bursts |
| CPU | Frequency scaling to 160 MHz |
| Wi-Fi | Disabled, modem sleep enabled |
| Watchdog | 30-second timeout for robustness |

---

## Future Improvements
- Add configurable BLE inactivity timeout via build flag.
- Implement persistent message buffering across reboots.
- Explore deep sleep for extended idle periods.
