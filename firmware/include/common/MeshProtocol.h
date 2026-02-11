#ifndef MESH_PROTOCOL_H
#define MESH_PROTOCOL_H

#include <cstdint>
#include <cstddef>
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"

/**
 * @file MeshProtocol.h
 * @brief Meshtastic protocol initialization and channel key management
 *
 * Handles NodeDB initialization, channel key setup, and provides
 * the channel key for packet encryption/decryption.
 */

namespace MeshProtocol
{
    /**
     * @brief Initialize Meshtastic protocol
     *
     * Sets up NodeDB, channel keys, and ConfigManager.
     *
     * @return true on success, false on failure
     */
    bool init();

    /**
     * @brief Get the current channel encryption key
     * @param key Output buffer for 32-byte key
     */
    void getChannelKey(uint8_t key[32]);
}

#endif // MESH_PROTOCOL_H
