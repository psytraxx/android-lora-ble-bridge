#include "common/MeshProtocol.h"
#include "common/MeshPacket.h"
#include "common/MeshCrypto.h"
#include "common/NodeDB.h"
#include "common/PeerNodeDB.h"
#include "common/ConfigManager.h"
#include "common/Logging.h"
#include <cstring>

static const char *TAG = "MeshProtocol";

// Multi-channel state
static uint8_t channelKeys[MeshProtocol::MAX_CHANNELS][32];
static uint8_t channelHashes[MeshProtocol::MAX_CHANNELS];
static uint8_t channelCount = 1;
static meshtastic_ChannelSettings channelSettings[MeshProtocol::MAX_CHANNELS];

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

        // Initialize PeerNodeDB (load persisted peers)
        if (!PeerNodeDB::init())
        {
            LOG_W(TAG, "PeerNodeDB init failed (continuing without persistent peers)");
        }

        // Increment reboot count
        NodeDB::incrementRebootCount();

        // Clear all channel state
        memset(channelKeys, 0, sizeof(channelKeys));
        memset(channelHashes, 0, sizeof(channelHashes));
        memset(channelSettings, 0, sizeof(channelSettings));
        channelCount = 1;

        // Set up primary channel (index 0) from default PSK
        channelSettings[0].psk.bytes[0] = 0x01;
        channelSettings[0].psk.size = 1;
        MeshCrypto::expandKey(channelSettings[0].psk.bytes, channelSettings[0].psk.size, channelKeys[0]);
        channelHashes[0] = MeshPacket::calculateChannelHash(
            channelSettings[0].name, channelSettings[0].psk.bytes, channelSettings[0].psk.size);

        // Initialize ConfigManager
        ConfigManager::init();

        LOG_I(TAG, "Meshtastic protocol initialized");
        LOG_I(TAG, "Own node: 0x%08X", NodeDB::getOwnNodeNum());
        LOG_I(TAG, "Channel[0] hash: 0x%02X", channelHashes[0]);

        char shortName[5];
        NodeDB::getOwnShortName(shortName, sizeof(shortName));
        LOG_I(TAG, "Short name: %s", shortName);

        return true;
    }

    void getChannelKey(uint8_t index, uint8_t key[32])
    {
        if (index >= MAX_CHANNELS)
        {
            memset(key, 0, 32);
            return;
        }
        memcpy(key, channelKeys[index], 32);
    }

    uint8_t getChannelHash(uint8_t index)
    {
        if (index >= MAX_CHANNELS) return 0;
        return channelHashes[index];
    }

    uint8_t getChannelCount()
    {
        return channelCount;
    }

    void setChannel(uint8_t index, const meshtastic_ChannelSettings &settings)
    {
        if (index >= MAX_CHANNELS)
        {
            LOG_W(TAG, "Channel index %u out of range", index);
            return;
        }

        channelSettings[index] = settings;

        // Expand PSK to 32-byte key
        if (settings.psk.size > 0)
        {
            MeshCrypto::expandKey(settings.psk.bytes, settings.psk.size, channelKeys[index]);
        }
        else
        {
            // No PSK = use default
            uint8_t defaultPsk = 0x01;
            MeshCrypto::expandKey(&defaultPsk, 1, channelKeys[index]);
        }

        // Compute channel hash
        channelHashes[index] = MeshPacket::calculateChannelHash(
            settings.name, settings.psk.bytes, settings.psk.size);

        // Expand channel count if needed
        if (index >= channelCount)
        {
            channelCount = index + 1;
        }

        LOG_I(TAG, "Channel[%u] set: name='%s', pskLen=%u, hash=0x%02X",
              index, settings.name, settings.psk.size, channelHashes[index]);
    }

    bool findChannelByHash(uint8_t hash, uint8_t &outIndex)
    {
        for (uint8_t i = 0; i < channelCount; i++)
        {
            if (channelHashes[i] == hash)
            {
                outIndex = i;
                return true;
            }
        }
        return false;
    }
}
