# LoRa Message Protocol

This document defines the binary protocol for LoRa messages between ESP32 devices. The protocol is designed for minimal byte usage to maximize range and efficiency on 433 MHz LoRa.

## Message Structure

All messages are binary and start with a 1-byte message type.

### Text Message (Type: 0x01)
Used to send text messages with 6-bit character packing for bandwidth optimization.

- **Type**: 1 byte (0x01)
- **Sequence Number**: 1 byte (u8, for acknowledgment)
- **Character Count**: 1 byte (u8, number of characters)
- **Packed Length**: 1 byte (u8, number of packed bytes)
- **Packed Text**: Variable bytes (6-bit packed, **maximum 50 characters**)

**Character Set**: Uppercase A-Z, 0-9, space, and punctuation (64 chars total)
**Encoding**: 6 bits per character (not UTF-8)
**Total Size**: 4 + packed_bytes  
**Minimum Size**: 4 bytes (empty text)  
**Maximum Size**: 42 bytes (50 chars × 6 bits = 300 bits = 38 bytes + 4 byte header)

### GPS Message (Type: 0x02)
Used to send GPS coordinates separately from text.

- **Type**: 1 byte (0x02)
- **Sequence Number**: 1 byte (u8, for acknowledgment)
- **Latitude**: 4 bytes (i32, latitude × 1,000,000)
- **Longitude**: 4 bytes (i32, longitude × 1,000,000)

**Total Size**: 10 bytes (fixed)

### Acknowledgment Message (Type: 0x03)
Used to acknowledge receipt of text or GPS messages.

- **Type**: 1 byte (0x03)
- **Sequence Number**: 1 byte (u8, the seq number being acknowledged)

**Total Size**: 2 bytes

## Technical Specifications

### Text Length Limit
- **Maximum**: 50 characters (enforced in both Android and ESP32)
- **Rationale**: Optimized for long-range LoRa transmission
  - With SF10, BW125, 433MHz configuration
  - Time on Air: ~550ms for max message (42 bytes)
  - Allows ~65 messages/hour within 1% duty cycle limits
  - Range: 5-10 km typical, up to 15+ km in ideal conditions

### 6-bit Character Encoding
- **Character Set**: ` ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:;'"@#$%&*()[]{}=+/<>_`
- **Encoding**: 6 bits per character (64 possible values)
- **Efficiency**: 25% smaller than UTF-8 for uppercase ASCII
- **Case Handling**: Lowercase letters automatically converted to uppercase
- **Unsupported**: Emoji, non-ASCII characters, lowercase (converted)
- **Example**: "HELLO" = 5 chars × 6 bits = 30 bits = 4 bytes (vs 5 bytes UTF-8)

### GPS Coordinates
- **Format**: Signed 32-bit integers (i32)
- **Scaling**: Multiply degrees by 1,000,000 before transmission
- **Precision**: ~1 meter (6 decimal places)
- **Range**: -90° to +90° latitude, -180° to +180° longitude
- **Byte Order**: Little-endian
- **Example**: 
  - 37.7742° → 37,774,200 → bytes: `[0x18, 0x61, 0x3F, 0x02]`
  - -122.4192° → -122,419,200 → bytes: `[0x00, 0x0D, 0x83, 0x8A]`

### Sequence Numbers
- **Range**: 0-255 (unsigned 8-bit)
- **Wraparound**: Automatic (255 → 0)
- **Purpose**: Match ACK responses to messages
- **Note**: No delivery guarantee mechanism beyond ACK (application must handle retries)

## Wire Format Examples

### Example 1: Emergency Text Message
```
Text: "SOS"
Sequence: 1

Hex bytes (6-bit packed):
01 01 03 03 4A 12
│  │  │  │  └──┬─┘
│  │  │  │     └─ Packed text: "SOS" (3 chars in 3 bytes)
│  │  │  └─ Packed length: 3 bytes
│  │  └─ Character count: 3
│  └─ Sequence: 1
└─ Type: TEXT (0x01)

Total: 7 bytes
```

### Example 2: GPS Location
```
Latitude: 37.7742° (San Francisco)
Longitude: -122.4192°
Sequence: 2

Hex bytes:
02 02 18 61 3F 02 00 0D 83 8A
│  │  └──┬───┘ └──┬───┘
│  │     │        └─ Longitude: -122419200 (LE)
│  │     └─ Latitude: 37774200 (LE)
│  └─ Sequence: 2
└─ Type: GPS (0x02)

Total: 10 bytes
```

### Example 3: Status Update with GPS
```
Scenario: Send text then GPS (100ms apart)

Message 1 (Text):
Text: "AT CHECKPOINT 2"
Sequence: 5

01 05 0F 0C [12 bytes of 6-bit packed text]
Total: 16 bytes

Message 2 (GPS):
Latitude: 40.7128° (New York)
Longitude: -74.0060°
Sequence: 6

02 06 80 C9 6D 02 1C F4 B9 FB
Total: 10 bytes

Combined: 26 bytes (vs 36 bytes in old combined format)
```

### Example 4: ACK Response
```
Acknowledging sequence: 5

Hex bytes:
03 05
│  └─ Sequence: 5
└─ Type: ACK (0x03)

Total: 2 bytes
```

## Message Flow

### Sending a Message (Phone A → Phone B)

