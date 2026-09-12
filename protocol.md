# LoRa Message Protocol

This document defines the binary protocol for LoRa messages between ESP32 devices. The protocol is designed for minimal byte usage to maximize range and efficiency on 433 MHz LoRa.

## Message Structure

All messages are binary and start with a 1-byte message type.

### Text Message (Type: 0x01)
Used to send text messages with optional GPS coordinates and sender time. Uses 6-bit character packing for bandwidth optimization.

- **Type**: 1 byte (0x01)
- **Sequence Number**: 1 byte (u8, for acknowledgment)
- **Character Count**: 1 byte (u8, number of characters)
- **Packed Length**: 1 byte (u8, number of packed bytes)
- **Packed Text**: Variable bytes (6-bit packed, **maximum 50 characters**)
- **Has GPS**: 1 byte (0x00 = no GPS, 0x01 = GPS included)
- **Latitude**: 4 bytes (i32, latitude × 1,000,000) - **only if Has GPS = 1**
- **Longitude**: 4 bytes (i32, longitude × 1,000,000) - **only if Has GPS = 1**
- **Sender Time**: 4 bytes (u32, Unix time in seconds, little-endian)

**Character Set**: Uppercase A-Z, 0-9, space, and punctuation (64 chars total)
**Encoding**: 6 bits per character (not UTF-8)
**Minimum Size**: 5 bytes (empty text without GPS)
**Maximum Size**: 55 bytes (50 chars × 6 bits = 38 bytes + 5 byte header + 8 byte GPS + 4 byte sender time)

### Acknowledgment Message (Type: 0x02)
Used to acknowledge receipt of text messages.

- **Type**: 1 byte (0x02)
- **Sequence Number**: 1 byte (u8, the seq number being acknowledged)

**Total Size**: 2 bytes

## Technical Specifications

### Text Length Limit
- **Maximum**: 50 characters (enforced in both Android and ESP32)
- **Rationale**: Optimized for long-range LoRa transmission
  - With SF11, BW125 kHz, CR4/7 configuration (433.92 MHz on ESP32, 869.525 MHz EU868 on nRF52)
  - Time on Air: ~3 seconds for max message with GPS (51 bytes)
  - Allows ~12 messages/hour within 1% duty cycle limits (EU)
  - Range: 10-25 km typical (SF11+BW125 balance of range and speed)

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
Sender Time: 1707418551 (Unix seconds)

Hex bytes (6-bit packed):
01 01 03 03 4A 12 00 77 2A 5B 65
│  │  │  │  └──┬─┘ └─ Has GPS: 0 (no)
│  │  │  │     └─ Packed text: "SOS" (3 chars in 3 bytes)
│  │  │  └─ Packed length: 3 bytes
│  │  └─ Character count: 3
│  └─ Sequence: 1
└─ Type: TEXT (0x01)
  └─ Sender Time: 0x655B2A77 (LE)

Total: 12 bytes
```

### Example 2: Text Message with GPS Location
```
Text: "AT CHECKPOINT 2"
Latitude: 37.7742° (San Francisco)
Longitude: -122.4192°
Sequence: 5
Sender Time: 1707418551 (Unix seconds)

Hex bytes:
01 05 0F 0C [12 bytes of 6-bit packed text] 01 18 61 3F 02 00 0D 83 8A 77 2A 5B 65
│  │  │  │  └──────────┬──────────────┘ │  └──┬───┘ └──┬───┘
│  │  │  │             │                  │     │        └─ Longitude: -122419200 (LE)
│  │  │  │             │                  │     └─ Latitude: 37774200 (LE)
│  │  │  │             │                  └─ Has GPS: 1 (yes)
│  │  │  │             └─ Packed text (12 bytes for 15 chars)
│  │  │  └─ Packed length: 12 bytes
│  │  └─ Character count: 15
│  └─ Sequence: 5
└─ Type: TEXT (0x01)
  └─ Sender Time: 0x655B2A77 (LE)

Total: 30 bytes
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

**Current Settings:**
- **Spreading Factor**: SF11 (excellent range)
- **Bandwidth**: 125 kHz (favors range over data rate)
- **Coding Rate**: 4/7 (higher overhead, stronger error correction)
- **Frequency**: 433.92 MHz on ESP32 boards, 869.525 MHz (EU868) on nRF52 (default, configurable)
- **TX Power**: 22 dBm / ~158 mW (default, configurable)
- **Preamble**: 64 symbols (extended preamble for direct wake-up of duty-cycled receivers)

