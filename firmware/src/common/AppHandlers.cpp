#include "common/AppHandlers.h"
#include "common/Logging.h"
#include "common/NodeDB.h"
#include "common/PeerNodeDB.h"
#include "common/ConfigManager.h"
#include "common/MessageQueue.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <Arduino.h>

// Platform-specific includes for PowerManager
#if defined(ARDUINO_ARCH_ESP32)
#include "esp32/PowerManager.h"
#elif defined(ARDUINO_ARCH_NRF52)
#include "nrf52/PowerManager.h"
#endif

static const char *TAG = "AppHandlers";

// External references to queues (defined in unified_main.cpp)
extern MessageQueue<meshtastic_ToRadio> bleToLoraQueue;
extern MessageQueue<meshtastic_FromRadio> loraToBleQueue;
extern uint32_t fromRadioId;

// ============================================================================
// RX Handlers (Incoming Packets)
// ============================================================================

void AppHandlers::handleNodeInfoApp(const meshtastic_Data &data, uint32_t fromNode)
{
    // Decode User protobuf from payload
    meshtastic_User user;
    pb_istream_t stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);

    if (!pb_decode(&stream, meshtastic_User_fields, &user))
    {
        LOG_W(TAG, "Failed to decode User protobuf");
        return;
    }

    LOG_I(TAG, "NodeInfo from %08lx: '%s' (%s), hw=%d",
          (unsigned long)fromNode, user.long_name, user.short_name, user.hw_model);

    // Update PeerNodeDB with user info
    PeerNodeDB::updateFromUser(fromNode, user);
}

void AppHandlers::handleRoutingApp(const meshtastic_Data &data, uint32_t fromNode)
{
    // Decode Routing protobuf from payload
    meshtastic_Routing routing;
    pb_istream_t stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);

    if (!pb_decode(&stream, meshtastic_Routing_fields, &routing))
    {
        LOG_W(TAG, "Failed to decode Routing protobuf");
        return;
    }

    if (routing.which_variant == meshtastic_Routing_error_reason_tag)
    {
        const char *errorStr = (routing.error_reason == meshtastic_Routing_Error_NONE)
                                   ? "ACK"
                                   : "NACK";
        LOG_I(TAG, "Routing %s from %08lx (error=%d)",
              errorStr, (unsigned long)fromNode, routing.error_reason);
    }
    else
    {
        LOG_D(TAG, "Routing variant %lu from %08lx",
              (unsigned long)routing.which_variant, (unsigned long)fromNode);
    }

    // TODO Phase 4: Handle route discovery and error reporting
}

void AppHandlers::handleTelemetryApp(const meshtastic_Data &data, uint32_t fromNode)
{
    // Decode Telemetry protobuf from payload
    meshtastic_Telemetry telemetry;
    pb_istream_t stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);

    if (!pb_decode(&stream, meshtastic_Telemetry_fields, &telemetry))
    {
        LOG_W(TAG, "Failed to decode Telemetry protobuf");
        return;
    }

    if (telemetry.which_variant == meshtastic_Telemetry_device_metrics_tag)
    {
        const auto &m = telemetry.variant.device_metrics;
        LOG_I(TAG, "Telemetry from %08lx: bat=%lu%%, volt=%.2fV, uptime=%lus",
              (unsigned long)fromNode,
              m.has_battery_level ? (unsigned long)m.battery_level : 0UL,
              m.has_voltage ? m.voltage : 0.0f,
              m.has_uptime_seconds ? (unsigned long)m.uptime_seconds : 0UL);
    }
    else if (telemetry.which_variant == meshtastic_Telemetry_environment_metrics_tag)
    {
        LOG_D(TAG, "Environment metrics from %08lx", (unsigned long)fromNode);
    }
    else
    {
        LOG_D(TAG, "Telemetry variant %lu from %08lx",
              (unsigned long)telemetry.which_variant, (unsigned long)fromNode);
    }

    // Update PeerNodeDB with telemetry
    PeerNodeDB::updateFromTelemetry(fromNode, telemetry);
}

// ============================================================================
// TX Broadcasters (Periodic Announcements)
// ============================================================================

