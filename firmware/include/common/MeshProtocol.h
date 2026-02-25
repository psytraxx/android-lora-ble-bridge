#ifndef MESH_PROTOCOL_H
#define MESH_PROTOCOL_H

#include <cstdint>
#include <cstddef>
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "meshtastic/channel.pb.h"

/**
 * @file MeshProtocol.h
 * @brief Meshtastic protocol initialization and channel key management
 *
 * Handles NodeDB initialization, channel key setup, and provides
 * the channel key for packet encryption/decryption.
 * Supports up to 8 channels with independent PSKs.
 */

namespace MeshProtocol
{
    constexpr uint8_t MAX_CHANNELS = 8;

    /**
     * @brief Initialize Meshtastic protocol
     *
     * Sets up NodeDB, channel keys, and ConfigManager.
     *
     * @return true on success, false on failure
     */
    bool init();

    /**
     * @brief Get the encryption key for a channel by index
     * @param index Channel index (0 = primary)
     * @param key Output buffer for 32-byte key
     */
    void getChannelKey(uint8_t index, uint8_t key[32]);

    /**
     * @brief Get the OTA hash for a channel
     * @param index Channel index
     * @return 8-bit channel hash
     */
    uint8_t getChannelHash(uint8_t index);

    /**
     * @brief Get the number of configured channels
     * @return Channel count (1-8)
     */
    uint8_t getChannelCount();

    /**
     * @brief Set/update a channel's settings
     * @param index Channel index (0-7)
     * @param settings Channel settings (name, PSK, etc.)
     */
    void setChannel(uint8_t index, const meshtastic_ChannelSettings &settings);

    /**
     * @brief Find a channel by its OTA hash
     * @param hash Channel hash from packet header
     * @param outIndex Output channel index if found
     * @return true if found, false if no match
     */
    bool findChannelByHash(uint8_t hash, uint8_t &outIndex);
}

#endif // MESH_PROTOCOL_H
