#ifndef PEER_NODE_DB_H
#define PEER_NODE_DB_H

#include <cstdint>
#include <cstddef>
#include "meshtastic/mesh.pb.h"

/**
 * @file PeerNodeDB.h
 * @brief Persistent database of peer mesh nodes
 *
 * Phase 4: Stores up to 50 peers with identity, signal info, and telemetry.
 * Persists to NVS (ESP32) or LittleFS (nRF52) for survival across reboots.
 *
 * Total RAM: 50 × 64 bytes = 3.2 KB
 */

namespace PeerNodeDB
{
    /**
     * @brief Peer node record (64 bytes per node)
     */
    struct PeerNode
    {
        uint32_t nodeNum;        // 4B - Unique node identifier
        char shortName[5];       // 5B - Short name (4 chars + null)
        char longName[40];       // 40B - Long name (39 chars + null)
        int32_t hwModel;         // 4B - Hardware model enum (stored as int32)
        uint32_t lastHeard;      // 4B - Last reception time (millis()/1000)
        int16_t snr;             // 2B - SNR × 10 fixed-point (e.g., 85 = 8.5 dB)
        int16_t rssi;            // 2B - RSSI in dBm
        uint8_t hopsAway;        // 1B - Hop count (0 = direct)
        uint8_t batteryLevel;    // 1B - Battery level 0-100%
        bool hasUser;            // 1B - Whether User info has been received
        uint8_t _padding[1];     // 1B - Align to 64 bytes
    };

    constexpr size_t MAX_PEER_NODES = 50;

    /**
     * @brief Initialize peer database (load from storage)
     * @return true on success, false on failure
     */
    bool init();

    /**
     * @brief Save database to storage (debounced via dirty flag)
     *
     * Call after updates to mark dirty. Actual flush occurs:
     * - On periodic timer (5 minutes)
     * - On deep sleep entry
     */
    void save();

    /**
     * @brief Force immediate save (bypasses debounce)
     */
    void saveNow();

    /**
     * @brief Get or create peer node entry
     *
     * If full, evicts the node with oldest lastHeard timestamp.
     *
     * @param nodeNum Node number to find or create
     * @return Pointer to PeerNode entry (never null)
     */
    PeerNode *getOrCreateNode(uint32_t nodeNum);

    /**
     * @brief Get read-only peer node entry
     * @param nodeNum Node number to find
     * @return Pointer to PeerNode, or nullptr if not found
     */
    const PeerNode *getNode(uint32_t nodeNum);

    /**
     * @brief Remove peer node from database
     * @param nodeNum Node number to remove
     */
    void removeNode(uint32_t nodeNum);

    /**
     * @brief Update peer from NODEINFO_APP User protobuf
     *
     * Sets name, hwModel, and marks hasUser=true.
     * Automatically calls save() to mark dirty.
     *
     * @param nodeNum Node number
     * @param user Decoded User protobuf
     */
    void updateFromUser(uint32_t nodeNum, const meshtastic_User &user);

    /**
     * @brief Update peer from TELEMETRY_APP DeviceMetrics
     *
     * Sets batteryLevel from telemetry data.
     * Automatically calls save() to mark dirty.
     *
     * @param nodeNum Node number
     * @param telemetry Decoded Telemetry protobuf
     */
    void updateFromTelemetry(uint32_t nodeNum, const meshtastic_Telemetry &telemetry);

    /**
     * @brief Update signal information for peer
     *
     * Sets RSSI, SNR, hopsAway, and updates lastHeard to current time.
     * Automatically calls save() to mark dirty.
     *
     * @param nodeNum Node number
     * @param rssi Signal strength in dBm
     * @param snr Signal-to-noise ratio in dB (float)
     * @param hops Hop count (0 = direct reception)
     */
    void updateSignalInfo(uint32_t nodeNum, int rssi, float snr, uint8_t hops);

    /**
     * @brief Get number of stored peers
     * @return Count of peer nodes
     */
    uint16_t getCount();

    /**
     * @brief Get peer by index (for iteration)
     * @param idx Index (0 to getCount()-1)
     * @return Pointer to PeerNode, or nullptr if out of bounds
     */
    const PeerNode *getNodeByIndex(uint16_t idx);

    /**
     * @brief Build NodeInfo protobuf for BLE config download
     *
     * Populates a meshtastic_NodeInfo structure for sending to app.
     *
     * @param idx Index (0 to getCount()-1)
     * @param nodeInfo Output NodeInfo protobuf
     * @return true on success, false if index out of bounds
     */
    bool getNodeInfo(uint16_t idx, meshtastic_NodeInfo *nodeInfo);
}

#endif // PEER_NODE_DB_H
