#include "Protocol.h"

/// Convert a character to its 6-bit encoded value
/// Automatically converts lowercase to uppercase
int char_to_6bit(char ch)
{
    char upper_ch = toupper(ch);
    for (int i = 0; i < 64; i++)
    {
        if (CHARSET[i] == upper_ch)
        {
            return i;
        }
    }
    return -1; // Character not in supported charset
}

/// Convert a 6-bit value back to a character
char sixbit_to_char(uint8_t val)
{
    if (val < 64)
    {
        return CHARSET[val];
    }
    return '?'; // Invalid value
}

/// Pack text into 6-bit encoded bytes using manual bit manipulation
/// Each character is encoded as 6 bits instead of 8 bits (UTF-8)
/// Lowercase letters are automatically converted to uppercase
/// 50 chars × 6 bits = 300 bits = 37.5 bytes → 38 bytes
int pack_text(const char *text, uint8_t *output, size_t maxLen)
{
    size_t charCount = strlen(text);

    // Calculate required bytes: (charCount * 6 + 7) / 8 (round up)
    size_t byteCount = (charCount * 6 + 7) / 8;

    if (byteCount > maxLen)
    {
        return -1; // Buffer too small
    }

    // Initialize output buffer to zero
    memset(output, 0, byteCount);

    size_t bitOffset = 0;

    for (size_t i = 0; i < charCount; i++)
    {
        int value = char_to_6bit(text[i]);
        if (value < 0)
        {
            return -1; // Invalid character
        }

        // Calculate which byte(s) this 6-bit value spans
        size_t byteIdx = bitOffset / 8;
        size_t bitInByte = bitOffset % 8;

        if (bitInByte <= 2)
        {
            // The 6 bits fit within the current byte
            output[byteIdx] |= (value << (2 - bitInByte));
        }
        else
        {
            // The 6 bits span two bytes
            size_t bitsInFirst = 8 - bitInByte;
            size_t bitsInSecond = 6 - bitsInFirst;

            output[byteIdx] |= (value >> bitsInSecond);
            if (byteIdx + 1 < byteCount)
            {
                output[byteIdx + 1] |= (value << (8 - bitsInSecond));
            }
        }

        bitOffset += 6;
    }

    return byteCount;
}

/// Unpack 6-bit encoded bytes back to text using manual bit manipulation
/// Reads 6 bits at a time and converts to characters (uppercase)
bool unpack_text(const uint8_t *packed, size_t packedLen, uint8_t charCount, char *output, size_t maxOutputLen)
{
    if (charCount >= maxOutputLen)
    {
        return false; // Output buffer too small
    }

    size_t bitOffset = 0;

    for (uint8_t i = 0; i < charCount; i++)
    {
        size_t byteIdx = bitOffset / 8;
        size_t bitInByte = bitOffset % 8;

        if (byteIdx >= packedLen)
        {
            return false; // Insufficient packed data
        }

        uint8_t value;
        if (bitInByte <= 2)
        {
            // The 6 bits are within the current byte
            value = (packed[byteIdx] >> (2 - bitInByte)) & 0x3F;
        }
        else
        {
            // The 6 bits span two bytes
            size_t bitsInFirst = 8 - bitInByte;
            size_t bitsInSecond = 6 - bitsInFirst;

            uint8_t firstPart = packed[byteIdx] & ((1 << bitsInFirst) - 1);
            uint8_t secondPart = 0;
            if (byteIdx + 1 < packedLen)
            {
                secondPart = packed[byteIdx + 1] >> (8 - bitsInSecond);
            }
            else
            {
                return false; // Insufficient packed data
            }

            value = (firstPart << bitsInSecond) | secondPart;
        }

        char ch = sixbit_to_char(value);
        output[i] = ch;

        bitOffset += 6;
    }

    output[charCount] = '\0'; // Null-terminate
    return true;
}

Message Message::createText(uint8_t seq, const char *text)
{
    Message msg;
    msg.type = MessageType::Text;
    msg.textData.seq = seq;
    // Copy text to fixed-size buffer, ensure null-termination
    size_t len = strlen(text);
    if (len > MAX_TEXT_LENGTH)
    {
        len = MAX_TEXT_LENGTH; // Truncate if too long
    }
    memcpy(msg.textData.text, text, len);
    msg.textData.text[len] = '\0';
    msg.textData.hasGps = false;
    msg.textData.lat = 0;
    msg.textData.lon = 0;
    return msg;
}

