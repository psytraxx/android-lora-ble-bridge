#include "common/MeshProtocol.h"
#include "common/MeshPacket.h"
#include "common/MeshCrypto.h"
#include "common/NodeDB.h"
#include "common/Logging.h"
#include <cstring>

static const char *TAG = "MeshProtocol";

// Channel key (default Meshtastic channel)
static uint8_t channelKey[32];

namespace MeshProtocol
{
    bool init()
    {
        LOG_I(TAG, "Initializing Meshtastic protocol");

        // Initialize NodeDB (generates node number from MAC)
        if (!NodeDB::init())
        {
            LOG_E(TAG, "NodeDB init failed");
            return false;
        }

        // Get default channel key
        MeshCrypto::getDefaultChannelKey(channelKey);

        LOG_I(TAG, "Meshtastic protocol initialized");
        LOG_I(TAG, "Own node: 0x%08X", NodeDB::getOwnNodeNum());

        char shortName[5];
        NodeDB::getOwnShortName(shortName, sizeof(shortName));
        LOG_I(TAG, "Short name: %s", shortName);

        return true;
    }

    int textMessageToMeshtastic(const Message &msg, uint8_t *buffer, size_t bufferSize)
    {
        if (msg.type != MessageType::Text)
        {
            LOG_W(TAG, "Cannot convert non-text message to Meshtastic");
            return -1;
        }

        // Generate packet ID
        uint32_t packetId = NodeDB::generatePacketId();

        // Create Meshtastic text message packet
        // For now, broadcast to all nodes (Phase 1)
        int len = MeshPacket::createTextMessage(
            msg.textData.text,
            NodeDB::getOwnNodeNum(),
            MeshPacket::BROADCAST_ADDR, // Broadcast
            packetId,
            channelKey,
            buffer,
            bufferSize);

        if (len > 0)
        {
            LOG_I(TAG, "Converted text to Meshtastic: %d bytes, pktId=%u",
                  len, packetId);
        }

        return len;
    }

    bool meshtasticToMessage(const uint8_t *buffer, size_t bufferSize, Message &msg)
    {
        // Parse Meshtastic packet
        MeshPacket::PacketHeader header;
        meshtastic_Data data;

        if (!MeshPacket::parsePacket(buffer, bufferSize, channelKey, header, data))
        {
            LOG_E(TAG, "Failed to parse Meshtastic packet");
            return false;
        }

        // Record the sender node
        NodeDB::recordSeenNode(header.from, -100, 0.0); // RSSI/SNR updated separately

        // Convert based on portnum
        if (data.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP)
        {
            // Extract text message
            msg = Message::createText(
                0, // seq (not used in Meshtastic)
                (const char *)data.payload.bytes);

            // Null-terminate the text
            size_t textLen = data.payload.size;
            if (textLen >= sizeof(msg.textData.text))
            {
                textLen = sizeof(msg.textData.text) - 1;
            }
            msg.textData.text[textLen] = '\0';

            LOG_I(TAG, "Converted Meshtastic TEXT_MESSAGE to Message: \"%s\"",
                  msg.textData.text);
            return true;
        }
        else if (data.portnum == meshtastic_PortNum_ROUTING_APP)
        {
            // TODO: Handle ACKs in Phase 3
            LOG_D(TAG, "Received ROUTING_APP message (ACK), ignoring for now");
            return false;
        }
        else
        {
            LOG_D(TAG, "Received unsupported portnum: %d", data.portnum);
            return false;
        }
    }
}
