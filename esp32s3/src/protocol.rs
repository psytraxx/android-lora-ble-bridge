use alloc::boxed::Box;
use heapless::String;

/// Maximum text length in characters for optimal long-range LoRa transmission.
/// With SF10, BW125, 433MHz: 61 bytes (11 header + 50 text) = ~700ms Time on Air
/// This allows ~51 messages per hour within 1% duty cycle limits.
pub const MAX_TEXT_LENGTH: usize = 50;

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
    pub fn serialize(&self, buf: &mut [u8]) -> Result<usize, &'static str> {
        match self {
            Message::Data(data) => {
                if data.text.len() > 255 {
                    return Err("Text too long");
                }
                if buf.len() < 11 + data.text.len() {
                    return Err("Buffer too small");
                }
                buf[0] = MessageType::Data as u8;
                buf[1] = data.seq;
                buf[2] = data.text.len() as u8;
                buf[3..3 + data.text.len()].copy_from_slice(data.text.as_bytes());
                let mut offset = 3 + data.text.len();
                buf[offset..offset + 4].copy_from_slice(&data.lat.to_le_bytes());
                offset += 4;
                buf[offset..offset + 4].copy_from_slice(&data.lon.to_le_bytes());
                Ok(11 + data.text.len())
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
    pub fn deserialize(buf: &[u8]) -> Result<Self, &'static str> {
        if buf.is_empty() {
            return Err("Empty buffer");
        }
        match buf[0] {
            0x01 => {
                if buf.len() < 11 {
                    return Err("Buffer too small for data message");
                }
                let seq = buf[1];
                let text_len = buf[2] as usize;
                if buf.len() < 11 + text_len {
                    return Err("Buffer too small for text");
                }
                let text_bytes = &buf[3..3 + text_len];
                let text = core::str::from_utf8(text_bytes)
                    .map_err(|_| "Invalid UTF-8")?
                    .try_into()
                    .map_err(|_| "Text too long")?;
                let mut offset = 3 + text_len;
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
