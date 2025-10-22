#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>

/// Maximum text length in characters for optimal long-range LoRa transmission.
/// With 6-bit packing: 50 chars = 38 bytes (was 50 bytes)
/// With SF10, BW125, 433MHz: 50 bytes (12 header + 38 text) = ~600ms Time on Air
const uint8_t MAX_TEXT_LENGTH = 50;

/// Character set for 6-bit encoding (64 characters)
/// Index maps to 6-bit value: 0-63
/// UPPERCASE ONLY: Space + A-Z (26) + 0-9 (10) + punctuation (27)
const char CHARSET[65] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:;'\"@#$%&*()[]{}=+/<>_";

/// Message types
enum class MessageType : uint8_t
{
    Text = 0x01,
    Ack = 0x02
};

/// Text message with optional GPS coordinates
struct TextMessage
{
    uint8_t seq;
    char text[MAX_TEXT_LENGTH + 1]; // Fixed-size buffer for text (null-terminated)
    bool hasGps;                    // Whether GPS coordinates are included
    int32_t lat;                    // latitude * 1_000_000 (only valid if hasGps=true)
    int32_t lon;                    // longitude * 1_000_000 (only valid if hasGps=true)
};

/// Acknowledgment message
struct AckMessage
{
    uint8_t seq;
};

/// Union of all message types
class Message
{
public:
    MessageType type;

    // Store all message data separately (only one will be used based on type)
    TextMessage textData;
    AckMessage ackData;

    Message() : type(MessageType::Text) {}

    static Message createText(uint8_t seq, const char *text);
    static Message createTextWithGps(uint8_t seq, const char *text, int32_t lat, int32_t lon);
    static Message createAck(uint8_t seq);

    /// Serializes the message into the provided buffer.
    /// Returns the number of bytes written on success, or -1 on failure.
    int serialize(uint8_t *buf, size_t bufSize) const;

    /// Deserializes a message from the provided buffer.
    /// Returns true on success, false on failure.
    bool deserialize(const uint8_t *buf, size_t len);
};

/// Convert a character to its 6-bit encoded value
/// Automatically converts lowercase to uppercase
int char_to_6bit(char ch);

/// Convert a 6-bit value back to a character
char sixbit_to_char(uint8_t val);

/// Pack text into 6-bit encoded bytes
/// Returns the number of bytes written, or -1 on error
int pack_text(const char *text, uint8_t *output, size_t maxLen);

/// Unpack 6-bit encoded bytes back to text
/// Returns true on success, false on error
bool unpack_text(const uint8_t *packed, size_t packedLen, uint8_t charCount, char *output, size_t maxOutputLen);

#endif // PROTOCOL_H