void AppHandlers::broadcastNodeInfo()
{
    LOG_D(TAG, "Broadcasting NodeInfo");

    // Build User protobuf
    meshtastic_User user;
    memset(&user, 0, sizeof(user));

    snprintf(user.id, sizeof(user.id), "!%08lx",
             (unsigned long)NodeDB::getOwnNodeNum());
    NodeDB::getOwnShortName(user.short_name, sizeof(user.short_name));
    NodeDB::getOwnLongName(user.long_name, sizeof(user.long_name));
    user.hw_model = NodeDB::getOwnHardwareModel();

    // Encode User to payload bytes
    meshtastic_Data data;
    memset(&data, 0, sizeof(data));
    data.portnum = meshtastic_PortNum_NODEINFO_APP;

    pb_ostream_t stream = pb_ostream_from_buffer(
        data.payload.bytes, sizeof(data.payload.bytes));
    if (!pb_encode(&stream, meshtastic_User_fields, &user))
    {
        LOG_E(TAG, "Failed to encode User protobuf");
        return;
    }
    data.payload.size = stream.bytes_written;

    // Build MeshPacket
    meshtastic_MeshPacket meshPacket;
    memset(&meshPacket, 0, sizeof(meshPacket));
    meshPacket.from = NodeDB::getOwnNodeNum();
    meshPacket.to = 0xFFFFFFFF; // BROADCAST_ADDR
    meshPacket.id = NodeDB::generatePacketId();
    meshPacket.channel = 0;
    meshPacket.hop_limit = 3;
    meshPacket.want_ack = false;
    meshPacket.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    meshPacket.decoded = data;

    // Wrap in ToRadio and queue for transmission
    meshtastic_ToRadio toRadio;
    memset(&toRadio, 0, sizeof(toRadio));
    toRadio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    toRadio.packet = meshPacket;

    if (bleToLoraQueue.push(toRadio))
    {
        LOG_D(TAG, "NodeInfo broadcast queued");
    }
    else
    {
        LOG_W(TAG, "Failed to queue NodeInfo broadcast");
    }
}

void AppHandlers::broadcastTelemetry()
{
    LOG_D(TAG, "Broadcasting Telemetry");

    // Build DeviceMetrics
    meshtastic_DeviceMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));

    // Battery level (0-100%)
    metrics.has_battery_level = true;
    metrics.battery_level = PowerManager::readBatteryLevel();

    // Voltage (in volts as float) - calculate from battery level
    // Approximate: 100% = 4.2V, 0% = 3.0V, linear interpolation
    metrics.has_voltage = true;
    float voltageApprox = 3.0f + (metrics.battery_level / 100.0f) * 1.2f;
    metrics.voltage = voltageApprox;

    // Uptime (seconds since boot)
    metrics.has_uptime_seconds = true;
    metrics.uptime_seconds = millis() / 1000;

    // Build Telemetry wrapper
    meshtastic_Telemetry telemetry;
    memset(&telemetry, 0, sizeof(telemetry));
    telemetry.time = millis() / 1000; // No RTC, use seconds since boot
    telemetry.which_variant = meshtastic_Telemetry_device_metrics_tag;
    telemetry.variant.device_metrics = metrics;

    // Encode Telemetry to payload bytes
    meshtastic_Data data;
    memset(&data, 0, sizeof(data));
    data.portnum = meshtastic_PortNum_TELEMETRY_APP;

    pb_ostream_t stream = pb_ostream_from_buffer(
        data.payload.bytes, sizeof(data.payload.bytes));
    if (!pb_encode(&stream, meshtastic_Telemetry_fields, &telemetry))
    {
        LOG_E(TAG, "Failed to encode Telemetry protobuf");
        return;
    }
    data.payload.size = stream.bytes_written;

    // Build MeshPacket
    meshtastic_MeshPacket meshPacket;
    memset(&meshPacket, 0, sizeof(meshPacket));
    meshPacket.from = NodeDB::getOwnNodeNum();
    meshPacket.to = 0xFFFFFFFF; // BROADCAST_ADDR
    meshPacket.id = NodeDB::generatePacketId();
    meshPacket.channel = 0;
    meshPacket.hop_limit = 3;
    meshPacket.want_ack = false;
    meshPacket.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    meshPacket.decoded = data;

    // Wrap in ToRadio and queue for transmission
    meshtastic_ToRadio toRadio;
    memset(&toRadio, 0, sizeof(toRadio));
    toRadio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    toRadio.packet = meshPacket;

    if (bleToLoraQueue.push(toRadio))
    {
        LOG_D(TAG, "Telemetry broadcast queued (bat=%lu%%, volt=%.2fV, uptime=%lus)",
              (unsigned long)metrics.battery_level, metrics.voltage,
              (unsigned long)metrics.uptime_seconds);
    }
    else
    {
        LOG_W(TAG, "Failed to queue Telemetry broadcast");
    }
}

