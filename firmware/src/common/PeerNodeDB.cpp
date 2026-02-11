#include "common/PeerNodeDB.h"
#include "common/Logging.h"
#include <cstring>
#include <Arduino.h>

// Platform-specific includes
#if defined(ARDUINO_ARCH_ESP32)
#include <nvs_flash.h>
#include <nvs.h>
#elif defined(ARDUINO_ARCH_NRF52)
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;
#endif

static const char *TAG = "PeerNodeDB";

namespace PeerNodeDB
{
    // RAM storage
    static PeerNode peers[MAX_PEER_NODES];
    static uint16_t peerCount = 0;
    static bool dirty = false;
    static bool initialized = false;

#if defined(ARDUINO_ARCH_ESP32)
    static const char *NVS_NAMESPACE = "peer_db";
    static const char *NVS_KEY_PEERS = "peers";
    static const char *NVS_KEY_COUNT = "peer_cnt";
    static nvs_handle_t nvsHandle = 0;
#elif defined(ARDUINO_ARCH_NRF52)
    static const char *DB_DIR = "/peerdb";
    static const char *DB_FILE = "/peerdb/peers.bin";
#endif

    // Forward declarations
    static void loadFromStorage();
    static void saveToStorage();

    bool init()
    {
        LOG_I(TAG, "Initializing PeerNodeDB");

        memset(peers, 0, sizeof(peers));
        peerCount = 0;
        dirty = false;

#if defined(ARDUINO_ARCH_ESP32)
        // Initialize NVS
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            LOG_W(TAG, "NVS partition needs erasing");
            nvs_flash_erase();
            err = nvs_flash_init();
        }

