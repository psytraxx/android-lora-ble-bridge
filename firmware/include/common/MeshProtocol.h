#ifndef MESH_PROTOCOL_H
#define MESH_PROTOCOL_H

#include <cstdint>
#include <cstddef>
#include "common/Protocol.h" // For Message struct (old protocol)
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"

/**
 * @file MeshProtocol.h
 * @brief Protocol conversion layer between custom Message and Meshtastic
 *
 * This provides compatibility functions to bridge the gap during Phase 1.
 * Allows the existing BLE interface to work with Meshtastic OTA format.
 */

namespace MeshProtocol
{
    /**
     * @brief Convert custom TextMessage to Meshtastic text packet
     *
     * Takes a TextMessage from the BLE interface and creates a Meshtastic
     * TEXT_MESSAGE_APP packet ready for LoRa transmission.
     *
     * @param msg Input TextMessage
     * @param buffer Output buffer for Meshtastic packet
     * @param bufferSize Size of output buffer
     * @return Number of bytes written, or -1 on error
     */
    int textMessageToMeshtastic(const Message &msg, uint8_t *buffer, size_t bufferSize);

    /**
     * @brief Convert received Meshtastic packet to custom Message
     *
     * Parses a Meshtastic packet and extracts text/data into the custom
     * Message format for delivery to BLE clients.
     *
     * @param buffer Input Meshtastic packet buffer
     * @param bufferSize Size of input buffer
     * @param msg Output Message structure
     * @return true on success, false on failure
     */
    bool meshtasticToMessage(const uint8_t *buffer, size_t bufferSize, Message &msg);

    /**
     * @brief Initialize Meshtastic protocol
     *
     * Sets up NodeDB, channel keys, and any persistent state.
     *
     * @return true on success, false on failure
     */
    bool init();
}

#endif // MESH_PROTOCOL_H
