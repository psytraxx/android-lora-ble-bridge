#include "common/MeshProtocol.h"
#include "common/MeshPacket.h"
#include "common/MeshCrypto.h"
#include "common/NodeDB.h"
#include "common/PeerNodeDB.h"
#include "common/ConfigManager.h"
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

        // Initialize PeerNodeDB (load persisted peers)
        if (!PeerNodeDB::init())
        {
            LOG_W(TAG, "PeerNodeDB init failed (continuing without persistent peers)");
        }

        // Increment reboot count
        NodeDB::incrementRebootCount();

        // Get default channel key
        MeshCrypto::getDefaultChannelKey(channelKey);

        // Initialize ConfigManager
        ConfigManager::init();

        LOG_I(TAG, "Meshtastic protocol initialized");
        LOG_I(TAG, "Own node: 0x%08X", NodeDB::getOwnNodeNum());

        char shortName[5];
        NodeDB::getOwnShortName(shortName, sizeof(shortName));
        LOG_I(TAG, "Short name: %s", shortName);

        return true;
    }

    void getChannelKey(uint8_t key[32])
    {
        memcpy(key, channelKey, 32);
    }
}
