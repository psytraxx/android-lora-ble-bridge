#ifndef NODE_DB_H
#define NODE_DB_H

#include <cstdint>
#include <cstddef>
#include "meshtastic/mesh.pb.h"

/**
 * @file NodeDB.h
 * @brief Node database for Meshtastic identity and peer tracking
 *
 * Manages:
 * - Own node identity (number, name, hardware model)
 * - Recently seen nodes (minimal tracking for Phase 1)
 */

namespace NodeDB
{
    /**
     * @brief Initialize node database
     *
     * Generates or loads node number from MAC address.
     * Loads or creates default short/long names.
     *
     * @return true on success, false on failure
     */
    bool init();

    /**
     * @brief Get own node number (derived from MAC address)
     *
     * Node number is the lower 4 bytes of the MAC address,
     * matching Meshtastic's approach.
     *
     * @return 32-bit node number
     */
    uint32_t getOwnNodeNum();

    /**
     * @brief Get own short name (4 characters)
     *
     * Default format: "MXXXX" where XXXX is last 4 hex of node number.
     * Can be customized via NVS storage.
     *
     * @param buffer Output buffer (at least 5 bytes for null terminator)
     * @param bufferSize Size of buffer
     */
    void getOwnShortName(char *buffer, size_t bufferSize);

    /**
     * @brief Get own long name
     *
     * Default format: "Meshtastic XXXX"
     * Can be customized via NVS storage.
     *
     * @param buffer Output buffer (at least 40 bytes recommended)
     * @param bufferSize Size of buffer
     */
    void getOwnLongName(char *buffer, size_t bufferSize);

    /**
     * @brief Get own hardware model
     *
     * Returns PRIVATE_HW for custom hardware.
     *
     * @return Hardware model enum value
     */
    meshtastic_HardwareModel getOwnHardwareModel();

    /**
     * @brief Generate a new unique packet ID
     *
     * Returns a monotonically increasing ID for packet identification.
     * Wraps around after UINT32_MAX.
     *
     * @return 32-bit unique packet ID
     */
    uint32_t generatePacketId();

    /**
     * @brief Set own short name (persisted to NVS)
     *
     * @param name New short name (max 4 characters)
     * @return true on success, false on failure
     */
    bool setOwnShortName(const char *name);

    /**
     * @brief Set own long name (persisted to NVS)
     *
     * @param name New long name (max 39 characters)
     * @return true on success, false on failure
     */
    bool setOwnLongName(const char *name);

    /**
     * @brief Record a seen node (minimal tracking for Phase 1)
     *
     * For now, just logs the node. Full NodeDB tracking comes in Phase 3.
     *
     * @param nodeNum Node number
     * @param rssi Signal strength
     * @param snr Signal-to-noise ratio
     */
    void recordSeenNode(uint32_t nodeNum, int rssi, float snr);

    /**
     * @brief Get reboot count (persisted across reboots)
     * @return Number of reboots since factory reset
     */
    uint32_t getRebootCount();

    /**
     * @brief Increment and persist reboot count (call in setup())
     */
    void incrementRebootCount();

    /**
     * @brief Get number of known nodes in the database
     * @return Count of nodes (including self)
     */
    uint16_t getNodeDBCount();
}

#endif // NODE_DB_H
