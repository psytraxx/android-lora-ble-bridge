#include "common/NodeDB.h"
#include "common/Logging.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

// Platform-specific includes
#if defined(ARDUINO_ARCH_ESP32)
#include <esp32/PlatformTraits.h>
#elif defined(ARDUINO_ARCH_NRF52)
#include <nrf52/PlatformTraits.h>
#endif

static const char *TAG = "NodeDB";

namespace NodeDB
{
    // Static storage
    static uint32_t ownNodeNum = 0;
    static char ownShortName[5] = {0};
    static char ownLongName[40] = {0};
    static uint32_t nextPacketId = 1;

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

        LOG_I(TAG, "Short name: %s", ownShortName);
        LOG_I(TAG, "Long name: %s", ownLongName);

        // TODO: Load persisted names from NVS in future

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
        // TODO: Persist to NVS
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
        // TODO: Persist to NVS
        return true;
    }

    void recordSeenNode(uint32_t nodeNum, int rssi, float snr)
    {
        // Minimal tracking for Phase 1 — just log
        LOG_D(TAG, "Seen node: 0x%08X (RSSI=%d dBm, SNR=%.1f dB)", nodeNum, rssi, snr);
        // TODO: Full NodeDB tracking in Phase 3
    }
}
