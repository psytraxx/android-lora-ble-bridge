# firmware-rs

Rust rewrite of the ESP32-S3 LoRa-BLE bridge firmware, using `esp-hal` + Embassy
with a channel-based task architecture.

ESP32-S3 only (no feature switches).

## Status

Builds clean (`cargo build --release`, `cargo fmt --check`, `cargo clippy -- -D warnings`).
Wire-protocol, radio-parameter, BLE-UUID, and deep-sleep-wake-source parity with the
C++ firmware in `../firmware` has been verified by inspection.

## Known blockers

### 1. No power management — significantly higher idle current than the C++ firmware

This is the most consequential gap and is **structural, not a tuning issue**.
Higher-than-C++ power draw has been observed on hardware.

The C++ firmware calls `esp_pm_configure()` in `PowerManager::configurePowerManagement()`
with `max_freq_mhz = 160`, `min_freq_mhz = 80`, `light_sleep_enable = true`, backed by
`CONFIG_PM_ENABLE=y` and `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y` in `sdkconfig.defaults`.
It therefore gets both **DFS** (dynamic frequency scaling) and **automatic light sleep**
(tickless idle powers the CPU down between FreeRTOS ticks, with the modem kept coherent
via BT/WiFi PM locks).

The entire power-management surface in this crate is one line in `src/bin/main.rs`:

```rust
let config = Config::default().with_cpu_clock(CpuClock::_80MHz);
```

Consequences:

- **No automatic light sleep.** The CPU stays awake at 80 MHz continuously between
  deep-sleep windows. This is the dominant term in the power delta.
- **No DFS.** Fixed 80 MHz — equivalent to the C++ *fallback* path, not its success
  path. Roughly a wash on its own, since C++ ramps to 160 MHz under load.
- **No tickless idle.** `esp-rtos` + Embassy has no `CONFIG_FREERTOS_USE_TICKLESS_IDLE`
  analogue wired up.

Two application-level patterns make this worse and would blunt light sleep even if it
were enabled:

- **50 ms drain tick** — `MessageRouter::routing_loop` wakes every 50 ms whenever the
  offline buffer is non-empty (a 20 Hz wake cadence).
- **2 s LED heartbeat** — `Ticker::every(LED_HEARTBEAT_INTERVAL_MS)` runs in both the
  advertising and connected loops, forcing a wake every 2 s regardless of traffic.

`esp-hal` does expose light sleep (`Rtc::sleep_light()` with wake sources), but there is
no automatic, scheduler-integrated light sleep in the Embassy / `esp-rtos` stack the way
IDF's tickless idle provides it for free. Closing the gap means driving light sleep
manually from the idle path: computing when the next Embassy timer expires, arming the
appropriate wake sources, and ensuring the BLE controller and SX1262 tolerate the
transition. That interacts directly with the two wake patterns above.

To attribute the measured delta precisely, measure the C++ firmware with
`esp_pm_configure()` stubbed out; convergence on the Rust figure confirms it is entirely
PM, as opposed to differences in esp-radio vs NimBLE modem-sleep defaults or in the
LoRa RX/CAD duty cycle.

### 2. Hardware validation incomplete

No end-to-end LoRa↔BLE round-trip against a C++ node has been confirmed and recorded.

### 3. Single board only

`src/bin/main.rs` hardcodes Heltec Wireless Stick Lite V3 pins. The C++ tree ships four
PlatformIO envs (Stick V3, WSL V3, Stick Lite V3, `xiao_nrf52840`).

### 4. No OLED support

`ENABLE_OLED_DISPLAY` / `DisplayManager.cpp` has no Rust counterpart, so the Wireless
Stick V3 variant cannot be ported as-is.

### 5. nRF52840 out of scope

By design (ESP32-S3 only). If the `xiao_nrf52840` target must survive, the C++ tree
cannot be retired.

### 6. Unit tests do not run in CI

`.cargo/config.toml` pins `build.target` to xtensa with `build-std`, so `cargo test`
cross-compiles to the device and the CI job never invokes it. The pure-logic protocol
and router tests need a host-target harness to be meaningful.

### 7. Not integrated

No mention of `firmware-rs` in the top-level `README.md`, `AGENTS.md`, or `CLAUDE.md`;
`firmware-compile.yml` still only builds PlatformIO.

## References

- https://github.com/BroderickCarlin/SX1262
- https://github.com/psytraxx/android-lora-ble-bridge/blob/7020e3576eab2a74297a79f4f95c5a267fa7405b/esp32s3/src/lora.rs
- https://github.com/lora-rs/lora-rs
- https://github.com/embassy-rs/trouble/tree/main/examples/esp32/src/bin
