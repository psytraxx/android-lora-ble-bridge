#ifndef APP_HANDLERS_H
#define APP_HANDLERS_H

#include <cstdint>
#include "meshtastic/mesh.pb.h"
#include "meshtastic/admin.pb.h"

/**
 * @file AppHandlers.h
 * @brief Application-specific packet handlers for Meshtastic portnums
 *
 * Phase 3: Mesh Network Integration
 * Phase 4: Admin module, peer NodeDB updates
 *
 * Provides centralized handling for:
 * - NODEINFO_APP: Device announcements and peer discovery
 * - ROUTING_APP: ACK/NACK responses and routing control
 * - TELEMETRY_APP: Device metrics (battery, uptime, etc.)
 * - ADMIN_APP: Remote configuration and device control
 *
 * Also provides periodic broadcasting functions for NodeInfo and Telemetry.
 */

namespace AppHandlers
{
    /**
     * @brief Admin request source (BLE or LoRa)
     *
     * Determines routing for admin responses:
     * - SOURCE_BLE: Response goes to loraToBleQueue (FromRadio)
     * - SOURCE_LORA: Response goes to bleToLoraQueue (ToRadio)
     */
    enum AdminSource
    {
        SOURCE_BLE,
        SOURCE_LORA
    };
    /**
     * @brief Handle incoming NODEINFO_APP packets (portnum=4)
     *
     * Extracts User protobuf from payload and logs peer information.
     * Future: Update NodeDB with peer details.
     *
     * @param data Decoded Data message
     * @param fromNode Sender node number
     */
    void handleNodeInfoApp(const meshtastic_Data &data, uint32_t fromNode);

    /**
     * @brief Handle incoming POSITION_APP packets (portnum=3)
     *
     * Decodes Position protobuf and stores lat/lon/alt in PeerNodeDB.
     *
     * @param data Decoded Data message
     * @param fromNode Sender node number
     */
    void handlePositionApp(const meshtastic_Data &data, uint32_t fromNode);

    /**
     * @brief Handle incoming ROUTING_APP packets (portnum=5)
     *
     * Processes ACK/NACK responses and routing control messages.
     * Logs delivery confirmations for debugging.
     *
     * @param data Decoded Data message
     * @param fromNode Sender node number
     */
    void handleRoutingApp(const meshtastic_Data &data, uint32_t fromNode);

    /**
     * @brief Handle incoming TELEMETRY_APP packets (portnum=67)
     *
     * Decodes and logs device metrics from peer nodes.
     * Future: Store telemetry history for trends.
     *
     * @param data Decoded Data message
     * @param fromNode Sender node number
     */
    void handleTelemetryApp(const meshtastic_Data &data, uint32_t fromNode);

    /**
     * @brief Build and queue a NODEINFO_APP broadcast packet
     *
     * Constructs User protobuf with own identity (short/long name, hw_model)
     * and queues for LoRa transmission. Called by periodic timer (30s).
     *
     * This announces device presence to the mesh network.
     */
    void broadcastNodeInfo();

    /**
     * @brief Build and queue a TELEMETRY_APP broadcast packet
     *
     * Constructs DeviceMetrics protobuf with battery level, voltage, and uptime.
     * Queues for LoRa transmission. Called by periodic timer (60s).
     *
     * This provides health/status information to the mesh network.
     */
    void broadcastTelemetry();

    /**
     * @brief Build and queue a ROUTING_APP ACK packet
     *
     * Constructs Routing protobuf with error_reason field and queues for
     * unicast transmission to the original sender.
     *
     * @param toNode Destination node to ACK
     * @param ackId Original packet ID being acknowledged (set as reply_id)
     * @param error Error code (NONE=success, or specific error reason)
     */
    void sendAck(uint32_t toNode, uint32_t ackId, meshtastic_Routing_Error error);

    /**
     * @brief Handle incoming ADMIN_APP packets (portnum=6)
     *
     * Processes administrative requests:
     * - get_owner_request: Respond with own User info
     * - get_config_request: Respond with Config via ConfigManager
     * - set_owner: Update own short/long name (persisted)
     * - reboot_seconds: Schedule device reboot
     *
     * @param data Decoded Data message
     * @param fromNode Sender node number
     * @param requestId Original packet ID for reply_id
     * @param source Request source (BLE or LoRa) for routing response
     */
    void handleAdminApp(const meshtastic_Data &data, uint32_t fromNode,
                        uint32_t requestId, AdminSource source);
}

#endif // APP_HANDLERS_H
