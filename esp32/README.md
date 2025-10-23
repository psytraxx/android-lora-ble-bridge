# ESP32 LoRa-BLE Bridge (PlatformIO)

This is the ESP32 implementation of the LoRa-BLE bridge firmware, providing the same functionality as the ESP32-S3 Rust firmware but using C++ and the Arduino framework with PlatformIO.

## Overview

This firmware implements a BLE peripheral that communicates with Android devices and bridges BLE messages to LoRa transmission and reception.

### Features

- **BLE GATT Server**: Exposes TX/RX characteristics for message exchange with Android app
- **LoRa Radio**: Long-range communication (5-10 km typical, up to 15+ km in ideal conditions)
- **Message Protocol**: Implements the 6-bit text encoding protocol for bandwidth optimization
- **Automatic ACK**: Sends acknowledgments for received Text and GPS messages
- **Real-time Bridging**: Forwards messages between BLE and LoRa in real-time
- **LED Feedback**: Visual confirmation for message events

### Supported Message Types

1. **Text Message** (0x01): Sends text with 6-bit character encoding (max 50 chars)
2. **GPS Message** (0x02): Sends GPS coordinates separately (lat/lon as i32)
3. **ACK Message** (0x03): Acknowledges received messages

## Hardware Configuration

### Pin Mapping (Default ESP32 DevKit)

```
LoRa Module Pins:
  - SCK:   GPIO 18
  - MISO:  GPIO 19
  - MOSI:  GPIO 23
  - CS:    GPIO 5
  - RST:   GPIO 12
  - DIO0:  GPIO 32
```

### LoRa Configuration

- **Frequency**: 433.92 MHz (configurable via build flag)
- **Spreading Factor**: SF10 (long range)
- **Bandwidth**: 125 kHz
- **Coding Rate**: 4/5
- **TX Power**: 14 dBm / ~25 mW (configurable via build flag)
- **CRC**: Enabled

## BLE Service Configuration

```
Service UUID:              00001234-0000-1000-8000-00805f9b34fb
TX Characteristic UUID:    00005678-0000-1000-8000-00805f9b34fb  (for notifications to phone)
RX Characteristic UUID:    00005679-0000-1000-8000-00805f9b34fb  (for writes from phone)
Device Name:               ESP32S3-LoRa
```

## LED Status Indicators

The built-in LED provides visual feedback:

| Pattern | Meaning |
|---------|---------|
| 1 blink | LoRa message received |
| 2 blinks | LoRa message transmitted |

## Building and Uploading

### Prerequisites

- [PlatformIO](https://platformio.org/) installed
- ESP32 development board
- LoRa module (e.g., SX1276/SX1278)

### Build Commands

```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Open serial monitor
pio device monitor

# Build, upload, and monitor in one command
pio run --target upload && pio device monitor
```

### Configuration Options

You can configure TX power and frequency via build flags in `platformio.ini`:

```ini
build_flags = 
    -DLORA_TX_POWER_DBM=14          ; TX power in dBm (2-20)
    -DLORA_TX_FREQUENCY=433920000   ; Frequency in Hz
```

## Project Structure

```
esp32/
├── platformio.ini          # PlatformIO configuration
├── include/
│   ├── BLEManager.h        # BLE GATT server management
│   ├── LoRaManager.h       # LoRa radio management
│   ├── Protocol.h          # Message protocol definitions
│   ├── lora_config.h       # LoRa configuration constants
│   └── LEDManager.h        # LED control (optional)
├── src/
│   ├── main.cpp            # Main application logic
│   ├── BLEManager.cpp      # BLE implementation
│   └── Protocol.cpp        # Message serialization/deserialization
└── README.md               # This file
```

## Usage

1. **Power on the ESP32**: The device will start advertising as "ESP32S3-LoRa"
2. **Connect via Android app**: Use the companion Android app to connect via BLE
3. **Send messages**: Messages sent from the app are transmitted via LoRa
4. **Receive messages**: Incoming LoRa messages are forwarded to the connected phone via BLE

## Message Flow

### Sending a Message (Phone A → Phone B)

1. **Phone A**: User types message and presses send
2. **Phone A → ESP32-A**: Binary message sent via BLE (RX characteristic)
3. **ESP32-A**: Deserializes and validates message
4. **ESP32-A**: Transmits over LoRa radio (433 MHz)
5. **ESP32-B**: Receives LoRa transmission
6. **ESP32-B → ESP32-A**: Sends ACK via LoRa
7. **ESP32-B → Phone B**: Forwards via BLE notification (TX characteristic)
8. **Phone B**: Displays message text
9. **ESP32-A → Phone A**: Forwards ACK via BLE notification

## Protocol Details

See [protocol.md](../protocol.md) for detailed information about:
- 6-bit text encoding
- Message structure and wire format
- GPS coordinate format
- Time on Air (ToA) calculations
- Duty cycle compliance

## Performance

### Time on Air (ToA) @ SF10, BW125, 433MHz

| Message Size | Content | ToA |
|--------------|---------|-----|
| 7 bytes | "SOS" | ~370 ms |
| 16 bytes | 15-char text | ~420 ms |
| 42 bytes | 50-char text (max) | ~550 ms |
| 10 bytes | GPS only | ~380 ms |
| 2 bytes | ACK | ~330 ms |

### Range

- **Typical**: 5-10 km in open areas
- **Maximum**: Up to 15+ km in ideal conditions (line of sight, elevated antennas)
- **Urban**: 1-3 km (depending on obstacles)

## Differences from ESP32-S3 Rust Firmware

The C++/Arduino implementation provides the same core functionality as the Rust version but with some differences:

### Similarities
- Identical message protocol (compatible with ESP32-S3 devices)
- Same LoRa configuration (SF10, BW125, 433.92 MHz)
- Same BLE service UUIDs
- Automatic ACK handling
- 6-bit text encoding

### Differences
- **Language**: C++ with Arduino framework vs. Rust with Embassy async
- **Architecture**: Simple loop-based vs. async tasks
- **Message buffering**: Limited buffering vs. 10-message queue in Rust version
- **Memory**: Dynamic allocation vs. static memory in Rust
- **Concurrency**: Sequential processing vs. true async/await in Rust

## Troubleshooting

### LoRa Not Initializing
- Check SPI pin connections
- Verify LoRa module is powered (3.3V)
- Check antenna is connected

### BLE Not Visible
- Ensure Bluetooth is enabled on phone
- Try restarting the ESP32
- Check serial output for errors

### Messages Not Receiving
- Verify both devices use same frequency and LoRa configuration
- Check antenna connections
- Monitor RSSI values (should be > -120 dBm)

### Serial Monitor

Enable detailed logging by connecting to serial at 115200 baud:

```bash
pio device monitor -b 115200
```

## License

Same as the parent project.

## Related Files

- [ESP32-S3 Rust Firmware](../esp32s3/) - Async Rust implementation
- [Protocol Documentation](../protocol.md) - Message format specification
- [Android App](../android/) - Companion Android application