void AppHandlers::sendAck(uint32_t toNode, uint32_t ackId,
                          meshtastic_Routing_Error error)
{
    LOG_D(TAG, "Sending ACK to %08lx for packet %lu (error=%d)",
          (unsigned long)toNode, (unsigned long)ackId, error);

    // Build Routing protobuf with error_reason
    meshtastic_Routing routing;
    memset(&routing, 0, sizeof(routing));
    routing.which_variant = meshtastic_Routing_error_reason_tag;
    routing.error_reason = error;

    // Encode Routing to payload bytes
    meshtastic_Data data;
    memset(&data, 0, sizeof(data));
    data.portnum = meshtastic_PortNum_ROUTING_APP;

    // Set reply_id to original packet ID (critical for matching ACK to request)
    data.reply_id = ackId;

    pb_ostream_t stream = pb_ostream_from_buffer(
        data.payload.bytes, sizeof(data.payload.bytes));
    if (!pb_encode(&stream, meshtastic_Routing_fields, &routing))
    {
        LOG_E(TAG, "Failed to encode Routing protobuf");
        return;
    }
    data.payload.size = stream.bytes_written;

    // Build MeshPacket (UNICAST to sender)
    meshtastic_MeshPacket meshPacket;
    memset(&meshPacket, 0, sizeof(meshPacket));
    meshPacket.from = NodeDB::getOwnNodeNum();
    meshPacket.to = toNode;
    meshPacket.id = NodeDB::generatePacketId();
    meshPacket.channel = 0;
    meshPacket.hop_limit = 3;
    meshPacket.want_ack = false; // Don't ACK an ACK!
    meshPacket.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    meshPacket.decoded = data;

    // Wrap in ToRadio and queue for transmission
    meshtastic_ToRadio toRadio;
    memset(&toRadio, 0, sizeof(toRadio));
    toRadio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    toRadio.packet = meshPacket;

    if (bleToLoraQueue.push(toRadio))
    {
        LOG_D(TAG, "ACK queued for transmission");
    }
    else
    {
        LOG_W(TAG, "Failed to queue ACK");
    }
}

// ============================================================================
// Admin Handler (Phase 4)
// ============================================================================

/**
 * @brief Helper to send admin response back to requester
 */
static void sendAdminResponse(const meshtastic_AdminMessage &adminMsg,
                              uint32_t toNode, uint32_t requestId,
                              AppHandlers::AdminSource source)
{
    // Encode AdminMessage to payload
    meshtastic_Data data;
    memset(&data, 0, sizeof(data));
    data.portnum = meshtastic_PortNum_ADMIN_APP;
    data.reply_id = requestId;

    pb_ostream_t stream = pb_ostream_from_buffer(
        data.payload.bytes, sizeof(data.payload.bytes));
    if (!pb_encode(&stream, meshtastic_AdminMessage_fields, &adminMsg))
    {
        LOG_E(TAG, "Failed to encode AdminMessage");
        return;
    }
    data.payload.size = stream.bytes_written;

    if (source == AppHandlers::SOURCE_BLE)
    {
        // Response goes back to BLE app (FromRadio)
        meshtastic_FromRadio fromRadio;
        memset(&fromRadio, 0, sizeof(fromRadio));
        fromRadio.id = fromRadioId + 1;
        fromRadio.which_payload_variant = meshtastic_FromRadio_packet_tag;

        // Build MeshPacket
        meshtastic_MeshPacket meshPacket;
        memset(&meshPacket, 0, sizeof(meshPacket));
        meshPacket.from = NodeDB::getOwnNodeNum();
        meshPacket.to = toNode;
        meshPacket.id = NodeDB::generatePacketId();
        meshPacket.channel = 0;
        meshPacket.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        meshPacket.decoded = data;

        fromRadio.packet = meshPacket;

        if (loraToBleQueue.push(fromRadio))
        {
            LOG_D(TAG, "Admin response queued to BLE");
        }
        else
        {
            LOG_W(TAG, "Failed to queue admin response to BLE");
        }
    }
    else // SOURCE_LORA
    {
        // Response goes back over LoRa (ToRadio)
        meshtastic_MeshPacket meshPacket;
        memset(&meshPacket, 0, sizeof(meshPacket));
        meshPacket.from = NodeDB::getOwnNodeNum();
        meshPacket.to = toNode;
        meshPacket.id = NodeDB::generatePacketId();
        meshPacket.channel = 0;
        meshPacket.hop_limit = 3;
        meshPacket.want_ack = false;
        meshPacket.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        meshPacket.decoded = data;

        meshtastic_ToRadio toRadio;
        memset(&toRadio, 0, sizeof(toRadio));
        toRadio.which_payload_variant = meshtastic_ToRadio_packet_tag;
        toRadio.packet = meshPacket;

        if (bleToLoraQueue.push(toRadio))
        {
            LOG_D(TAG, "Admin response queued to LoRa");
        }
        else
        {
            LOG_W(TAG, "Failed to queue admin response to LoRa");
        }
    }
}

