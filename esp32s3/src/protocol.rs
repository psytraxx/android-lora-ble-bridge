use alloc::boxed::Box;
use alloc::vec::Vec;
use heapless::String;

/// Maximum text length in characters for optimal long-range LoRa transmission.
/// With 6-bit packing: 50 chars = 38 bytes (was 50 bytes)
/// With SF10, BW125, 433MHz: 50 bytes (12 header + 38 text) = ~600ms Time on Air
/// This allows ~60 messages per hour within 1% duty cycle limits (was ~51).
pub const MAX_TEXT_LENGTH: usize = 50;

/// Character set for 6-bit encoding (64 characters)
/// Index maps to 6-bit value: 0-63
/// UPPERCASE ONLY: Space + A-Z (26) + 0-9 (10) + punctuation (27)
const CHARSET: &[u8; 64] = b" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:;'\"@#$%&*()[]{}=+/<>_";

/// Convert a character to its 6-bit encoded value
/// Automatically converts lowercase to uppercase
fn char_to_6bit(ch: char) -> Result<u8, &'static str> {
    let upper_ch = ch.to_ascii_uppercase();
    CHARSET
        .iter()
        .position(|&c| c as char == upper_ch)
        .map(|p| p as u8)
        .ok_or("Character not in supported charset")
}

/// Convert a 6-bit value back to a character
fn sixbit_to_char(val: u8) -> Result<char, &'static str> {
    if (val as usize) < CHARSET.len() {
        Ok(CHARSET[val as usize] as char)
    } else {
        Err("Invalid 5-bit value")
    }
}

/// Pack text into 6-bit encoded bytes using manual bit manipulation
/// Each character is encoded as 6 bits instead of 8 bits (UTF-8)
/// Lowercase letters are automatically converted to uppercase
/// 50 chars × 6 bits = 300 bits = 37.5 bytes → 38 bytes
fn pack_text(text: &str) -> Result<Vec<u8>, &'static str> {
    let char_count = text.chars().count();
    // Calculate required bytes: (char_count * 6 + 7) / 8 (round up)
    let byte_count = (char_count * 6).div_ceil(8);
    let mut result = alloc::vec![0u8; byte_count];

    let mut bit_offset = 0;

    for ch in text.chars() {
        let value = char_to_6bit(ch)?;

        // Calculate which byte(s) this 6-bit value spans
        let byte_idx = bit_offset / 8;
        let bit_in_byte = bit_offset % 8;

        if bit_in_byte <= 2 {
            // The 6 bits fit within the current byte
            result[byte_idx] |= value << (2 - bit_in_byte);
        } else {
            // The 6 bits span two bytes
            let bits_in_first = 8 - bit_in_byte;
            let bits_in_second = 6 - bits_in_first;

            result[byte_idx] |= value >> bits_in_second;
            if byte_idx + 1 < result.len() {
                result[byte_idx + 1] |= value << (8 - bits_in_second);
            }
        }

        bit_offset += 6;
    }

    Ok(result)
}

/// Unpack 6-bit encoded bytes back to text using manual bit manipulation
/// Reads 6 bits at a time and converts to characters (uppercase)
fn unpack_text(packed: &[u8], char_count: usize) -> Result<String<64>, &'static str> {
    let mut result = String::<64>::new();
    let mut bit_offset = 0;

    for _ in 0..char_count {
        let byte_idx = bit_offset / 8;
        let bit_in_byte = bit_offset % 8;

        if byte_idx >= packed.len() {
            return Err("Insufficient packed data");
        }

        let value = if bit_in_byte <= 2 {
            // The 6 bits are within the current byte
            (packed[byte_idx] >> (2 - bit_in_byte)) & 0x3F
        } else {
            // The 6 bits span two bytes
            let bits_in_first = 8 - bit_in_byte;
            let bits_in_second = 6 - bits_in_first;

            let first_part = packed[byte_idx] & ((1 << bits_in_first) - 1);
            let second_part = if byte_idx + 1 < packed.len() {
                packed[byte_idx + 1] >> (8 - bits_in_second)
            } else {
                return Err("Insufficient packed data");
            };

            (first_part << bits_in_second) | second_part
        };

        let ch = sixbit_to_char(value)?;
        result.push(ch).map_err(|_| "String capacity exceeded")?;

        bit_offset += 6;
    }

    Ok(result)
}

/// Message types
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum MessageType {
    Data = 0x01,
    Ack = 0x02,
}

