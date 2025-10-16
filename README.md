# LoRa Android RS

A project for sending short text messages and GPS locations via LoRa using ESP32 and Android.

## Architecture

See `architecture.md` for the system diagram and requirements.

## Structure

- `protocol/`: Rust crate defining the binary protocol for LoRa messages.
- `esp32/`: ESP32 firmware in Rust, handling BLE and LoRa communication.
- `android/`: Android Java app for sending/receiving messages.

## Protocol

See `protocol.md` for the message format.

## Building

### Protocol Crate
```bash
cd protocol
cargo build
```

### ESP32 Firmware
Requires ESP32 Rust toolchain.
```bash
cd esp32s3
cargo build --release --target xtensa-esp32-espidf
```

### Android App
Requires Android SDK.
```bash
cd android
# Gradle build
```