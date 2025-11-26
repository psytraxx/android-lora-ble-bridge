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

### Wake-Up Message (Type: 0x03)
Used to wake LoRa devices from deep sleep. **LoRa-only** - never sent via BLE.

- **Type**: 1 byte (0x03)

**Total Size**: 1 byte
**Note**: This message type is only transmitted over LoRa between ESP32 devices. Android devices understand this message type for protocol consistency but never send or receive it via BLE.

## Technical Specifications

### Text Length Limit
- **Maximum**: 50 characters (enforced in both Android and ESP32)
- **Rationale**: Optimized for long-range LoRa transmission
  - With SF11, BW250 kHz, 433.92 MHz configuration
  - Time on Air: ~1.0 second for max message with GPS (51 bytes)
  - Allows ~36 messages/hour within 1% duty cycle limits (EU)
  - Range: 5-15 km typical (SF11 provides excellent sensitivity)

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

### Example 5: Wake-Up Message
```
Wake-up signal for deep sleep devices

Hex bytes:
03
└─ Type: WAKE_UP (0x03)

Total: 1 byte
Note: LoRa-only, never sent via BLE
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

**Current Settings (as of Nov 2025):**
- **Spreading Factor**: SF9 (balanced range/speed)
- **Bandwidth**: 250 kHz (fast airtime)
- **Coding Rate**: 4/5
- **Frequency**: 433.92 MHz (default, configurable)
- **TX Power**: 20 dBm / ~100 mW (default, configurable -4 to 20 dBm)
- **Preamble**: 8 symbols (default RadioLib)

**Note:** Settings optimized for moderate range (3-10 km) with fast airtime. For longer range, increase SF to 10-12 (reduces speed).

### Time on Air (ToA)

**At SF9, BW250 kHz (current configuration):**

| Message Size | Content | ToA @ SF9 BW250 | Example |
|--------------|---------|------------------|---------|
| 5 bytes | Empty text (no GPS) | ~0.2 s | "" |
| 8 bytes | 3-char text (no GPS) | ~0.2 s | "SOS" |
| 17 bytes | 15-char text (no GPS) | ~0.3 s | "AT CHECKPOINT 2" |
| 26 bytes | 15-char text + GPS | ~0.4 s | "AT CHECKPOINT 2" with location |
| 43 bytes | 50-char text (no GPS) | ~0.5 s | Maximum length text only |
| 51 bytes | 50-char text + GPS | ~0.6 s | Maximum length with GPS |
| 2 bytes | ACK | ~0.2 s | Acknowledgment |

**At SF11, BW250 kHz (for maximum range):**

| Message Size | Content | ToA @ SF11 BW250 | Range Benefit |
|--------------|---------|------------------|--------------|
| 8 bytes | 3-char text (no GPS) | ~0.5 s | +50% range vs SF9 |
| 26 bytes | 15-char text + GPS | ~0.8 s | +50% range vs SF9 |
| 51 bytes | 50-char text + GPS | ~1.0 s | +50% range vs SF9 |

**Benefits of current configuration (SF9):**
- One unified message (text + GPS optional)
- Fast airtime with BW250 kHz
- Good balance of range (3-10 km) and speed
- Lower duty cycle impact (more messages per hour)

### Duty Cycle Compliance (EU: 1% = 36 seconds/hour)

**Based on SF9, BW250 kHz (current configuration):**

| Scenario | Per Message | Messages/Hour | Use Case |
|----------|-------------|---------------|----------|
| Text only (50 char) | ~0.5 s | ~72 | Detailed updates without GPS |
| Text only (25 char) | ~0.3 s | ~120 | Normal messages |
| Text (10 char) + GPS | ~0.3 s | ~120 | Status with location |
| Text (50 char) + GPS | ~0.6 s | ~60 | Full message with location |
| Emergency (5 char) | ~0.2 s | ~180 | SOS messages |
| ACK | ~0.2 s | ~180 | Acknowledgments |

**Based on SF11, BW250 kHz (maximum range configuration):**

| Scenario | Per Message | Messages/Hour | Range Improvement |
|----------|-------------|---------------|------------------|
| Text only (50 char) | ~0.9 s | ~40 | +50% vs SF9 |
| Text (50 char) + GPS | ~1.0 s | ~36 | +50% vs SF9 |
| Emergency (5 char) | ~0.5 s | ~72 | +50% vs SF9 |

**Note:** Use [LoRa Calculator](https://www.loratools.nl/#/airtime) to calculate exact ToA for your specific messages.

## Implementation Notes

### Timing and practical notes

- Preamble length: 512 symbols (chosen to reliably intersect duty-cycled receive windows). At SF11 / BW250 this equates to roughly ~2.5 seconds of preamble on-air — plan for a long preamble when sizing duty-cycle and wake behaviours.

- RX settle time: hardware receivers (SX126x) need a short stabilization window after switching into RX. Allow ~50 ms after calling startReceive()/startReceiveDutyCycleAuto() before assuming the radio is actively listening for payload bytes.

- ACK timing: when a node receives a packet it waits a short ACK delay before transmitting its ACK. This implementation uses an ACK delay of ~500 ms to allow the radio subsystem to safely switch from RX to TX and avoid collisions. The ACK itself is a small packet (ToA ~0.4 s at SF11/BW250).

- Duty-cycle interoperability: the 512-symbol preamble ensures that duty-cycled SX1262 receivers (using RadioLib's startReceiveDutyCycleAuto()) will be awake at least once during the preamble and can lock onto the following payload. Continuous-receive radios (SX127x) simply stay in RX and detect the preamble normally.

- Preventing wake-up loops: on ESP32 devices we distinguish the wake source. If the device was woken by LoRa (EXT0 / DIO0), do NOT send a WakeUp message in response. If woken by a local button (EXT1) or on a cold boot, send a WakeUp message. This avoids two devices repeatedly triggering each other.

- Practical tip for testing: when validating interoperability between an autonomous-duty SX1262 node and a continuous SX127x receiver, send a single packet from the transmitter and monitor the receiver for the full preamble duration plus RX settle (packet transmission + ~50 ms). For ACK testing include the 500 ms ACK delay in your timing expectations.


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
- **WAKE_UP (0x03)**: LoRa-only (ESP32/nRF52 to ESP32/nRF52)
