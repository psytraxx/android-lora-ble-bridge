# Copilot / AI agent instructions for esp32s3-debugger

Purpose: help an AI agent become productive quickly in this firmware repo (ESP32 + LoRa + TFT). Keep responses concise and actionable.

1) Big-picture architecture (what to read first)
- `src/main.cpp` — entrypoint. Handles initialization, LoRa receive callback, button handling, sleep modes, and display updates.
- `include/DisplayManager.h` — thin wrapper around Arduino_GFX for the specific display (LilyGo T-Display-S3) and backlight PWM control.
- `LoRaManager.*` & `Protocol.*` (in `lib/` or project root) — radio setup, send/receive helpers, and message serialization/deserialization (Message, MessageType, createText/createAck, etc.).
- `platformio.ini` — environment (board: `lilygo-t-display-s3`), framework (Arduino) and libraries (LoRa, Arduino_GFX). Use it to infer build/test commands.

2) Primary runtime flows
- Startup: `setup()` in `src/main.cpp` initializes display, LoRa, starts RX, then either continues awake or (in our updated flow) will default to deep sleep on a power-on reset.
- RX path: LoRa interrupt invokes `onLoRaReceive()` which enqueues a `LoRaPacket` into `loRaQueue`. The `loop()` consumes the queue, deserializes `Message`, updates display and schedules ACKs.
- Sleep/wake: code uses both light sleep (`esp_light_sleep_start()`) and deep sleep (`esp_deep_sleep_start()`). Wake sources: button (EXT0), LoRa DIO0 (EXT1). Understand RTC_DATA_ATTR usage for data persisted across deep sleep.

3) Project-specific conventions and patterns
- RTC persistence: message buffer uses `RTC_DATA_ATTR` to keep small structures across deep sleep cycles. Look for `RTC_DATA_ATTR` variables when searching for persistence.
- Display usage: `DisplayManager` exposes `printLine`, `setBrightness`, `clearScreen`. Use these instead of directly calling `Arduino_GFX` in new code.
- Message handling: `Message` objects are serialized/deserialized via `Message::serialize()` / `deserialize()`. The code expects `MessageType::Text` and `MessageType::Ack` with fields like `textData.seq`, `textData.text`.
- LoRa driver: `LoRaManager` encapsulates low-level LoRa setup and `startReceiveMode()` / `sendPacket()` API. Prefer using `LoRaManager` instead of direct LoRa functions unless adding low-level support.

4) Build, flash and debug commands (PlatformIO)
- Build: from repo root run `~/.platformio/penv/bin/pio run` (PlatformIO reads `platformio.ini`).
- Upload/Flash: `~/.platformio/penv/bin/pio run --target upload` (uses the board and upload protocol in `platformio.ini`).
- Monitor serial: `~/.platformio/penv/bin/pio device monitor` or `~/.platformio/penv/bin/pio device monitor -b 115200`.
- Clean: `~/.platformio/penv/bin/pio run --target clean`.

Note: local environment may rely on PlatformIO installed in PATH. If missing, use the VS Code PlatformIO extension.

5) Where to add tests and how
- The repo uses `test/` but has no concrete unit tests. For fast feedback, create small unit-like tests using the ArduinoUnit or PlatformIO's Unity runner for logic-heavy code (e.g., circular buffer, Message serialize/deserialize).
- To run tests: `platformio test -e esp32dev` (after adding test cases under `test/`).

6) Key integration points and hardware assumptions
- Board: `lilygo-t-display-s3` (PlatformIO board). Pinout and RTC wake capability depend on this board; DIO0 is defined as GPIO 3 in `src/main.cpp` — verify that GPIO 3 is RTC-capable on your board if modifying wake sources.
- LoRa frequency and radio config live in `lora_config.h`.
- Display wiring: 8-bit parallel mode via `Arduino_ESP32PAR8Q` in `DisplayManager.h`. Changes to display must respect this bus type.

7) Typical change patterns and examples
- Add new wake source: update `configureDeepSleepWakeup()` and add appropriate `esp_sleep_enable_ext0_wakeup` / `ext1` calls. Remember to use RTC-capable GPIOs.
- Persist small state across deep sleep: add `RTC_DATA_ATTR` struct/variables; keep overall size small to avoid RTC memory overhead.
- Adding a new message type: extend `Protocol.h` `MessageType` enum and update `loop()`'s message handling switch to display/handle it.

8) Files to reference when modifying behavior
- `src/main.cpp` — primary changes will go here for runtime behavior.
- `include/DisplayManager.h` — UI rendering helpers.
- `lora_config.h`, `LoRaManager.*`, `Protocol.*` — radio and message format.
- `platformio.ini` — build flags and lib deps.

9) Safety and build tips for AI edits
- Keep edits minimal and localized. When changing sleep/wake code, avoid reformatting unrelated code. Test via `platformio run` after edits.
- If adding `RTC_DATA_ATTR` variables, initialize them carefully; they persist across deep sleep but are zeroed on full power cycle.
- When changing pin definitions, cross-check with `platformio.ini` board definition and `DisplayManager` wiring.

10) Example tasks the agent may be asked to do (how to approach)
- "Make deep sleep the default and wake on LoRa": modify `setup()` to configure and `esp_deep_sleep_start()` on power-on; add `esp_sleep_enable_ext1_wakeup` for DIO0 and keep button as EXT0.
- "Persist latest N messages": create a small circular buffer with `RTC_DATA_ATTR` and write into it in `onLoRaReceive()`, restore in `setup()` before clearing display.
- "Make short button press send test message": change button handling in `loop()` to call `Message::createText()` and `loraManager.sendPacket()` on short press.

11) Questions to ask the maintainer (if unclear)
- Is GPIO 3 (LORA_DIO0) guaranteed to be RTC-capable on the target hardware revision? If not, which pin should be used?
- Preferred persistence: keep messages in RTC memory (fast, limited) or NVS/Flash (durable, wears with writes)?

If you want me to update or expand any section (examples, build steps, or more precise file references), say which part and I'll iterate.
