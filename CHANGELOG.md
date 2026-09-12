## Changelog

### 2026-09-12

**Changed**
- Switched the radio config to favor range and reliability over speed: messages now take longer to send but are more resistant to interference and travel further at the edge of range.
- The nRF52 board now transmits on a different radio frequency (869.525 MHz instead of 433.92 MHz), matching the European frequency band its new LoRa module is built for.
- Increased transmit power slightly across all boards for a small range boost.
- Brought the documentation back in line with how the radio is actually configured, and removed mentions of a radio module the project never actually supported.

### 2026-08-01

**Added**
- A settings menu in the PWA lets you turn auto-reconnect on or off and forget a previously paired device, so switching to a different board for testing no longer means losing the existing pairing.

**Fixed**
- Reconnecting to a device could get stuck with no way to cancel; a "Stop waiting" option is now always available during reconnect.
- A stuck reconnect attempt no longer hangs indefinitely — it now gives up and reports an error after a reasonable wait, and retries sooner when you switch back to the app or turn Bluetooth back on.
- Auto-reconnect no longer mistakenly latches onto a different device than the one you actually paired.

### 2026-06-01

**Added**
- Support for a new ESP32 board variant with a built-in display, showing connection status, signal strength, and battery level.

**Changed**
- Renamed a firmware build target to match the actual hardware it targets, to avoid confusion when flashing.
- Hardened the default ESP32 build settings so new boards work out of the box without manual configuration steps.

### 2026-03-01

**Changed**
- The radio now switches into receive mode without pausing the rest of the firmware, so the device stays responsive during that brief window.
- Reconnecting over Bluetooth no longer causes a noticeable pause while the app catches up.

**Fixed**
- Malformed incoming messages are now rejected safely instead of risking a crash.
- A rare corruption in the on-device message buffer could previously cause a crash; it now recovers gracefully instead.

### 2026-02-15

**Changed**
- Acknowledgment messages now wait for a genuinely free radio channel before sending, instead of a fixed random delay — this avoids collisions more reliably when multiple devices reply at once.
- Removed an older, simpler collision-avoidance delay that's no longer needed with the new channel-sensing approach.
- Fixed a bug in low-power mode where log messages could be cut off before the device went to sleep.

### 2026-02-01

**Changed**
- Updated Android app dependencies (Kotlin, Compose, and related libraries) to their latest versions.

### 2026-01-15

**Changed**
- Simplified the message protocol down to just text messages and acknowledgments, removing a separate wake-up message type that was no longer needed.
- Extended the radio preamble so an incoming text message can directly wake a sleeping receiver, without needing a dedicated wake-up message first.
- Simplified how acknowledgment timing works, replacing a more complex calculation with a short fixed delay.

### 2025-11-29

**Changed**
- Reconfigured the radio settings to prioritize range, roughly tripling the usable distance between devices in dense urban environments.

### 2025-11-24

**Changed**
- Renamed the sleep function for clarity.
- The firmware now distinguishes between being woken up by an incoming radio message versus a button press, and handles each correctly.
- Fixed the status LED using the wrong color.
- Reduced CPU speed while awake to save 30-40% more battery.

**Fixed**
- Fixed a bug where sending a message didn't correctly report whether it succeeded.

### 2025-11-21

**Added**
- Support for a second, smaller hardware platform (Seeed XIAO nRF52840), running the same firmware as the ESP32 boards.
- The firmware can now buffer up to 10 messages when the phone is disconnected, instead of dropping them.

**Changed**
- Rebuilt the firmware around a single shared codebase for both supported platforms, instead of maintaining separate versions.

### 2025-10-15

**Added**
- Rewrote the Android app from Java to Kotlin with a modern UI framework, along with a much larger automated test suite.
- The app can now send text messages together with GPS location in a single message, and tapping a message with a location opens it in Google Maps.
- Introduced a more compact way of encoding text that uses noticeably less radio airtime than plain text.

**Changed**
- Simplified the message format so text and location travel together as one message instead of two separate ones.