/// Data message containing text and GPS coordinates
#[derive(Debug, Clone, PartialEq)]
pub struct DataMessage {
    pub seq: u8,
    pub text: String<64>, // Max 50 chars (optimized for long-range transmission)
    pub lat: i32,         // latitude * 1_000_000
    pub lon: i32,         // longitude * 1_000_000
}

/// Acknowledgment message
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct AckMessage {
    pub seq: u8,
}

/// Union of all message types
#[derive(Debug, Clone, PartialEq)]
pub enum Message {
    Data(Box<DataMessage>),
    Ack(AckMessage),
}

impl Message {
    /// Serializes the message into the provided buffer.
    /// Returns the number of bytes written on success, or an error on failure.
    /// Text is packed using 6-bit encoding for efficiency.
    pub fn serialize(&self, buf: &mut [u8]) -> Result<usize, &'static str> {
        match self {
            Message::Data(data) => {
                if data.text.len() > MAX_TEXT_LENGTH {
                    return Err("Text too long");
                }

                // Pack the text using 6-bit encoding
                let packed_text = pack_text(&data.text)?;
                let packed_len = packed_text.len();

                if buf.len() < 12 + packed_len {
                    return Err("Buffer too small");
                }

                buf[0] = MessageType::Data as u8;
                buf[1] = data.seq;
                buf[2] = data.text.len() as u8; // Store original character count
                buf[3] = packed_len as u8; // Store packed byte count
                buf[4..4 + packed_len].copy_from_slice(&packed_text);

                let mut offset = 4 + packed_len;
                buf[offset..offset + 4].copy_from_slice(&data.lat.to_le_bytes());
                offset += 4;
                buf[offset..offset + 4].copy_from_slice(&data.lon.to_le_bytes());

                Ok(12 + packed_len)
            }
            Message::Ack(ack) => {
                if buf.len() < 2 {
                    return Err("Buffer too small");
                }
                buf[0] = MessageType::Ack as u8;
                buf[1] = ack.seq;
                Ok(2)
            }
        }
    }

    /// Deserializes a message from the provided buffer.
    /// Returns the parsed Message on success, or an error on failure.
    /// Text is unpacked from 6-bit encoding.
    pub fn deserialize(buf: &[u8]) -> Result<Self, &'static str> {
        if buf.is_empty() {
            return Err("Empty buffer");
        }
        match buf[0] {
            0x01 => {
                if buf.len() < 12 {
                    return Err("Buffer too small for data message");
                }
                let seq = buf[1];
                let char_count = buf[2] as usize;
                let packed_len = buf[3] as usize;

                if buf.len() < 12 + packed_len {
                    return Err("Buffer too small for packed text");
                }

                let packed_bytes = &buf[4..4 + packed_len];
                let text = unpack_text(packed_bytes, char_count)?;

                let mut offset = 4 + packed_len;
                let lat = i32::from_le_bytes(buf[offset..offset + 4].try_into().unwrap());
                offset += 4;
                let lon = i32::from_le_bytes(buf[offset..offset + 4].try_into().unwrap());

                Ok(Message::Data(Box::new(DataMessage {
                    seq,
                    text,
                    lat,
                    lon,
                })))
            }
            0x02 => {
                if buf.len() < 2 {
                    return Err("Buffer too small for ack");
                }
                let seq = buf[1];
                Ok(Message::Ack(AckMessage { seq }))
            }
            _ => Err("Unknown message type"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_charset_size() {
        assert_eq!(
            CHARSET.len(),
            64,
            "Charset must have exactly 64 characters for 6-bit encoding"
        );
    }

    #[test]
    fn test_pack_unpack_simple() {
        let text = "SOS";
        let packed = pack_text(text).unwrap();
        let unpacked = unpack_text(&packed, text.len()).unwrap();
        assert_eq!(unpacked.as_str(), text);
    }

    #[test]
    fn test_pack_unpack_all_chars() {
        let text = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        let packed = pack_text(text).unwrap();
        let unpacked = unpack_text(&packed, text.len()).unwrap();
        assert_eq!(unpacked.as_str(), text);
    }

    #[test]
    fn test_pack_unpack_numbers() {
        let text = "0123456789";
        let packed = pack_text(text).unwrap();
        let unpacked = unpack_text(&packed, text.len()).unwrap();
        assert_eq!(unpacked.as_str(), text);
    }

    #[test]
    fn test_pack_unpack_max_length() {
        let text = "At checkpoint 2, all good. Weather clear. Moving.";
        assert_eq!(text.len(), 50);
        let packed = pack_text(text).unwrap();

        // 50 chars * 6 bits = 300 bits = 37.5 bytes -> 38 bytes
        assert_eq!(packed.len(), 38, "50 characters should pack to 38 bytes");

        let unpacked = unpack_text(&packed, text.len()).unwrap();
        assert_eq!(unpacked.as_str(), text);
    }

    #[test]
    fn test_pack_efficiency() {
        // Test various lengths to verify packing efficiency
        let test_cases = [
            ("SOS", 3),                     // 3 chars * 6 bits = 18 bits = 2.25 -> 3 bytes
            ("At checkpoint 2", 16),        // 16 chars * 6 bits = 96 bits = 12 bytes
            ("Weather is getting bad", 23), // 23 chars * 6 bits = 138 bits = 17.25 -> 18 bytes
        ];

        for (text, expected_chars) in test_cases {
            assert_eq!(text.len(), expected_chars);
            let packed = pack_text(text).unwrap();
            let expected_bytes = (expected_chars * 6 + 7) / 8; // Round up
            assert_eq!(
                packed.len(),
                expected_bytes,
                "Text '{}' ({} chars) should pack to {} bytes",
                text,
                expected_chars,
                expected_bytes
            );
        }
    }

    #[test]
    fn test_unsupported_character() {
        let text = "Hello! 🆘"; // Contains emoji
        let result = pack_text(text);
        assert!(result.is_err(), "Emoji should not be supported");
    }

    #[test]
    fn test_message_serialize_deserialize() {
        let msg = Message::Data(Box::new(DataMessage {
            seq: 42,
            text: "SOS".try_into().unwrap(),
            lat: 37774200,   // 37.7742°
            lon: -122419200, // -122.4192°
        }));

        let mut buf = [0u8; 128];
        let len = msg.serialize(&mut buf).unwrap();

        // Expected: 1 (type) + 1 (seq) + 1 (char_count) + 1 (packed_len) + 3 (packed "SOS") + 4 (lat) + 4 (lon)
        // "SOS" = 3 chars * 6 bits = 18 bits = 3 bytes (rounded up)
        assert_eq!(len, 15, "SOS message should be 15 bytes total");

        let decoded = Message::deserialize(&buf[..len]).unwrap();
        assert_eq!(msg, decoded);
    }

    #[test]
    fn test_message_max_length() {
        let text = "At checkpoint 2, all good. Weather clear. Moving.";
        let msg = Message::Data(Box::new(DataMessage {
            seq: 1,
            text: text.try_into().unwrap(),
            lat: 40712800,
            lon: -74006000,
        }));

        let mut buf = [0u8; 128];
        let len = msg.serialize(&mut buf).unwrap();

        // Expected: 1 (type) + 1 (seq) + 1 (char_count) + 1 (packed_len) + 38 (packed 50 chars) + 4 (lat) + 4 (lon)
        assert_eq!(
            len, 50,
            "Max length message should be 50 bytes (was 61 bytes without packing)"
        );

        let decoded = Message::deserialize(&buf[..len]).unwrap();
        assert_eq!(msg, decoded);
    }

    #[test]
    fn test_ack_unchanged() {
        let msg = Message::Ack(AckMessage { seq: 42 });
        let mut buf = [0u8; 128];
        let len = msg.serialize(&mut buf).unwrap();

        assert_eq!(len, 2, "ACK should still be 2 bytes");

        let decoded = Message::deserialize(&buf[..len]).unwrap();
        assert_eq!(msg, decoded);
    }

    #[test]
    fn test_savings_demonstration() {
        // Demonstrate the actual byte savings
        let test_messages = [
            ("SOS", 3, 14, 15), // Old: 14, New: 15 (overhead dominates short messages)
            ("At checkpoint 2, all good", 26, 37, 32), // Old: 37, New: 32
            ("Weather is getting bad for the hike today", 43, 54, 45), // Old: 54, New: 45
            (
                "At checkpoint 2, all good. Weather clear. Moving.",
                50,
                61,
                50,
            ), // Old: 61, New: 50
        ];

        for (text, char_count, old_size, expected_new_size) in test_messages {
            let msg = Message::Data(Box::new(DataMessage {
                seq: 1,
                text: text.try_into().unwrap(),
                lat: 0,
                lon: 0,
            }));

            let mut buf = [0u8; 128];
            let new_size = msg.serialize(&mut buf).unwrap();

            println!(
                "'{}' ({} chars): Old={} bytes, New={} bytes, Saved={} bytes ({:.1}%)",
                text,
                char_count,
                old_size,
                new_size,
                old_size as i32 - new_size as i32,
                (old_size as f32 - new_size as f32) / old_size as f32 * 100.0
            );

            assert_eq!(new_size, expected_new_size);
        }
    }
}