**Note:** Settings favor range and reliability over throughput. SF11+BW125+CR4/7 trades transmission speed for stronger error correction and duty-cycle wake-up range.

### Time on Air (ToA)

**At SF11, BW125 kHz, CR4/7 (current configuration):**

| Message Size | Content | ToA @ SF11 BW125 CR4/7 | Example |
|--------------|---------|------------------------|---------|
| 2 bytes | ACK | ~0.8 s | Acknowledgment |
| 8 bytes | 3-char text (no GPS) | ~1.0 s | "SOS" |
| 20 bytes | 15-char text (no GPS) | ~1.4 s | "AT CHECKPOINT 2" |
| 28 bytes | 15-char text + GPS | ~1.8 s | "AT CHECKPOINT 2" with location |
| 40 bytes | 35-char text (no GPS) | ~2.2 s | Medium length text |
| 51 bytes | 50-char text + GPS | ~3.0 s | Maximum length with GPS |

**Benefits of current configuration (SF11 + BW125 + CR4/7):**
- Good range: 10-25 km typical
- Strong error correction reduces packet loss on marginal links
- Extended preamble reliably wakes duty-cycled receivers
- Good balance for most use cases

### Duty Cycle Compliance (EU: 1% = 36 seconds/hour)

**Based on SF11, BW125 kHz, CR4/7 (current configuration):**

| Scenario | Per Message | Messages/Hour | Use Case |
|----------|-------------|---------------|----------|
| Emergency (5 char) | ~1.0 s | ~36 | SOS messages |
| Text only (25 char) | ~1.6 s | ~22 | Normal messages |
| Text (15 char) + GPS | ~1.8 s | ~20 | Status with location |
| Text only (50 char) | ~2.4 s | ~15 | Detailed updates without GPS |
| Text (50 char) + GPS | ~3.0 s | ~12 | Full message with location |
| ACK | ~0.8 s | ~45 | Acknowledgments |

**Range vs. Speed Trade-off:**
- SF11+BW125: Favors range and link margin over throughput
- Ideal for: Maximizing range and reliability over speed
- Consider: If more throughput is needed at shorter range, BW250 could be used instead

**Note:** Use [LoRa Calculator](https://www.loratools.nl/#/airtime) to calculate exact ToA for your specific messages.

## Implementation Notes

### Timing and practical notes

- Preamble length: 64 symbols (~1050ms at SF11/BW125). This extended preamble ensures reliable duty-cycle RX detection and allows text messages to directly wake sleeping receivers without a separate wake-up mechanism.

- RX settle time: hardware receivers (SX126x) need a short stabilization window after switching into RX. Allow ~50 ms after calling startReceive()/startReceiveDutyCycleAuto() before assuming the radio is actively listening for payload bytes.

- ACK timing: ACK transmission goes through the same CAD (Channel Activity Detection) queue as any other transmit. Before sending, the radio performs `scanChannel()` — if the channel is free, it transmits immediately; if busy, it backs off (`CAD_BACKOFF_BASE_MS` + jitter) and retries, up to `CAD_MAX_RETRIES` before force-transmitting. This naturally staggers simultaneous ACKs from multiple receivers without a fixed delay/jitter scheme.

- Duty-cycle interoperability: the 64-symbol preamble ensures that duty-cycled SX1262 receivers (using RadioLib's startReceiveDutyCycleAuto()) will detect incoming text messages directly. No separate wake-up mechanism is required.


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

### Cross-Platform Implementation
- ✅ **C++ (Unified Firmware)** - ESP32 and nRF52 support
- ✅ **Kotlin (Android App)** - Modern implementation
- ✅ **TypeScript (PWA)** - Web Bluetooth support
- ✅ **Binary compatible** - Verified via unit tests across all platforms
- ✅ **Same byte order** - Little-endian on all platforms
- ✅ **6-bit packing** - Consistent implementation

### Message Type Usage
- **TEXT (0x01)**: Both LoRa and BLE
- **ACK (0x02)**: Both LoRa and BLE

