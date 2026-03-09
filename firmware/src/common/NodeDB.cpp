#include "common/NodeDB.h"
#include "common/PeerNodeDB.h"
#include "common/Logging.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

// Platform-specific includes
#if defined(ARDUINO_ARCH_ESP32)
#include <esp32/PlatformTraits.h>
#include <nvs_flash.h>
#include <nvs.h>
#elif defined(ARDUINO_ARCH_NRF52)
#include <nrf52/PlatformTraits.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;
#endif

static const char *TAG = "NodeDB";

#if defined(ARDUINO_ARCH_ESP32)
static const char *NVS_NAMESPACE = "node_db";
static const char *NVS_KEY_SHORT = "short_name";
static const char *NVS_KEY_LONG = "long_name";
static const char *NVS_KEY_REBOOT = "reboot_cnt";
static const char *NVS_KEY_PKT_ID = "pkt_id";
static const char *NVS_KEY_BOOT_EPOCH = "boot_epoch";
static const char *NVS_KEY_FIXPOS_SET = "fix_pos_set";
static const char *NVS_KEY_FIXPOS = "fix_pos";
static nvs_handle_t nvsHandle = 0;
#elif defined(ARDUINO_ARCH_NRF52)
static const char *DB_DIR = "/nodedb";
static const char *DB_FILE = "/nodedb/names.bin";
static const char *TIME_FILE = "/nodedb/time.bin";
static const char *FIXPOS_FILE = "/nodedb/fixpos.bin";
#endif

// Unix time at boot (0 = unknown). Set by setCurrentTime().
static uint32_t s_bootEpoch = 0;

// Fixed position (from set_fixed_position admin command)
static bool s_hasFixedPosition = false;
static meshtastic_Position s_fixedPosition;

namespace NodeDB
{
    // Static storage
    static uint32_t ownNodeNum = 0;
    static char ownShortName[5] = {0};
    static char ownLongName[40] = {0};
    static uint32_t nextPacketId = 1;
    static uint32_t rebootCount = 0;

    // Forward declarations
    static void loadPersistedData();
    static void persistShortName();
    static void persistLongName();
    static void persistRebootCount();
    static void persistPacketId();

    bool init()
    {
        LOG_I(TAG, "Initializing NodeDB");

        // Generate node number from MAC address
        // Use platform-specific MAC suffix
#if defined(ARDUINO_ARCH_ESP32)
        String macSuffix = ESP32PlatformTraits::getMacSuffix();
#elif defined(ARDUINO_ARCH_NRF52)
        String macSuffix = NRF52PlatformTraits::getMacSuffix();
#endif

        // Parse hex string to node number (last 4 bytes of MAC)
        uint32_t mac32 = 0;
        if (macSuffix.length() >= 4)
        {
            // Parse 4-character hex string (e.g., "A1B2")
            mac32 = (uint32_t)strtoul(macSuffix.c_str(), nullptr, 16);
        }
        else
        {
            LOG_W(TAG, "Invalid MAC suffix, using fallback");
            mac32 = 0x12345678; // Fallback
        }

        ownNodeNum = mac32;
        LOG_I(TAG, "Node number: 0x%08X (%u)", ownNodeNum, ownNodeNum);

        // Generate default short name: "M" + last 4 hex digits
        snprintf(ownShortName, sizeof(ownShortName), "M%s", macSuffix.c_str());

        // Generate default long name: "Meshtastic " + last 4 hex
        snprintf(ownLongName, sizeof(ownLongName), "Meshtastic %s", macSuffix.c_str());

        // Initialize storage and load persisted data (overrides defaults)
#if defined(ARDUINO_ARCH_ESP32)
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            LOG_W(TAG, "NVS partition needs erasing");
            nvs_flash_erase();
            err = nvs_flash_init();
        }