Message Message::createTextWithGps(uint8_t seq, const char *text, int32_t lat, int32_t lon)
{
    Message msg;
    msg.type = MessageType::Text;
    msg.textData.seq = seq;
    // Copy text to fixed-size buffer, ensure null-termination
    size_t len = strlen(text);
    if (len > MAX_TEXT_LENGTH)
    {
        len = MAX_TEXT_LENGTH; // Truncate if too long
    }
    memcpy(msg.textData.text, text, len);
    msg.textData.text[len] = '\0';
    msg.textData.hasGps = true;
    msg.textData.lat = lat;
    msg.textData.lon = lon;
    return msg;
}

Message Message::createAck(uint8_t seq)
{
    Message msg;
    msg.type = MessageType::Ack;
    msg.ackData.seq = seq;
    return msg;
}

/// Serializes the message into the provided buffer.
/// Returns the number of bytes written on success, or -1 on failure.
int Message::serialize(uint8_t *buf, size_t bufSize) const
{
    switch (type)
    {
    case MessageType::Text:
    {
        size_t textLen = strlen(textData.text);
        if (textLen > MAX_TEXT_LENGTH)
        {
            return -1; // Text too long
        }

        // Pack the text using 6-bit encoding
        uint8_t packedText[64];
        int packedLen = pack_text(textData.text, packedText, sizeof(packedText));
        if (packedLen < 0)
        {
            return -1; // Packing failed
        }

        size_t totalSize = 5 + packedLen; // type + seq + charCount + packedLen + hasGps + packed text
        if (textData.hasGps)
        {
            totalSize += 8; // lat + lon
        }

        if (bufSize < totalSize)
        {
            return -1; // Buffer too small
        }

        buf[0] = static_cast<uint8_t>(MessageType::Text);
        buf[1] = textData.seq;
        buf[2] = textLen;   // Store original character count
        buf[3] = packedLen; // Store packed byte count
        memcpy(buf + 4, packedText, packedLen);
        buf[4 + packedLen] = textData.hasGps ? 1 : 0;

        if (textData.hasGps)
        {
            memcpy(buf + 5 + packedLen, &textData.lat, 4); // Little-endian
            memcpy(buf + 9 + packedLen, &textData.lon, 4); // Little-endian
        }

        return totalSize;
    }

    case MessageType::Ack:
    {
        if (bufSize < 2)
        {
            return -1; // Buffer too small
        }
        buf[0] = static_cast<uint8_t>(MessageType::Ack);
        buf[1] = ackData.seq;
        return 2;
    }
    }

    return -1; // Unknown message type
}

/// Deserializes a message from the provided buffer.
/// Returns true on success, false on failure.
bool Message::deserialize(const uint8_t *buf, size_t len)
{
    if (len == 0)
    {
        return false; // Empty buffer
    }

    switch (buf[0])
    {
    case 0x01:
    { // Text message
        if (len < 5)
        {
            return false; // Buffer too small for text message header
        }

        type = MessageType::Text;
        textData.seq = buf[1];
        uint8_t charCount = buf[2];
        uint8_t packedLen = buf[3];

        if (len < 5 + packedLen)
        {
            return false; // Buffer too small for packed text + hasGps flag
        }

        const uint8_t *packedBytes = buf + 4;
        if (!unpack_text(packedBytes, packedLen, charCount, textData.text, sizeof(textData.text)))
        {
            return false;
        }

        textData.hasGps = (buf[4 + packedLen] != 0);

        if (textData.hasGps)
        {
            if (len < 5 + packedLen + 8)
            {
                return false; // Buffer too small for GPS data
            }
            memcpy(&textData.lat, buf + 5 + packedLen, 4); // Little-endian
            memcpy(&textData.lon, buf + 9 + packedLen, 4); // Little-endian
        }
        else
        {
            textData.lat = 0;
            textData.lon = 0;
        }

        return true;
    }

    case 0x02:
    { // ACK message
        if (len < 2)
        {
            return false; // Buffer too small for ack
        }

        type = MessageType::Ack;
        ackData.seq = buf[1];

        return true;
    }

    default:
        return false; // Unknown message type
    }
}