/**
 * @brief Platform-specific reboot
 */
static void performReboot()
{
    LOG_I(TAG, "Rebooting device...");
    delay(100); // Allow log to flush

#if defined(ARDUINO_ARCH_ESP32)
    ESP.restart();
#elif defined(ARDUINO_ARCH_NRF52)
    NVIC_SystemReset();
#endif
}

void AppHandlers::handleAdminApp(const meshtastic_Data &data, uint32_t fromNode,
                                 uint32_t requestId, AdminSource source)
{
    // Decode AdminMessage from payload
    meshtastic_AdminMessage adminMsg;
    pb_istream_t stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);

    if (!pb_decode(&stream, meshtastic_AdminMessage_fields, &adminMsg))
    {
        LOG_W(TAG, "Failed to decode AdminMessage");
        return;
    }

    LOG_I(TAG, "Admin request variant=%lu from %08lx",
          (unsigned long)adminMsg.which_payload_variant,
          (unsigned long)fromNode);

    // Handle specific admin variants
    switch (adminMsg.which_payload_variant)
    {
    case meshtastic_AdminMessage_get_owner_request_tag:
    {
        LOG_I(TAG, "Admin get_owner_request");

        // Build response with own User info
        meshtastic_AdminMessage response;
        memset(&response, 0, sizeof(response));
        response.which_payload_variant = meshtastic_AdminMessage_get_owner_response_tag;

        snprintf(response.get_owner_response.id, sizeof(response.get_owner_response.id),
                 "!%08lx", (unsigned long)NodeDB::getOwnNodeNum());
        NodeDB::getOwnShortName(response.get_owner_response.short_name,
                                sizeof(response.get_owner_response.short_name));
        NodeDB::getOwnLongName(response.get_owner_response.long_name,
                               sizeof(response.get_owner_response.long_name));
        response.get_owner_response.hw_model = NodeDB::getOwnHardwareModel();

        sendAdminResponse(response, fromNode, requestId, source);
        break;
    }

    case meshtastic_AdminMessage_get_config_request_tag:
    {
        pb_size_t which = adminMsg.get_config_request;
        LOG_I(TAG, "Admin get_config_request (which=%lu)", (unsigned long)which);

        // Build response via ConfigManager
        meshtastic_AdminMessage response;
        memset(&response, 0, sizeof(response));
        response.which_payload_variant = meshtastic_AdminMessage_get_config_response_tag;

        // Use ConfigManager to populate config with requested variant
        ConfigManager::getConfig(&response.get_config_response, which);

        sendAdminResponse(response, fromNode, requestId, source);
        break;
    }

    case meshtastic_AdminMessage_set_owner_tag:
    {
        LOG_I(TAG, "Admin set_owner");

        const auto &owner = adminMsg.set_owner;

        // Update short name if provided
        if (strlen(owner.short_name) > 0)
        {
            NodeDB::setOwnShortName(owner.short_name);
            LOG_I(TAG, "Updated short name: %s", owner.short_name);
        }

        // Update long name if provided
        if (strlen(owner.long_name) > 0)
        {
            NodeDB::setOwnLongName(owner.long_name);
            LOG_I(TAG, "Updated long name: %s", owner.long_name);
        }

        // Send ACK (no response payload needed for set commands)
        break;
    }

    case meshtastic_AdminMessage_reboot_seconds_tag:
    {
        uint32_t seconds = adminMsg.reboot_seconds;
        LOG_I(TAG, "Admin reboot in %lu seconds", (unsigned long)seconds);

        // Schedule reboot via timer
        if (seconds == 0)
        {
            seconds = 1; // Minimum 1 second
        }

        // Use Arduino built-in timer (simple approach for one-shot)
        static TimerHandle_t rebootTimer = nullptr;
        if (rebootTimer != nullptr)
        {
            xTimerDelete(rebootTimer, 0);
        }

        rebootTimer = xTimerCreate(
            "RebootTimer",
            pdMS_TO_TICKS(seconds * 1000),
            pdFALSE, // One-shot
            nullptr,
            [](TimerHandle_t) { performReboot(); });

        if (rebootTimer != nullptr)
        {
            xTimerStart(rebootTimer, 0);
            LOG_I(TAG, "Reboot timer started");
        }
        else
        {
            LOG_E(TAG, "Failed to create reboot timer");
        }
        break;
    }

    default:
        LOG_W(TAG, "Unsupported admin variant: %lu",
              (unsigned long)adminMsg.which_payload_variant);
        break;
    }
}