        if (err != ESP_OK)
        {
            LOG_E(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
            return false;
        }

        err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
        if (err != ESP_OK)
        {
            LOG_E(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
            return false;
        }
#elif defined(ARDUINO_ARCH_NRF52)
        // Initialize LittleFS
        InternalFS.begin();

        if (!InternalFS.exists(DB_DIR))
        {
            InternalFS.mkdir(DB_DIR);
        }
#endif

        loadFromStorage();
        initialized = true;

        LOG_I(TAG, "PeerNodeDB initialized: %u peers loaded", peerCount);
        return true;
    }

    void save()
    {
        dirty = true;
    }

    void saveNow()
    {
        if (!initialized)
        {
            return;
        }
        saveToStorage();
        dirty = false;
    }

    PeerNode *getOrCreateNode(uint32_t nodeNum)
    {
        // Search for existing entry
        for (uint16_t i = 0; i < peerCount; i++)
        {
            if (peers[i].nodeNum == nodeNum)
            {
                return &peers[i];
            }
        }

        // Create new entry
        if (peerCount < MAX_PEER_NODES)
        {
            // Add to end
            PeerNode *node = &peers[peerCount];
            memset(node, 0, sizeof(PeerNode));
            node->nodeNum = nodeNum;
            node->lastHeard = millis() / 1000;
            peerCount++;
            LOG_D(TAG, "Created new peer entry: 0x%08lx (%u/%u)",
                  (unsigned long)nodeNum, peerCount, MAX_PEER_NODES);
            return node;
        }
        else
        {
            // Evict oldest (find minimum lastHeard)
            uint16_t oldestIdx = 0;
            uint32_t oldestTime = peers[0].lastHeard;
            for (uint16_t i = 1; i < peerCount; i++)
            {
                if (peers[i].lastHeard < oldestTime)
                {
                    oldestTime = peers[i].lastHeard;
                    oldestIdx = i;
                }
            }

            LOG_W(TAG, "Peer DB full, evicting oldest: 0x%08lx (last heard %lus ago)",
                  (unsigned long)peers[oldestIdx].nodeNum,
                  (unsigned long)(millis() / 1000 - oldestTime));

            PeerNode *node = &peers[oldestIdx];
            memset(node, 0, sizeof(PeerNode));
            node->nodeNum = nodeNum;
            node->lastHeard = millis() / 1000;
            return node;
        }
    }

    const PeerNode *getNode(uint32_t nodeNum)
    {
        for (uint16_t i = 0; i < peerCount; i++)
        {
            if (peers[i].nodeNum == nodeNum)
            {
                return &peers[i];
            }
        }
        return nullptr;
    }

    void removeNode(uint32_t nodeNum)
    {
        for (uint16_t i = 0; i < peerCount; i++)
        {
            if (peers[i].nodeNum == nodeNum)
            {
                // Shift remaining entries
                for (uint16_t j = i; j < peerCount - 1; j++)
                {
                    peers[j] = peers[j + 1];
                }
                peerCount--;
                dirty = true;
                LOG_I(TAG, "Removed peer: 0x%08lx", (unsigned long)nodeNum);
                return;
            }
        }
    }

    void updateFromUser(uint32_t nodeNum, const meshtastic_User &user)
    {
        PeerNode *node = getOrCreateNode(nodeNum);
        if (!node)
        {
            return;
        }

        strncpy(node->shortName, user.short_name, sizeof(node->shortName) - 1);
        node->shortName[sizeof(node->shortName) - 1] = '\0';

        strncpy(node->longName, user.long_name, sizeof(node->longName) - 1);
        node->longName[sizeof(node->longName) - 1] = '\0';

        node->hwModel = user.hw_model;
        node->hasUser = true;
        node->lastHeard = millis() / 1000;

        LOG_D(TAG, "Updated peer from User: 0x%08lx '%s' (%s)",
              (unsigned long)nodeNum, node->longName, node->shortName);

        save();
    }

    void updateFromTelemetry(uint32_t nodeNum, const meshtastic_Telemetry &telemetry)
    {
        PeerNode *node = getOrCreateNode(nodeNum);
        if (!node)
        {
            return;
        }

        if (telemetry.which_variant == meshtastic_Telemetry_device_metrics_tag)
        {
            const auto &metrics = telemetry.variant.device_metrics;
            if (metrics.has_battery_level)
            {
                node->batteryLevel = metrics.battery_level;
                LOG_D(TAG, "Updated peer battery: 0x%08lx = %u%%",
                      (unsigned long)nodeNum, node->batteryLevel);
            }
        }

        node->lastHeard = millis() / 1000;
        save();
    }

    void updateSignalInfo(uint32_t nodeNum, int rssi, float snr, uint8_t hops)
    {
        PeerNode *node = getOrCreateNode(nodeNum);
        if (!node)
        {
            return;
        }

        node->rssi = rssi;
        node->snr = (int16_t)(snr * 10.0f); // Fixed-point: SNR × 10
        node->hopsAway = hops;
        node->lastHeard = millis() / 1000;

        LOG_D(TAG, "Updated peer signal: 0x%08lx RSSI=%d SNR=%.1f hops=%u",
              (unsigned long)nodeNum, rssi, snr, hops);

        save();
    }

    uint16_t getCount()
    {
        return peerCount;
    }

    const PeerNode *getNodeByIndex(uint16_t idx)
    {
        if (idx >= peerCount)
        {
            return nullptr;
        }
        return &peers[idx];
    }

    bool getNodeInfo(uint16_t idx, meshtastic_NodeInfo *nodeInfo)
    {
        if (idx >= peerCount || !nodeInfo)
        {
            return false;
        }

        const PeerNode *node = &peers[idx];
        memset(nodeInfo, 0, sizeof(meshtastic_NodeInfo));

        nodeInfo->num = node->nodeNum;
        nodeInfo->has_user = node->hasUser;

        if (node->hasUser)
        {
            snprintf(nodeInfo->user.id, sizeof(nodeInfo->user.id),
                     "!%08lx", (unsigned long)node->nodeNum);
            strncpy(nodeInfo->user.long_name, node->longName,
                    sizeof(nodeInfo->user.long_name) - 1);
            strncpy(nodeInfo->user.short_name, node->shortName,
                    sizeof(nodeInfo->user.short_name) - 1);
            nodeInfo->user.hw_model = (meshtastic_HardwareModel)node->hwModel;
        }

        nodeInfo->has_position = false;
        nodeInfo->has_device_metrics = (node->batteryLevel > 0);

        if (nodeInfo->has_device_metrics)
        {
            nodeInfo->device_metrics.battery_level = node->batteryLevel;
            nodeInfo->device_metrics.has_battery_level = true;
        }

        nodeInfo->snr = (float)node->snr / 10.0f;
        nodeInfo->last_heard = node->lastHeard;
        nodeInfo->hops_away = node->hopsAway;

        return true;
    }

    // ========================================================================
    // Platform-specific persistence
    // ========================================================================

    static void loadFromStorage()
    {
#if defined(ARDUINO_ARCH_ESP32)
        // Load from NVS blob
        size_t requiredSize = sizeof(peers);
        esp_err_t err = nvs_get_blob(nvsHandle, NVS_KEY_PEERS, peers, &requiredSize);
        if (err == ESP_OK && requiredSize == sizeof(peers))
        {
            uint32_t count = 0;
            err = nvs_get_u32(nvsHandle, NVS_KEY_COUNT, &count);
            if (err == ESP_OK && count <= MAX_PEER_NODES)
            {
                peerCount = count;
                LOG_I(TAG, "Loaded %u peers from NVS", peerCount);
            }
            else
            {
                LOG_W(TAG, "Invalid peer count in NVS, resetting");
                peerCount = 0;
            }
        }
        else if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            LOG_I(TAG, "No persisted peers found (first boot)");
        }
        else
        {
            LOG_W(TAG, "Failed to load peers from NVS: %s", esp_err_to_name(err));
        }

#elif defined(ARDUINO_ARCH_NRF52)
        // Load from LittleFS
        if (!InternalFS.exists(DB_FILE))
        {
            LOG_I(TAG, "No persisted peers found (first boot)");
            return;
        }

        File file = InternalFS.open(DB_FILE, FILE_O_READ);
        if (!file)
        {
            LOG_E(TAG, "Failed to open peer DB file");
            return;
        }

        // Read count (4 bytes)
        uint32_t count = 0;
        if (file.read((uint8_t *)&count, sizeof(count)) != sizeof(count))
        {
            LOG_E(TAG, "Failed to read peer count");
            file.close();
            return;
        }

        if (count > MAX_PEER_NODES)
        {
            LOG_W(TAG, "Invalid peer count in file: %lu, resetting", (unsigned long)count);
            file.close();
            return;
        }

        // Read peer array
        size_t toRead = count * sizeof(PeerNode);
        if (file.read((uint8_t *)peers, toRead) != toRead)
        {
            LOG_E(TAG, "Failed to read peer data");
            file.close();
            return;
        }

        file.close();
        peerCount = count;
        LOG_I(TAG, "Loaded %u peers from LittleFS", peerCount);
#endif
    }

