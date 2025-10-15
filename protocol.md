# LoRa Message Protocol

This document defines the binary protocol for LoRa messages between ESP32 devices. The protocol is designed for minimal byte usage to maximize range and efficiency.

## Message Structure

All messages are binary and start with a 1-byte message type.

### Data Message (Type: 0x01)
Used to send text and GPS coordinates.

- **Type**: 1 byte (0x01)
- **Sequence Number**: 1 byte (u8, for acknowledgment)
- **Text Length**: 1 byte (u8, length of text in bytes)
- **Text**: Variable bytes (UTF-8 encoded text)
- **Latitude**: 4 bytes (i32, latitude * 1,000,000)
- **Longitude**: 4 bytes (i32, longitude * 1,000,000)

**Total Size**: 11 + text_length bytes

### Acknowledgment Message (Type: 0x02)
Used to acknowledge receipt of a data message.

- **Type**: 1 byte (0x02)
- **Sequence Number**: 1 byte (u8, the seq number being acknowledged)

**Total Size**: 2 bytes

## Notes
- GPS coordinates are stored as integers scaled by 1,000,000 to preserve precision while using 4 bytes instead of 8 for floats.
- Text is UTF-8 encoded; length limited to 255 bytes for protocol simplicity.
- No encryption or compression beyond this format.
- Sequence numbers wrap around (0-255) for simplicity.