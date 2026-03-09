#include "common/MeshPacket.h"
#include "common/MeshCrypto.h"
#include "common/Logging.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <cstring>

static const char *TAG = "MeshPacket";

namespace MeshPacket
{
    void serializeHeader(const PacketHeader &header, uint8_t *buffer)
    {
        // Little-endian serialization
        buffer[0] = (header.from >> 0) & 0xFF;
        buffer[1] = (header.from >> 8) & 0xFF;
        buffer[2] = (header.from >> 16) & 0xFF;
        buffer[3] = (header.from >> 24) & 0xFF;

        buffer[4] = (header.to >> 0) & 0xFF;
        buffer[5] = (header.to >> 8) & 0xFF;
        buffer[6] = (header.to >> 16) & 0xFF;
        buffer[7] = (header.to >> 24) & 0xFF;

        buffer[8] = (header.id >> 0) & 0xFF;
        buffer[9] = (header.id >> 8) & 0xFF;
        buffer[10] = (header.id >> 16) & 0xFF;
        buffer[11] = (header.id >> 24) & 0xFF;

        buffer[12] = header.flags;
        buffer[13] = header.channelHash;
        buffer[14] = (header.reserved >> 0) & 0xFF;
        buffer[15] = (header.reserved >> 8) & 0xFF;
    }

    bool deserializeHeader(const uint8_t *buffer, PacketHeader &header)
    {
        // Little-endian deserialization
        header.from = ((uint32_t)buffer[0] << 0) |
                      ((uint32_t)buffer[1] << 8) |
                      ((uint32_t)buffer[2] << 16) |
                      ((uint32_t)buffer[3] << 24);

        header.to = ((uint32_t)buffer[4] << 0) |
                    ((uint32_t)buffer[5] << 8) |
                    ((uint32_t)buffer[6] << 16) |
                    ((uint32_t)buffer[7] << 24);

        header.id = ((uint32_t)buffer[8] << 0) |
                    ((uint32_t)buffer[9] << 8) |
                    ((uint32_t)buffer[10] << 16) |
                    ((uint32_t)buffer[11] << 24);

        header.flags = buffer[12];
        header.channelHash = buffer[13];
        header.reserved = ((uint16_t)buffer[14] << 0) |
                          ((uint16_t)buffer[15] << 8);

        return true;
    }

    int createTextMessage(const char *text, uint32_t from, uint32_t to,
                          uint32_t packetId, const uint8_t channelKey[32],
                          uint8_t *buffer, size_t bufferSize)
    {
        if (bufferSize < HEADER_SIZE + 16)
        { // Need space for header + minimum payload
            LOG_E(TAG, "Buffer too small for packet");
            return -1;
        }

        // Create Data protobuf message
        meshtastic_Data data = meshtastic_Data_init_zero;
        data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

        // Copy text into payload
        size_t textLen = strlen(text);
        if (textLen > sizeof(data.payload.bytes))
        {
            textLen = sizeof(data.payload.bytes);
            LOG_W(TAG, "Text truncated to %d bytes", textLen);
        }
        memcpy(data.payload.bytes, text, textLen);
        data.payload.size = textLen;

        // Encode Data protobuf
        uint8_t plaintext[MAX_PAYLOAD_SIZE];
        pb_ostream_t stream = pb_ostream_from_buffer(plaintext, sizeof(plaintext));
        if (!pb_encode(&stream, meshtastic_Data_fields, &data))
        {
            LOG_E(TAG, "Protobuf encode failed: %s", PB_GET_ERROR(&stream));
            return -1;
        }
        size_t plaintextLen = stream.bytes_written;
        LOG_D(TAG, "Protobuf encoded: %d bytes", plaintextLen);

        // Construct nonce for encryption
        uint8_t nonce[16];
        MeshCrypto::constructNonce(packetId, from, nonce);

        // Encrypt payload
        uint8_t ciphertext[MAX_PAYLOAD_SIZE];
        if (!MeshCrypto::encrypt(plaintext, plaintextLen, channelKey, nonce, ciphertext))
        {
            LOG_E(TAG, "Encryption failed");
            return -1;
        }

        // Build packet header
        PacketHeader header;
        header.from = from;
        header.to = to;
        header.id = packetId;
        header.flags = 0;
        header.setChannelIndex(0); // Default channel 0
        header.setHopLimit(3);      // Default hop limit
        header.setWantAck(to != BROADCAST_ADDR); // ACK for unicast only
        header.channelHash = calculateChannelHash("", (const uint8_t *)"\x01", 1);
        header.reserved = 0;

        // Serialize header
        serializeHeader(header, buffer);

        // Append encrypted payload
        size_t totalLen = HEADER_SIZE + plaintextLen;
        if (totalLen > bufferSize)
        {
            LOG_E(TAG, "Packet too large: %d bytes", totalLen);
            return -1;
        }
        memcpy(buffer + HEADER_SIZE, ciphertext, plaintextLen);

        LOG_I(TAG, "Created text message packet: %d bytes total (header=%d, payload=%d)",
              totalLen, HEADER_SIZE, plaintextLen);
        return (int)totalLen;
    }