        if (err == ESP_OK)
        {
            err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
            if (err == ESP_OK)
            {
                loadPersistedData();
            }
            else
            {
                LOG_W(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
            }
        }
        else
        {
            LOG_W(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        }
#elif defined(ARDUINO_ARCH_NRF52)
        InternalFS.begin();
        if (!InternalFS.exists(DB_DIR))
        {
            InternalFS.mkdir(DB_DIR);
        }
        loadPersistedData();
#endif

        LOG_I(TAG, "Short name: %s", ownShortName);
        LOG_I(TAG, "Long name: %s", ownLongName);
        LOG_I(TAG, "Reboot count: %lu", (unsigned long)rebootCount);
        LOG_I(TAG, "Next packet ID: %lu", (unsigned long)nextPacketId);

        return true;
    }

    uint32_t getOwnNodeNum()
    {
        return ownNodeNum;
    }

    void getOwnShortName(char *buffer, size_t bufferSize)
    {
        if (bufferSize < sizeof(ownShortName))
        {
            LOG_W(TAG, "Buffer too small for short name");
            buffer[0] = '\0';
            return;
        }
        strncpy(buffer, ownShortName, bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
    }

    void getOwnLongName(char *buffer, size_t bufferSize)
    {
        if (bufferSize < sizeof(ownLongName))
        {
            LOG_W(TAG, "Buffer too small for long name");
            buffer[0] = '\0';
            return;
        }
        strncpy(buffer, ownLongName, bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
    }

    meshtastic_HardwareModel getOwnHardwareModel()
    {
        // Custom hardware — use PRIVATE_HW enum
        return meshtastic_HardwareModel_PRIVATE_HW;
    }

    uint32_t generatePacketId()
    {
        uint32_t id = nextPacketId++;
        // Wrap around at UINT32_MAX
        if (nextPacketId == 0)
        {
            nextPacketId = 1;
        }
        return id;
    }

    bool setOwnShortName(const char *name)
    {
        if (strlen(name) > 4)
        {
            LOG_E(TAG, "Short name too long: %s", name);
            return false;
        }
        strncpy(ownShortName, name, sizeof(ownShortName) - 1);
        ownShortName[sizeof(ownShortName) - 1] = '\0';
        LOG_I(TAG, "Short name updated: %s", ownShortName);
        persistShortName();
        return true;
    }

    bool setOwnLongName(const char *name)
    {
        if (strlen(name) > 39)
        {
            LOG_E(TAG, "Long name too long: %s", name);
            return false;
        }
        strncpy(ownLongName, name, sizeof(ownLongName) - 1);
        ownLongName[sizeof(ownLongName) - 1] = '\0';
        LOG_I(TAG, "Long name updated: %s", ownLongName);
        persistLongName();
        return true;
    }

    void recordSeenNode(uint32_t nodeNum, int rssi, float snr)
    {
        LOG_D(TAG, "Seen node: 0x%08lX (RSSI=%d, SNR=%.1f)",
              (unsigned long)nodeNum, rssi, snr);
        uint8_t hops = 0; // Direct reception = 0 hops
        PeerNodeDB::updateSignalInfo(nodeNum, rssi, snr, hops);
    }

    uint32_t getRebootCount()
    {
        return rebootCount;
    }

    void incrementRebootCount()
    {
        rebootCount++;
        LOG_I(TAG, "Reboot count: %lu", (unsigned long)rebootCount);
        persistRebootCount();
    }

    uint16_t getNodeDBCount()
    {
        // Own node + peers
        return 1 + PeerNodeDB::getCount();
    }

    void setCurrentTime(uint32_t unixTime)
    {
        s_bootEpoch = unixTime - (millis() / 1000);
        LOG_I(TAG, "Time set: Unix=%lu, bootEpoch=%lu",
              (unsigned long)unixTime, (unsigned long)s_bootEpoch);

        // Persist so approximate time survives reboots
#if defined(ARDUINO_ARCH_ESP32)
        if (nvsHandle != 0)
        {
            nvs_set_u32(nvsHandle, NVS_KEY_BOOT_EPOCH, s_bootEpoch);
            nvs_commit(nvsHandle);
        }
#elif defined(ARDUINO_ARCH_NRF52)
        File tf = InternalFS.open(TIME_FILE, FILE_O_WRITE);
        if (tf)
        {
            tf.write((uint8_t *)&s_bootEpoch, sizeof(s_bootEpoch));
            tf.close();
        }
#endif
    }

    uint32_t getCurrentTime()
    {
        if (s_bootEpoch == 0) return 0;
        return s_bootEpoch + (millis() / 1000);
    }

    // ========================================================================
    // Platform-specific persistence helpers
    // ========================================================================

    static void loadPersistedData()
    {
#if defined(ARDUINO_ARCH_ESP32)
        // Load short name
        size_t len = sizeof(ownShortName);
        esp_err_t err = nvs_get_str(nvsHandle, NVS_KEY_SHORT, ownShortName, &len);
        if (err == ESP_OK)
        {
            LOG_D(TAG, "Loaded persisted short name: %s", ownShortName);
        }

        // Load long name
        len = sizeof(ownLongName);
        err = nvs_get_str(nvsHandle, NVS_KEY_LONG, ownLongName, &len);
        if (err == ESP_OK)
        {
            LOG_D(TAG, "Loaded persisted long name: %s", ownLongName);
        }

        // Load reboot count
        err = nvs_get_u32(nvsHandle, NVS_KEY_REBOOT, &rebootCount);
        if (err == ESP_OK)
        {
            LOG_D(TAG, "Loaded reboot count: %lu", (unsigned long)rebootCount);
        }

        // Load packet ID
        err = nvs_get_u32(nvsHandle, NVS_KEY_PKT_ID, &nextPacketId);
        if (err == ESP_OK)
        {
            LOG_D(TAG, "Loaded packet ID: %lu", (unsigned long)nextPacketId);
        }

        // Load boot epoch (approximate RTC)
        err = nvs_get_u32(nvsHandle, NVS_KEY_BOOT_EPOCH, &s_bootEpoch);
        if (err == ESP_OK)
        {
            LOG_D(TAG, "Loaded boot epoch: %lu", (unsigned long)s_bootEpoch);
        }

        // Load fixed position
        {
            uint8_t fixSet = 0;
            if (nvs_get_u8(nvsHandle, NVS_KEY_FIXPOS_SET, &fixSet) == ESP_OK && fixSet)
            {
                size_t sz = sizeof(s_fixedPosition);
                if (nvs_get_blob(nvsHandle, NVS_KEY_FIXPOS, &s_fixedPosition, &sz) == ESP_OK
                    && sz == sizeof(s_fixedPosition))
                {
                    s_hasFixedPosition = true;
                    LOG_D(TAG, "Loaded fixed position");
                }
            }
        }

#elif defined(ARDUINO_ARCH_NRF52)
        // Load from binary file
        if (!InternalFS.exists(DB_FILE))
        {
            LOG_D(TAG, "No persisted node data (first boot)");
            return;
        }

        File file = InternalFS.open(DB_FILE, FILE_O_READ);
        if (!file)
        {
            LOG_W(TAG, "Failed to open node DB file");
            return;
        }

        // Read short name (5 bytes)
        if (file.read(ownShortName, sizeof(ownShortName)) == sizeof(ownShortName))
        {
            LOG_D(TAG, "Loaded persisted short name: %s", ownShortName);
        }

        // Read long name (40 bytes)
        if (file.read(ownLongName, sizeof(ownLongName)) == sizeof(ownLongName))
        {
            LOG_D(TAG, "Loaded persisted long name: %s", ownLongName);
        }

        // Read reboot count (4 bytes)
        if (file.read(&rebootCount, sizeof(rebootCount)) == sizeof(rebootCount))
        {
            LOG_D(TAG, "Loaded reboot count: %lu", (unsigned long)rebootCount);
        }

        // Read packet ID (4 bytes)
        if (file.read(&nextPacketId, sizeof(nextPacketId)) == sizeof(nextPacketId))
        {
            LOG_D(TAG, "Loaded packet ID: %lu", (unsigned long)nextPacketId);
        }

        file.close();

        // Load boot epoch from separate file
        if (InternalFS.exists(TIME_FILE))
        {
            File tf = InternalFS.open(TIME_FILE, FILE_O_READ);
            if (tf)
            {
                tf.read((uint8_t *)&s_bootEpoch, sizeof(s_bootEpoch));
                tf.close();
                LOG_D(TAG, "Loaded boot epoch: %lu", (unsigned long)s_bootEpoch);
            }
        }

        // Load fixed position from separate file
        if (InternalFS.exists(FIXPOS_FILE))
        {
            File pf = InternalFS.open(FIXPOS_FILE, FILE_O_READ);
            if (pf)
            {
                if ((size_t)pf.read((uint8_t *)&s_fixedPosition, sizeof(s_fixedPosition))
                    == sizeof(s_fixedPosition))
                {
                    s_hasFixedPosition = true;
                    LOG_D(TAG, "Loaded fixed position");
                }
                pf.close();
            }
        }
#endif
    }

    static void persistShortName()
    {
#if defined(ARDUINO_ARCH_ESP32)
        esp_err_t err = nvs_set_str(nvsHandle, NVS_KEY_SHORT, ownShortName);
        if (err == ESP_OK)
        {
            nvs_commit(nvsHandle);
            LOG_D(TAG, "Persisted short name");
        }
        else
        {
            LOG_W(TAG, "Failed to persist short name: %s", esp_err_to_name(err));
        }
#elif defined(ARDUINO_ARCH_NRF52)
        // Re-write entire file (simpler than partial update)
        File file = InternalFS.open(DB_FILE, FILE_O_WRITE);
        if (file)
        {
            file.write(ownShortName, sizeof(ownShortName));
            file.write(ownLongName, sizeof(ownLongName));
            file.write((uint8_t *)&rebootCount, sizeof(rebootCount));
            file.write((uint8_t *)&nextPacketId, sizeof(nextPacketId));
            file.close();
            LOG_D(TAG, "Persisted short name");
        }
#endif
    }

    static void persistLongName()
    {
#if defined(ARDUINO_ARCH_ESP32)
        esp_err_t err = nvs_set_str(nvsHandle, NVS_KEY_LONG, ownLongName);
        if (err == ESP_OK)
        {
            nvs_commit(nvsHandle);
            LOG_D(TAG, "Persisted long name");
        }
        else
        {
            LOG_W(TAG, "Failed to persist long name: %s", esp_err_to_name(err));
        }
#elif defined(ARDUINO_ARCH_NRF52)
        // Re-write entire file
        File file = InternalFS.open(DB_FILE, FILE_O_WRITE);
        if (file)
        {
            file.write(ownShortName, sizeof(ownShortName));
            file.write(ownLongName, sizeof(ownLongName));
            file.write((uint8_t *)&rebootCount, sizeof(rebootCount));
            file.write((uint8_t *)&nextPacketId, sizeof(nextPacketId));
            file.close();
            LOG_D(TAG, "Persisted long name");
        }
#endif
    }

    static void persistRebootCount()
    {
#if defined(ARDUINO_ARCH_ESP32)
        esp_err_t err = nvs_set_u32(nvsHandle, NVS_KEY_REBOOT, rebootCount);
        if (err == ESP_OK)
        {
            nvs_commit(nvsHandle);
            LOG_D(TAG, "Persisted reboot count");
        }
        else
        {
            LOG_W(TAG, "Failed to persist reboot count: %s", esp_err_to_name(err));
        }
#elif defined(ARDUINO_ARCH_NRF52)
        // Re-write entire file
        File file = InternalFS.open(DB_FILE, FILE_O_WRITE);
        if (file)
        {
            file.write(ownShortName, sizeof(ownShortName));
            file.write(ownLongName, sizeof(ownLongName));
            file.write((uint8_t *)&rebootCount, sizeof(rebootCount));
            file.write((uint8_t *)&nextPacketId, sizeof(nextPacketId));
            file.close();
            LOG_D(TAG, "Persisted reboot count");
        }
#endif
    }

    static void persistPacketId()
    {
#if defined(ARDUINO_ARCH_ESP32)
        esp_err_t err = nvs_set_u32(nvsHandle, NVS_KEY_PKT_ID, nextPacketId);
        if (err == ESP_OK)
        {
            nvs_commit(nvsHandle);
            LOG_D(TAG, "Persisted packet ID");
        }
        else
        {
            LOG_W(TAG, "Failed to persist packet ID: %s", esp_err_to_name(err));
        }
#elif defined(ARDUINO_ARCH_NRF52)
        // Re-write entire file
        File file = InternalFS.open(DB_FILE, FILE_O_WRITE);
        if (file)
        {
            file.write(ownShortName, sizeof(ownShortName));
            file.write(ownLongName, sizeof(ownLongName));
            file.write((uint8_t *)&rebootCount, sizeof(rebootCount));
            file.write((uint8_t *)&nextPacketId, sizeof(nextPacketId));
            file.close();
            LOG_D(TAG, "Persisted packet ID");
        }
#endif
    }

    void setFixedPosition(const meshtastic_Position &position)
    {
        s_fixedPosition = position;
        s_hasFixedPosition = true;
        LOG_I(TAG, "Fixed position set: lat=%ld lon=%ld alt=%ld",
              (long)position.latitude_i, (long)position.longitude_i, (long)position.altitude);

#if defined(ARDUINO_ARCH_ESP32)
        if (nvsHandle != 0)
        {
            nvs_set_u8(nvsHandle, NVS_KEY_FIXPOS_SET, 1);
            nvs_set_blob(nvsHandle, NVS_KEY_FIXPOS, &s_fixedPosition, sizeof(s_fixedPosition));
            nvs_commit(nvsHandle);
        }
#elif defined(ARDUINO_ARCH_NRF52)
        File pf = InternalFS.open(FIXPOS_FILE, FILE_O_WRITE);
        if (pf)
        {
            pf.write((uint8_t *)&s_fixedPosition, sizeof(s_fixedPosition));
            pf.close();
        }
#endif
    }

    void clearFixedPosition()
    {
        memset(&s_fixedPosition, 0, sizeof(s_fixedPosition));
        s_hasFixedPosition = false;
        LOG_I(TAG, "Fixed position cleared");

#if defined(ARDUINO_ARCH_ESP32)
        if (nvsHandle != 0)
        {
            nvs_set_u8(nvsHandle, NVS_KEY_FIXPOS_SET, 0);
            nvs_erase_key(nvsHandle, NVS_KEY_FIXPOS);
            nvs_commit(nvsHandle);
        }
#elif defined(ARDUINO_ARCH_NRF52)
        if (InternalFS.exists(FIXPOS_FILE))
        {
            InternalFS.remove(FIXPOS_FILE);
        }
#endif
    }

    bool hasFixedPosition()
    {
        return s_hasFixedPosition;
    }

    const meshtastic_Position &getFixedPosition()
    {
        return s_fixedPosition;
    }
}