1. **Phone A**: User types message and presses send
2. **Phone A**: App checks GPS availability
3. **Phone A**: App serializes `TextMessage(seq, text)` → binary (6-bit packed)
4. **Phone A → ESP32-A**: Binary sent via BLE (characteristic 0x5679)
5. **ESP32-A**: Deserializes and validates message
6. **ESP32-A**: Transmits over LoRa radio (433 MHz)
7. **ESP32-B**: Receives LoRa transmission
8. **ESP32-B**: Deserializes message
9. **ESP32-B → ESP32-A**: Sends ACK via LoRa
10. **ESP32-B → Phone B**: Forwards via BLE notification (characteristic 0x5678)
11. **Phone B**: Displays message text
12. **ESP32-A → Phone A**: Forwards ACK via BLE notification
13. **Phone A**: Shows "Message delivered" confirmation

### Sending GPS (if available, 100ms after text)

14. **Phone A**: App serializes `GpsMessage(seq+1, lat, lon)` → binary
15. **Phone A → ESP32-A**: Binary sent via BLE
16. **ESP32-A**: Transmits GPS over LoRa
17. **ESP32-B**: Receives and forwards GPS to Phone B
18. **ESP32-B → ESP32-A**: Sends ACK
19. **Phone B**: Displays GPS coordinates

**Note**: GPS message is only sent if GPS is enabled and location is available.

## Performance Characteristics

### LoRa Configuration
- **Spreading Factor**: SF10
- **Bandwidth**: 125 kHz
- **Coding Rate**: 4/5
- **Frequency**: 433.92 MHz (default, configurable)
- **TX Power**: 14 dBm / ~25 mW (default, configurable -4 to 20 dBm)

### Time on Air (ToA)

| Message Size | Content | ToA @ SF10 | Example |
|--------------|---------|------------|---------|
| 4 bytes | Empty text | ~350 ms | "" |
| 7 bytes | 3-char text | ~370 ms | "SOS" |
| 16 bytes | 15-char text | ~420 ms | "AT CHECKPOINT 2" |
| 26 bytes | 30-char text | ~480 ms | "WEATHER IS GETTING BAD NOW" |
| 42 bytes | 50-char text | ~550 ms | Maximum length message |
| 10 bytes | GPS only | ~380 ms | Lat/Lon coordinates |
| 2 bytes | ACK | ~330 ms | Acknowledgment |

**Combined Text + GPS**: Add both times + 100ms delay
- Example: "SOS" + GPS = 370ms + 380ms + 100ms = 850ms total

### Duty Cycle Compliance (EU: 1% = 36 seconds/hour)

| Scenario | Per Message | Messages/Hour | Use Case |
|----------|-------------|---------------|----------|
| Text only (50 char) | ~550 ms | ~65 | Detailed updates without GPS |
| Text only (25 char) | ~480 ms | ~75 | Normal messages |
| GPS only | ~380 ms | ~94 | Position tracking |
| Text (10 char) + GPS | ~850 ms | ~42 | Status with location |
| Text (50 char) + GPS | ~930 ms | ~38 | Full message with location |
| Emergency (5 char) | ~360 ms | ~100 | SOS messages |

## Implementation Notes

### Error Handling
- Invalid character: Character not in 64-char charset rejected
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
- **ACK mechanism**: Confirms delivery to receiver's ESP32 (for both text and GPS)
- **No retransmission**: Application layer must implement if needed
- **No ordering guarantee**: Messages may arrive out of order
- **Sequence numbers**: Allow application to detect gaps

### Message Sending Strategy
- **Android App Behavior**:
  - Always sends text message if user types something
  - Only sends GPS message if GPS is enabled and location available
  - 100ms delay between text and GPS messages (prevents channel congestion)
  - Shows "Sent text only" if no GPS, "Sent text + GPS" if both sent

### Channel Capacity
- **ESP32 Buffer**: BLE→LoRa channel holds 5 messages (increased from 1)
- **Prevents drops**: When sending text+GPS burst, both messages buffered
- **LoRa→BLE Buffer**: 10 messages (for phone disconnection scenario)

## Compatibility

### Cross-Platform
- ✅ Rust (ESP32 firmware)
- ✅ Java (Android app)
- ✅ Binary compatible (verified via unit tests)
- ✅ Same byte order (little-endian)
- ✅ 6-bit packing implemented consistently

### Version History
- **v1.0** (Oct 2025): Initial protocol with UTF-8 encoding, combined text+GPS (DataMessage)
- **v2.0** (Oct 2025): 
  - Separated messages: TextMessage (0x01), GpsMessage (0x02), AckMessage (0x03)
  - Changed to 6-bit character packing for bandwidth efficiency
  - Uppercase-only charset (lowercase auto-converted)
  - GPS now optional when sending text
  - Increased BLE→LoRa channel capacity to 5 messages
  - ~40% bandwidth savings for text-only messages

### Breaking Changes in v2.0
- ⚠️ **Not backward compatible** with v1.0
- Message type 0x01 changed from `DataMessage` to `TextMessage`
- Text encoding changed from UTF-8 to 6-bit packed
- GPS separated into distinct message type (0x02)
- ACK moved from 0x02 to 0x03
- All devices must be updated simultaneously

## See Also
- **[README.md](README.md)** - Project overview, architecture, configuration, and build instructions