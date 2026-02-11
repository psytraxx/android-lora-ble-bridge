#include "common/ConfigManager.h"
#include "common/NodeDB.h"
#include "common/MeshCrypto.h"
#include "common/Logging.h"
#include "common/FirmwareConfig.h"
#include <cstring>

static const char *TAG = "ConfigMgr";

namespace ConfigManager
{
    static bool initialized = false;

    void init()
    {
        initialized = true;
        LOG_I(TAG, "ConfigManager initialized");
    }

    bool getMyNodeInfo(meshtastic_MyNodeInfo *info)
    {
        if (!info) return false;

        memset(info, 0, sizeof(meshtastic_MyNodeInfo));
        info->my_node_num = NodeDB::getOwnNodeNum();
        info->reboot_count = NodeDB::getRebootCount();
        info->min_app_version = 30200; // Minimum compatible app version
        info->nodedb_count = NodeDB::getNodeDBCount();
        info->firmware_edition = meshtastic_FirmwareEdition_DIY_EDITION;

        // Device ID (MAC-based, 16 bytes)
        // Use node number repeated to fill
        uint32_t nodeNum = NodeDB::getOwnNodeNum();
        memcpy(info->device_id.bytes, &nodeNum, 4);
        info->device_id.size = 4;

        // PIO environment name
        strncpy(info->pio_env, "meshtastenstein", sizeof(info->pio_env) - 1);

        LOG_D(TAG, "MyNodeInfo: node=%08X, reboot=%u, nodedb=%u",
              info->my_node_num, info->reboot_count, info->nodedb_count);
        return true;
    }

    bool getDeviceMetadata(meshtastic_DeviceMetadata *metadata)
    {
        if (!metadata) return false;

        memset(metadata, 0, sizeof(meshtastic_DeviceMetadata));
        strncpy(metadata->firmware_version, "2.5.0.meshtastenstein",
                sizeof(metadata->firmware_version) - 1);
        metadata->device_state_version = 23;
        metadata->canShutdown = false;
        metadata->hasWifi = false;
        metadata->hasBluetooth = true;
        metadata->hasEthernet = false;
        metadata->role = meshtastic_Config_DeviceConfig_Role_CLIENT;
        metadata->position_flags = 0;
        metadata->hw_model = NodeDB::getOwnHardwareModel();
        metadata->hasRemoteHardware = false;
        metadata->hasPKC = false;

        LOG_D(TAG, "DeviceMetadata: fw=%s, hw=%d", metadata->firmware_version, metadata->hw_model);
        return true;
    }

    bool getConfig(meshtastic_Config *config, pb_size_t which)
    {
        if (!config) return false;

        memset(config, 0, sizeof(meshtastic_Config));
        config->which_payload_variant = which;

        switch (which)
        {
        case meshtastic_Config_device_tag:
        {
            auto &dev = config->payload_variant.device;
            dev.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
            dev.serial_enabled = true;
            dev.node_info_broadcast_secs = 3600;
            dev.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
            LOG_D(TAG, "Config.Device: role=CLIENT");
            return true;
        }

        case meshtastic_Config_lora_tag:
        {
            auto &lora = config->payload_variant.lora;
            lora.use_preset = false;
            lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
            lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
            lora.hop_limit = 3;
            lora.tx_enabled = true;
            lora.tx_power = static_cast<int8_t>(LoRaConstants::TX_POWER);
            lora.bandwidth = static_cast<uint16_t>(LoRaConstants::BANDWIDTH);
            lora.spread_factor = LoRaConstants::SPREADING_FACTOR;
            lora.coding_rate = LoRaConstants::CODING_RATE;
            lora.frequency_offset = 0.0f;
            lora.sx126x_rx_boosted_gain = false;
            lora.override_duty_cycle = false;
            LOG_D(TAG, "Config.LoRa: region=US, bw=%u, sf=%u, cr=%u",
                  lora.bandwidth, lora.spread_factor, lora.coding_rate);
            return true;
        }

        case meshtastic_Config_position_tag:
        {
            // Minimal position config
            LOG_D(TAG, "Config.Position: defaults");
            return true;
        }

        case meshtastic_Config_power_tag:
        {
            // Minimal power config
            LOG_D(TAG, "Config.Power: defaults");
            return true;
        }

        case meshtastic_Config_network_tag:
        {
            // No network config
            LOG_D(TAG, "Config.Network: defaults");
            return true;
        }

        case meshtastic_Config_display_tag:
        {
            // No display config
            LOG_D(TAG, "Config.Display: defaults");
            return true;
        }

        case meshtastic_Config_bluetooth_tag:
        {
            auto &bt = config->payload_variant.bluetooth;
            bt.enabled = true;
            bt.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
            bt.fixed_pin = 123456;
            LOG_D(TAG, "Config.Bluetooth: enabled");
            return true;
        }

        default:
            LOG_W(TAG, "Unknown config variant: %d", which);
            return false;
        }
    }

    bool getChannel(meshtastic_Channel *channel, uint8_t index)
    {
        if (!channel) return false;

        memset(channel, 0, sizeof(meshtastic_Channel));

        if (index == 0)
        {
            // Primary channel with default PSK
            channel->index = 0;
            channel->role = meshtastic_Channel_Role_PRIMARY;
            channel->has_settings = true;

            auto &settings = channel->settings;
            settings.name[0] = '\0'; // Empty name = default channel
            settings.psk.bytes[0] = 0x01; // Default Meshtastic PSK
            settings.psk.size = 1;
            settings.uplink_enabled = false;
            settings.downlink_enabled = false;

            LOG_D(TAG, "Channel[0]: PRIMARY, default PSK");
            return true;
        }

        // No other channels configured
        return false;
    }

    bool getOwnNodeInfo(meshtastic_NodeInfo *nodeInfo)
    {
        if (!nodeInfo) return false;

        memset(nodeInfo, 0, sizeof(meshtastic_NodeInfo));
        nodeInfo->num = NodeDB::getOwnNodeNum();
        nodeInfo->has_user = true;

        auto &user = nodeInfo->user;
        // User ID: "!XXXXXXXX" format
        snprintf(user.id, sizeof(user.id), "!%08lx", (unsigned long)NodeDB::getOwnNodeNum());
        NodeDB::getOwnLongName(user.long_name, sizeof(user.long_name));
        NodeDB::getOwnShortName(user.short_name, sizeof(user.short_name));
        user.hw_model = NodeDB::getOwnHardwareModel();
        user.role = meshtastic_Config_DeviceConfig_Role_CLIENT;

        nodeInfo->last_heard = 0; // Will be set by actual time if available
        nodeInfo->snr = 0.0f;
        nodeInfo->channel = 0;

        LOG_D(TAG, "OwnNodeInfo: num=%08X, name=%s", nodeInfo->num, user.long_name);
        return true;
    }
}
