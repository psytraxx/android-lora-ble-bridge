#ifndef MESH_PACKET_H
#define MESH_PACKET_H

#include <cstdint>
#include <cstddef>
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"

/**
 * @file MeshPacket.h
 * @brief Meshtastic over-the-air packet format
 *
 * Implements the Meshtastic LoRa packet structure:
 * - Unencrypted header (16 bytes): from, to, id, flags
 * - Encrypted payload: protobuf Data message
 *
 * Wire format:
 * [0-3]   from (uint32 LE)
 * [4-7]   to (uint32 LE)
 * [8-11]  id (uint32 LE)
 * [12]    flags (channel_index:4 | hop_limit:3 | want_ack:1)
 * [13]    channel_hash
 * [14-15] reserved (padding/future use)
 * [16+]   encrypted Data protobuf
 */

namespace MeshPacket
{
    /// Size of unencrypted packet header
    constexpr size_t HEADER_SIZE = 16;

    /// Maximum Meshtastic payload size (based on LoRa MTU - header)
    constexpr size_t MAX_PAYLOAD_SIZE = 237;

    /// Maximum total packet size
    constexpr size_t MAX_PACKET_SIZE = HEADER_SIZE + MAX_PAYLOAD_SIZE;

    /// Broadcast address (all nodes)
    constexpr uint32_t BROADCAST_ADDR = 0xFFFFFFFF;

    /**
     * @brief Packet header structure (unencrypted, 16 bytes)
     */
    struct PacketHeader
    {
        uint32_t from;        ///< Sender node number
        uint32_t to;          ///< Destination node number (0xFFFFFFFF = broadcast)
        uint32_t id;          ///< Unique packet identifier
        uint8_t flags;        ///< channel_index(4) | hop_limit(3) | want_ack(1)
        uint8_t channelHash;  ///< Channel identification hash
        uint16_t reserved;    ///< Reserved for future use

        /// Extract channel index from flags (bits 4-7)
        uint8_t getChannelIndex() const { return (flags >> 4) & 0x0F; }

        /// Extract hop limit from flags (bits 1-3)
        uint8_t getHopLimit() const { return (flags >> 1) & 0x07; }

        /// Extract want_ack from flags (bit 0)
        bool getWantAck() const { return (flags & 0x01) != 0; }

        /// Set channel index in flags
        void setChannelIndex(uint8_t idx)
        {
            flags = (flags & 0x0F) | ((idx & 0x0F) << 4);
        }

        /// Set hop limit in flags
        void setHopLimit(uint8_t limit)
        {
            flags = (flags & 0xF1) | ((limit & 0x07) << 1);
        }

        /// Set want_ack in flags
        void setWantAck(bool ack)
        {
            if (ack)
                flags |= 0x01;
            else
                flags &= ~0x01;
        }
    };

    /**
     * @brief Serialize packet header to wire format (little-endian)
     *
     * @param header Packet header to serialize
     * @param buffer Output buffer (must be at least HEADER_SIZE bytes)
     */
    void serializeHeader(const PacketHeader &header, uint8_t *buffer);

    /**
     * @brief Deserialize packet header from wire format
     *
     * @param buffer Input buffer containing header
     * @param header Output packet header
     * @return true on success, false on failure
     */
    bool deserializeHeader(const uint8_t *buffer, PacketHeader &header);

    /**
     * @brief Create a text message packet
     *
     * Creates a MeshPacket with TEXT_MESSAGE_APP portnum.
     * Encrypts the Data payload using the channel key.
     *
     * @param text Message text (null-terminated)
     * @param from Sender node number
     * @param to Destination node number (or BROADCAST_ADDR)
     * @param packetId Unique packet identifier
     * @param channelKey 32-byte AES-256 key for encryption
     * @param buffer Output buffer for complete packet
     * @param bufferSize Size of output buffer
     * @return Number of bytes written, or -1 on error
     */
    int createTextMessage(const char *text, uint32_t from, uint32_t to,
                          uint32_t packetId, const uint8_t channelKey[32],
                          uint8_t *buffer, size_t bufferSize);

    /**
     * @brief Parse an incoming packet
     *
     * Deserializes header, decrypts payload, and decodes protobuf Data.
     *
     * @param buffer Input packet buffer
     * @param bufferSize Size of input packet
     * @param channelKey 32-byte AES-256 key for decryption
     * @param header Output packet header
     * @param data Output decoded Data message
     * @return true on success, false on failure
     */
    bool parsePacket(const uint8_t *buffer, size_t bufferSize,
                     const uint8_t channelKey[32],
                     PacketHeader &header, meshtastic_Data &data);

    /**
     * @brief Calculate channel hash from channel name and PSK
     *
     * Simple 8-bit hash for OTA channel identification.
     * Based on Meshtastic's channel hash algorithm.
     *
     * @param channelName Channel name string
     * @param psk Pre-shared key
     * @param pskLen Length of PSK
     * @return 8-bit channel hash
     */
    uint8_t calculateChannelHash(const char *channelName, const uint8_t *psk, size_t pskLen);
}

#endif // MESH_PACKET_H
