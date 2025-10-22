# LoRa Message Protocol

This document defines the binary protocol for LoRa messages between ESP32 devices. The protocol is designed for minimal byte usage to maximize range and efficiency on 433 MHz LoRa.

## Message Structure

All messages are binary and start with a 1-byte message type.

### Text Message (Type: 0x01)
Used to send text messages with optional GPS coordinates. Uses 6-bit character packing for bandwidth optimization.

- **Type**: 1 byte (0x01)
- **Sequence Number**: 1 byte (u8, for acknowledgment)
- **Character Count**: 1 byte (u8, number of characters)
- **Packed Length**: 1 byte (u8, number of packed bytes)
- **Packed Text**: Variable bytes (6-bit packed, **maximum 50 characters**)
- **Has GPS**: 1 byte (0x00 = no GPS, 0x01 = GPS included)
- **Latitude**: 4 bytes (i32, latitude × 1,000,000) - **only if Has GPS = 1**
- **Longitude**: 4 bytes (i32, longitude × 1,000,000) - **only if Has GPS = 1**

**Character Set**: Uppercase A-Z, 0-9, space, and punctuation (64 chars total)
**Encoding**: 6 bits per character (not UTF-8)
**Minimum Size**: 5 bytes (empty text without GPS)
**Maximum Size**: 51 bytes (50 chars × 6 bits = 38 bytes + 5 byte header + 8 byte GPS)

### Acknowledgment Message (Type: 0x02)
Used to acknowledge receipt of text messages.

- **Type**: 1 byte (0x02)
- **Sequence Number**: 1 byte (u8, the seq number being acknowledged)

**Total Size**: 2 bytes

## Technical Specifications

### Text Length Limit
- **Maximum**: 50 characters (enforced in both Android and ESP32)
- **Rationale**: Optimized for long-range LoRa transmission
  - With SF10, BW125, 433MHz configuration
  - Time on Air: ~600ms for max message with GPS (51 bytes)
  - Allows ~60 messages/hour within 1% duty cycle limits
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
- **Optional**: GPS coordinates are only included when available
- **Example**: 
  - 37.7742° → 37,774,200 → bytes: `[0x18, 0x61, 0x3F, 0x02]`
  - -122.4192° → -122,419,200 → bytes: `[0x00, 0x0D, 0x83, 0x8A]`

### Sequence Numbers
- **Range**: 0-255 (unsigned 8-bit)
- **Wraparound**: Automatic (255 → 0)
- **Purpose**: Match ACK responses to messages
- **Note**: No delivery guarantee mechanism beyond ACK (application must handle retries)

## Wire Format Examples

### Example 1: Emergency Text Message (No GPS)
```
Text: "SOS"
Sequence: 1
Has GPS: No

Hex bytes (6-bit packed):
01 01 03 03 4A 12 00
│  │  │  │  └──┬─┘ └─ Has GPS: 0 (no)
│  │  │  │     └─ Packed text: "SOS" (3 chars in 3 bytes)
│  │  │  └─ Packed length: 3 bytes
│  │  └─ Character count: 3
│  └─ Sequence: 1
└─ Type: TEXT (0x01)

Total: 8 bytes
```

### Example 2: Text Message with GPS Location
```
Text: "AT CHECKPOINT 2"
Latitude: 37.7742° (San Francisco)
Longitude: -122.4192°
Sequence: 5

Hex bytes:
01 05 0F 0C [12 bytes of 6-bit packed text] 01 18 61 3F 02 00 0D 83 8A
│  │  │  │  └──────────┬──────────────┘ │  └──┬───┘ └──┬───┘
│  │  │  │             │                  │     │        └─ Longitude: -122419200 (LE)
│  │  │  │             │                  │     └─ Latitude: 37774200 (LE)
│  │  │  │             │                  └─ Has GPS: 1 (yes)
│  │  │  │             └─ Packed text (12 bytes for 15 chars)
│  │  │  └─ Packed length: 12 bytes
│  │  └─ Character count: 15
│  └─ Sequence: 5
└─ Type: TEXT (0x01)

Total: 26 bytes
```

### Example 3: Maximum Length Message with GPS
```
Text: "AT CHECKPOINT 2, ALL GOOD. WEATHER CLEAR. MOVING."
Sequence: 10
Has GPS: Yes

01 0A 32 26 [38 bytes of packed text] 01 [8 bytes GPS]
Total: 51 bytes (was 61 bytes in old format - 16% reduction!)
```

### Example 4: ACK Response
```
Acknowledging sequence: 5

Hex bytes:
02 05
│  └─ Sequence: 5
└─ Type: ACK (0x02)

Total: 2 bytes
```

## Message Flow

### Sending a Message (Phone A → Phone B)

