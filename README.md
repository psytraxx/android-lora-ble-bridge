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

## BLE Message Flow

This section provides a detailed explanation of how messages flow between the Android app and the ESP32 device via BLE, and how they are forwarded to/from LoRa.

### Receiving a Message from the Android App (BLE → LoRa)

1. **BLE Connection Establishment**:
   - The ESP32 runs as a BLE peripheral, advertising with the name "ESP32S3-LoRa" and service UUID 0x1234.
   - The Android app (acting as a BLE central) scans for and connects to the ESP32 peripheral.

2. **Message Transmission from Android**:
   - The Android app writes data to the RX characteristic (UUID: 0x5679) of the LoRa service.
   - This data is a serialized `Message` struct (see `protocol.md` for format).

3. **ESP32 GATT Event Handling**:
   - The `gatt_events_task` function listens for GATT events on the connection.
   - When a `GattEvent::Write` occurs on the handle matching `server.lora_service.rx.handle`:
     - The event data (`event.data()`) is deserialized into a `Message` using `Message::deserialize()`.
     - If deserialization succeeds, the `Message` is sent to the `ble_to_lora` channel using `try_send()`.
     - This channel is shared with the LoRa task (implemented in `lora.rs`).

4. **Forwarding to LoRa**:
   - The LoRa task receives the `Message` from the `ble_to_lora` channel.
   - It serializes the message and transmits it over the LoRa radio (details in `lora.rs`).

### Receiving a Message from LoRa and Forwarding to Android App (LoRa → BLE)

1. **Message Reception from LoRa**:
   - The LoRa task receives data over the LoRa radio.
   - It deserializes the data into a `Message` struct.
   - The `Message` is sent to the `lora_to_ble` channel using `try_send()`.

2. **ESP32 GATT Event Loop**:
   - In the `gatt_events_task` function, after processing any pending GATT events, the code checks for incoming messages from LoRa:
     - `if let Ok(msg) = lora_to_ble.try_receive()` attempts to receive a `Message` from the channel.
     - If a message is available, it is serialized into a buffer using `msg.serialize(&mut buf)`, which returns the length of the serialized data.

3. **BLE Notification to Android**:
   - The serialized data is copied into a `data` array.
   - `server.lora_service.tx.notify(conn, &data)` is called to send a GATT notification on the TX characteristic (UUID: 0x5678) to the connected Android central.
   - The Android app receives this notification and can process the message data.
   - Note: The TX characteristic's value is updated with the notification data, so the Android app can also read the characteristic to retrieve the last received message if needed (though notifications are the primary mechanism for real-time delivery).

### Key Notes
- **Channels**: `ble_to_lora` and `lora_to_ble` are Embassy channels used for asynchronous communication between the BLE and LoRa tasks.
- **Characteristics**:
  - RX (0x5679): Used for receiving data from the Android app (central writes to it).
  - TX (0x5678): Used for sending data to the Android app (peripheral notifies on it).
- **Error Handling**: Operations like deserialization, serialization, and channel sends use `if let Ok()` to handle potential failures gracefully without blocking.
- **Concurrency**: The BLE stack runs concurrently with the LoRa task, allowing bidirectional message forwarding.