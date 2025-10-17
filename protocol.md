# LoRa Message Protocol

This document defines the binary protocol for LoRa messages between ESP32 devices. The protocol is designed for minimal byte usage to maximize range and efficiency on 433 MHz LoRa.

## Message Structure

All messages are binary and start with a 1-byte message type.

### Data Message (Type: 0x01)
Used to send text and GPS coordinates.

- **Type**: 1 byte (0x01)
- **Sequence Number**: 1 byte (u8, for acknowledgment)
- **Text Length**: 1 byte (u8, length of text in bytes)
- **Text**: Variable bytes (UTF-8 encoded text, **maximum 50 characters**)
- **Latitude**: 4 bytes (i32, latitude × 1,000,000)
- **Longitude**: 4 bytes (i32, longitude × 1,000,000)

**Total Size**: 11 + text_length bytes  
**Minimum Size**: 11 bytes (empty text)  
**Maximum Size**: 61 bytes (50 characters text)

### Acknowledgment Message (Type: 0x02)
Used to acknowledge receipt of a data message.

- **Type**: 1 byte (0x02)
- **Sequence Number**: 1 byte (u8, the seq number being acknowledged)

**Total Size**: 2 bytes

## Technical Specifications

### Text Length Limit
- **Maximum**: 50 characters (enforced in both Android and ESP32)
- **Rationale**: Optimized for long-range LoRa transmission
  - With SF10, BW125, 433MHz configuration
  - Time on Air: ~700ms for max message (61 bytes)
  - Allows ~51 messages/hour within 1% duty cycle limits
  - Range: 5-10 km typical, up to 15+ km in ideal conditions

### GPS Coordinates
- **Format**: Signed 32-bit integers (i32)
- **Scaling**: Multiply degrees by 1,000,000 before transmission
- **Precision**: ~1 meter (6 decimal places)
- **Range**: -90° to +90° latitude, -180° to +180° longitude
- **Byte Order**: Little-endian
- **Example**: 
  - 37.7742° → 37,774,200 → bytes: `[0x18, 0x61, 0x3F, 0x02]`
  - -122.4192° → -122,419,200 → bytes: `[0x00, 0x0D, 0x83, 0x8A]`

### Text Encoding
- **Encoding**: UTF-8
- **Unicode Support**: Yes (including emoji)
- **Validation**: Both sender and receiver validate UTF-8 correctness

### Sequence Numbers
- **Range**: 0-255 (unsigned 8-bit)
- **Wraparound**: Automatic (255 → 0)
- **Purpose**: Match ACK responses to data messages
- **Note**: No delivery guarantee mechanism beyond ACK (application must handle retries)

## Wire Format Examples

### Example 1: Emergency Message
```
Text: "SOS"
Latitude: 37.7742° (San Francisco)
Longitude: -122.4192°
Sequence: 1

Hex bytes:
01 01 03 53 4F 53 18 61 3F 02 00 0D 83 8A
│  │  │  └─┬─┘ └──┬───┘ └──┬───┘
│  │  │    │      │        └─ Longitude: -122419200 (LE)
│  │  │    │      └─ Latitude: 37774200 (LE)
│  │  │    └─ Text: "SOS" (UTF-8)
│  │  └─ Text length: 3
│  └─ Sequence: 1
└─ Type: DATA (0x01)

Total: 14 bytes
```

### Example 2: Status Update
```
Text: "At checkpoint 2, all good"
Latitude: 40.7128° (New York)
Longitude: -74.0060°
Sequence: 42

Hex bytes:
01 2A 19 41 74 20 63 68 65 63 6B 70 6F 69 6E 74
20 32 2C 20 61 6C 6C 20 67 6F 6F 64 80 C9 6D 02
1C F4 B9 FB
│  │  │  └────────────────┬──────────────────┘ └──┬──┘ └──┬──┘
│  │  │                   │                        │       └─ Lon (LE)
│  │  │                   │                        └─ Lat (LE)  
│  │  │                   └─ Text: "At checkpoint 2, all good"
│  │  └─ Length: 25 (0x19)
│  └─ Sequence: 42 (0x2A)
└─ Type: DATA

Total: 36 bytes
```