    bool parsePacket(const uint8_t *buffer, size_t bufferSize,
                     const uint8_t channelKey[32],
                     PacketHeader &header, meshtastic_Data &data)
    {
        if (bufferSize < HEADER_SIZE)
        {
            LOG_E(TAG, "Packet too small: %d bytes", bufferSize);
            return false;
        }

        // Deserialize header
        if (!deserializeHeader(buffer, header))
        {
            LOG_E(TAG, "Header deserialization failed");
            return false;
        }

        LOG_D(TAG, "Packet header: from=%u, to=%u, id=%u, ch=%d, hop=%d, ack=%d",
              header.from, header.to, header.id,
              header.getChannelIndex(), header.getHopLimit(), header.getWantAck());

        // Extract encrypted payload
        size_t payloadLen = bufferSize - HEADER_SIZE;
        if (payloadLen == 0)
        {
            LOG_W(TAG, "Empty payload");
            return false;
        }

        // Construct nonce for decryption
        uint8_t nonce[16];
        MeshCrypto::constructNonce(header.id, header.from, nonce);

        // Decrypt payload
        uint8_t plaintext[MAX_PAYLOAD_SIZE];
        if (!MeshCrypto::decrypt(buffer + HEADER_SIZE, payloadLen, channelKey, nonce, plaintext))
        {
            LOG_E(TAG, "Decryption failed");
            return false;
        }

        // Decode Data protobuf
        pb_istream_t stream = pb_istream_from_buffer(plaintext, payloadLen);
        data = meshtastic_Data_init_zero;
        if (!pb_decode(&stream, meshtastic_Data_fields, &data))
        {
            LOG_E(TAG, "Protobuf decode failed: %s", PB_GET_ERROR(&stream));
            return false;
        }

        LOG_I(TAG, "Parsed packet: portnum=%d, payload=%d bytes",
              data.portnum, data.payload.size);
        return true;
    }

    // CRC8 with polynomial 0x39 — matches official Meshtastic channel hash algorithm
    static uint8_t crc8byte(uint8_t v)
    {
        uint8_t crc = 0;
        for (int i = 0; i < 8; i++)
        {
            if ((v ^ crc) & 0x80)
                crc = (crc << 1) ^ 0x39;
            else
                crc <<= 1;
            v <<= 1;
        }
        return crc;
    }

    uint8_t calculateChannelHash(const char *channelName, const uint8_t *psk, size_t pskLen)
    {
        // XOR of CRC8 of each byte — matches official Meshtastic hash algorithm
        uint8_t hash = 0;

        if (channelName)
        {
            for (size_t i = 0; channelName[i] != '\0'; i++)
                hash ^= crc8byte((uint8_t)channelName[i]);
        }

        for (size_t i = 0; i < pskLen; i++)
            hash ^= crc8byte(psk[i]);

        return hash;
    }
}
