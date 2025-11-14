#ifndef PROTOCOL_H
#define PROTOCOL_H

// Use standard C++ headers for portability (works with Arduino, ESP-IDF, Android NDK, etc.)
#include <cstdint>  // uint8_t, int32_t
#include <cstring>  // strlen, memcpy, memset
#include <cstddef>  // size_t
#include <cctype>   // toupper
#include <variant>  // std::variant for efficient message storage

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
    Ack = 0x02,
    WakeUp = 0x03  // Wake-up message (LoRa-only, used to wake devices from deep sleep)
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

/// Wake-up message (LoRa-only, used to wake devices from deep sleep)
struct WakeUpMessage
{
    // No additional data needed - presence of message is the signal
};

/// Message data stored efficiently using std::variant
/// This reduces memory usage from ~64 bytes to ~52 bytes per Message
/// Only the active variant is stored, not all types simultaneously
using MessageData = std::variant<TextMessage, AckMessage, WakeUpMessage>;

/// Union of all message types (now using std::variant for efficiency)
class Message
{
public:
    MessageType type;
    MessageData data;

    // Constructor - default to Text message with empty data
    Message() : type(MessageType::Text), data(TextMessage{}) {}

    // Destructor
    ~Message() = default;

    // Copy operations (explicitly defaulted for clarity)
    Message(const Message&) = default;
    Message& operator=(const Message&) = default;

    // Move operations (enable efficient transfers in queues)
    Message(Message&&) noexcept = default;
    Message& operator=(Message&&) noexcept = default;

    // Accessor methods for variant data (type-safe)
    TextMessage& textData() { return std::get<TextMessage>(data); }
    const TextMessage& textData() const { return std::get<TextMessage>(data); }

    AckMessage& ackData() { return std::get<AckMessage>(data); }
    const AckMessage& ackData() const { return std::get<AckMessage>(data); }

    WakeUpMessage& wakeUpData() { return std::get<WakeUpMessage>(data); }
    const WakeUpMessage& wakeUpData() const { return std::get<WakeUpMessage>(data); }

    // Factory methods
    static Message createText(uint8_t seq, const char *text);
    static Message createTextWithGps(uint8_t seq, const char *text, int32_t lat, int32_t lon);
    static Message createAck(uint8_t seq);
    static Message createWakeUp();

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