### Example 3: ACK Response
```
Acknowledging sequence: 42

Hex bytes:
02 2A
│  └─ Sequence: 42
└─ Type: ACK (0x02)

Total: 2 bytes
```

## Message Flow

### Sending a Message (Phone A → Phone B)

1. **Phone A**: User types message and presses send
2. **Phone A**: App reads GPS coordinates
3. **Phone A**: App serializes: `DataMessage(seq, text, lat, lon)` → binary
4. **Phone A → ESP32-A**: Binary sent via BLE (characteristic 0x5679)
5. **ESP32-A**: Deserializes and validates message
6. **ESP32-A**: Transmits over LoRa radio (433 MHz)
7. **ESP32-B**: Receives LoRa transmission
8. **ESP32-B**: Deserializes message
9. **ESP32-B → ESP32-A**: Sends ACK via LoRa
10. **ESP32-B → Phone B**: Forwards via BLE notification (characteristic 0x5678)
11. **Phone B**: Displays message and GPS location
12. **ESP32-A → Phone A**: Forwards ACK via BLE notification
13. **Phone A**: Shows "Message delivered" confirmation

## Performance Characteristics

### LoRa Configuration
- **Spreading Factor**: SF10
- **Bandwidth**: 125 kHz
- **Coding Rate**: 4/5
- **Frequency**: 433.92 MHz (default, configurable)
- **TX Power**: 14 dBm / ~25 mW (default, configurable -4 to 20 dBm)

### Time on Air (ToA)

| Message Size | Text Length | ToA @ SF10 | Example |
|--------------|-------------|------------|---------|
| 11 bytes | 0 chars | ~370 ms | Empty message |
| 14 bytes | 3 chars | ~390 ms | "SOS" |
| 30 bytes | 19 chars | ~470 ms | "Weather is getting bad" |
| 45 bytes | 34 chars | ~580 ms | "At summit, all good, heading down" |
| 61 bytes | 50 chars | ~700 ms | Maximum length message |
| 2 bytes | ACK | ~330 ms | Acknowledgment |

### Duty Cycle Compliance (EU: 1% = 36 seconds/hour)

| Scenario | Messages/Hour | Use Case |
|----------|--------------|----------|
| Max length (50 char) | ~51 | Detailed updates |
| Typical (25 char) | ~72 | Normal communication |
| Short (10 char) | ~85 | Status updates |
| Emergency (5 char) | ~90 | SOS messages |

## Implementation Notes

### Error Handling
- Invalid UTF-8: Message rejected
- Text too long (>50 chars): Truncated or rejected
- Buffer too small: Serialization fails
- Malformed data: Deserialization fails
- Unknown message type: Ignored

### Security
- **No encryption**: Messages transmitted in plaintext
- **No authentication**: Any device can send/receive
- **No integrity check**: Beyond LoRa CRC
- **Use case**: Non-sensitive location sharing and status updates

### Reliability
- **ACK mechanism**: Confirms delivery to receiver's ESP32
- **No retransmission**: Application layer must implement if needed
- **No ordering guarantee**: Messages may arrive out of order
- **Sequence numbers**: Allow application to detect gaps

## Compatibility

### Cross-Platform
- ✅ Rust (ESP32 firmware)
- ✅ Java (Android app)
- ✅ Binary compatible (verified via unit tests)
- ✅ Same byte order (little-endian)
- ✅ Same text encoding (UTF-8)

### Version History
- **v1.0** (Oct 2025): Initial protocol with 255-char limit
- **v1.1** (Oct 2025): Reduced to 50-char limit for optimal range

## See Also
- **[README.md](README.md)** - Project overview, architecture, configuration, and build instructions