    static void saveToStorage()
    {
        if (!initialized)
        {
            return;
        }

#if defined(ARDUINO_ARCH_ESP32)
        // Save to NVS blob
        esp_err_t err = nvs_set_blob(nvsHandle, NVS_KEY_PEERS, peers, sizeof(peers));
        if (err != ESP_OK)
        {
            LOG_E(TAG, "Failed to write peers to NVS: %s", esp_err_to_name(err));
            return;
        }

        err = nvs_set_u32(nvsHandle, NVS_KEY_COUNT, peerCount);
        if (err != ESP_OK)
        {
            LOG_E(TAG, "Failed to write peer count to NVS: %s", esp_err_to_name(err));
            return;
        }

        err = nvs_commit(nvsHandle);
        if (err != ESP_OK)
        {
            LOG_E(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
            return;
        }

        LOG_D(TAG, "Saved %u peers to NVS", peerCount);

#elif defined(ARDUINO_ARCH_NRF52)
        // Save to LittleFS
        File file = InternalFS.open(DB_FILE, FILE_O_WRITE);
        if (!file)
        {
            LOG_E(TAG, "Failed to open peer DB file for writing");
            return;
        }

        // Write count (4 bytes)
        uint32_t count = peerCount;
        if (file.write((uint8_t *)&count, sizeof(count)) != sizeof(count))
        {
            LOG_E(TAG, "Failed to write peer count");
            file.close();
            return;
        }

        // Write peer array
        size_t toWrite = peerCount * sizeof(PeerNode);
        if (file.write((uint8_t *)peers, toWrite) != toWrite)
        {
            LOG_E(TAG, "Failed to write peer data");
            file.close();
            return;
        }

        file.close();
        LOG_D(TAG, "Saved %u peers to LittleFS", peerCount);
#endif
    }
}