1. **Phone A**: User types message and presses send
2. **Phone A**: App checks GPS availability
3. **Phone A**: App serializes `TextMessage(seq, text, hasGps, lat?, lon?)` → binary (6-bit packed)
4. **Phone A → ESP32-A**: Binary sent via BLE (characteristic 0x5679)
5. **ESP32-A**: Deserializes and validates message
6. **ESP32-A**: Transmits over LoRa radio (433 MHz)
7. **ESP32-B**: Receives LoRa transmission
8. **ESP32-B**: Deserializes message
9. **ESP32-B → ESP32-A**: Sends ACK via LoRa
10. **ESP32-B → Phone B**: Forwards via BLE notification (characteristic 0x5678)
11. **Phone B**: Displays message text (and GPS pin icon if GPS included)
12. **Phone B**: If user clicks message with GPS → Opens Google Maps
13. **ESP32-A → Phone A**: Forwards ACK via BLE notification
14. **Phone A**: Shows "Message delivered" confirmation

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
| 5 bytes | Empty text (no GPS) | ~350 ms | "" |
| 8 bytes | 3-char text (no GPS) | ~370 ms | "SOS" |
| 17 bytes | 15-char text (no GPS) | ~420 ms | "AT CHECKPOINT 2" |
| 26 bytes | 15-char text + GPS | ~480 ms | "AT CHECKPOINT 2" with location |
| 43 bytes | 50-char text (no GPS) | ~550 ms | Maximum length text only |
| 51 bytes | 50-char text + GPS | ~600 ms | Maximum length with GPS |
| 2 bytes | ACK | ~330 ms | Acknowledgment |

**Benefits over old protocol**:
- One message instead of two (text + GPS)
- No 100ms inter-message delay needed
- Simpler message handling
- GPS is optional, saves bandwidth when not needed

### Duty Cycle Compliance (EU: 1% = 36 seconds/hour)

| Scenario | Per Message | Messages/Hour | Use Case |
|----------|-------------|---------------|----------|
| Text only (50 char) | ~550 ms | ~65 | Detailed updates without GPS |
| Text only (25 char) | ~480 ms | ~75 | Normal messages |
| Text (10 char) + GPS | ~420 ms | ~85 | Status with location |
| Text (50 char) + GPS | ~600 ms | ~60 | Full message with location |
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
- **ACK mechanism**: Confirms delivery to receiver's ESP32
- **No retransmission**: Application layer must implement if needed
- **No ordering guarantee**: Messages may arrive out of order
- **Sequence numbers**: Allow application to detect gaps

### Message Sending Strategy
- **Android App Behavior**:
  - Always sends text message when user types something
  - Automatically includes GPS if GPS is enabled and location available
  - Single unified message (no separate GPS message)
  - Shows GPS location inline with text message

### Android UI
- **Message Display**: GPS coordinates shown inline with text (📍 icon)
- **Clickable Messages**: Messages with GPS are clickable
- **Maps Integration**: Clicking a message with GPS opens Google Maps
- **Fallback**: If Google Maps not installed, opens in browser

## Compatibility

### Cross-Platform
- ✅ Rust (ESP32 firmware)
- ✅ C++ (ESP32 Arduino, ESP32S3 Debugger)
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
  - GPS sent as separate message after text
  - ~40% bandwidth savings for text-only messages
- **v3.0** (Oct 2025):
  - **Unified TextMessage**: Merged text and GPS into single message type
  - Optional GPS coordinates (hasGps flag)
  - ACK moved from 0x03 to 0x02
  - Removed separate GPS message type (0x02)
  - Simplified protocol: Only TEXT (0x01) and ACK (0x02)
  - Single message transmission (no delay needed)
  - Android: Click message to open Google Maps
  - 16% bandwidth reduction for messages with GPS
  - Better user experience: GPS shown inline with text

### Breaking Changes in v3.0
- ⚠️ **Not backward compatible** with v2.0 or v1.0
- GPS message type (0x02) removed
- ACK message type changed from 0x03 to 0x02
- TextMessage format changed: added hasGps, lat, lon fields
- All devices must be updated simultaneously

## Migration from v2.0 to v3.0

### What Changed
1. **Unified Message Format**: Text and GPS are now in one message
2. **Simpler Protocol**: Only 2 message types (TEXT, ACK) instead of 3
3. **Android UI**: GPS shown inline, clickable to open Maps
4. **No Delays**: Single message transmission, no inter-message delays

### Benefits
- ✅ Simpler code: Less message handling logic
- ✅ Faster transmission: One message instead of two
- ✅ Better UX: GPS integrated with text
- ✅ Bandwidth savings: No duplicate headers
- ✅ Reduced complexity: Fewer message types to handle

## See Also
- **[README.md](README.md)** - Project overview, architecture, configuration, and build instructions
