#include "common/ConfigManager.h"
#include "common/MeshProtocol.h"
#include "common/NodeDB.h"
#include "common/MeshCrypto.h"
#include "common/Logging.h"
#include "common/FirmwareConfig.h"
#include "common/LoRaManager.h"
#include <cstring>

// Platform-specific storage includes
#if defined(ARDUINO_ARCH_ESP32)
#include <nvs_flash.h>
#include <nvs.h>
#elif defined(ARDUINO_ARCH_NRF52)
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;
#endif

// ============================================================================
// In-memory config state (loaded from storage on init, overwritten by set_config)
// ============================================================================
static meshtastic_Config_DeviceConfig      s_deviceCfg;
static meshtastic_Config_LoRaConfig        s_loraCfg;
static meshtastic_Config_BluetoothConfig   s_btCfg;
static meshtastic_Config_PowerConfig       s_powerCfg;
static meshtastic_ModuleConfig_TelemetryConfig s_telemCfg;

// Bitmask of which variants have been loaded from storage (1 << which_payload_variant)
static uint32_t s_persistedConfigMask  = 0;
static uint32_t s_persistedModuleMask  = 0;

#if defined(ARDUINO_ARCH_ESP32)
static const char *CFG_NVS_NAMESPACE = "config_db";
static nvs_handle_t s_cfgNvsHandle = 0;
#elif defined(ARDUINO_ARCH_NRF52)
static const char *CFG_DIR = "/config";
#endif

static void saveConfigBlob(const char *key, const void *ptr, size_t size)
{
#if defined(ARDUINO_ARCH_ESP32)
    if (s_cfgNvsHandle == 0) return;
    esp_err_t err = nvs_set_blob(s_cfgNvsHandle, key, ptr, size);
    if (err == ESP_OK)
    {
        nvs_commit(s_cfgNvsHandle);
    }
#elif defined(ARDUINO_ARCH_NRF52)
    // Build path: /config/<key>.bin
    char path[48];
    snprintf(path, sizeof(path), "%s/%s.bin", CFG_DIR, key);
    File file = InternalFS.open(path, FILE_O_WRITE);
    if (file)
    {
        file.write((const uint8_t *)ptr, size);
        file.close();
    }
#else
    (void)key; (void)ptr; (void)size;
#endif
}

static void loadConfigBlobIfMatch(const char *key, void *ptr, size_t expectedSize, uint32_t *mask, uint32_t bit)
{
#if defined(ARDUINO_ARCH_ESP32)
    if (s_cfgNvsHandle == 0) return;
    size_t sz = expectedSize;
    esp_err_t err = nvs_get_blob(s_cfgNvsHandle, key, ptr, &sz);
    if (err == ESP_OK && sz == expectedSize)
    {
        *mask |= bit;
    }
#elif defined(ARDUINO_ARCH_NRF52)
    char path[48];
    snprintf(path, sizeof(path), "%s/%s.bin", CFG_DIR, key);
    if (!InternalFS.exists(path)) return;
    File file = InternalFS.open(path, FILE_O_READ);
    if (!file) return;
    if ((size_t)file.read((uint8_t *)ptr, expectedSize) == expectedSize)
    {
        *mask |= bit;
    }
    file.close();
#else
    (void)key; (void)ptr; (void)expectedSize; (void)mask; (void)bit;
#endif
}

static const char *TAG = "ConfigMgr";

namespace ConfigManager
{
    static bool initialized = false;

    void init()
    {
        // Initialize defaults for in-memory config structs
        memset(&s_deviceCfg, 0, sizeof(s_deviceCfg));
        memset(&s_loraCfg,   0, sizeof(s_loraCfg));
        memset(&s_btCfg,     0, sizeof(s_btCfg));
        memset(&s_powerCfg,  0, sizeof(s_powerCfg));
        memset(&s_telemCfg,  0, sizeof(s_telemCfg));

        // Open storage
#if defined(ARDUINO_ARCH_ESP32)
        // NVS should already be initialized by NodeDB
        esp_err_t err = nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &s_cfgNvsHandle);
        if (err != ESP_OK)
        {
            LOG_W(TAG, "Failed to open config NVS namespace: %s", esp_err_to_name(err));
        }
#elif defined(ARDUINO_ARCH_NRF52)
        // InternalFS already initialized by NodeDB
        if (!InternalFS.exists(CFG_DIR))
        {
            InternalFS.mkdir(CFG_DIR);
        }
#endif

        // Load persisted variants
        loadConfigBlobIfMatch("cfg_device", &s_deviceCfg, sizeof(s_deviceCfg),
                              &s_persistedConfigMask, (1u << meshtastic_Config_device_tag));
        loadConfigBlobIfMatch("cfg_lora", &s_loraCfg, sizeof(s_loraCfg),
                              &s_persistedConfigMask, (1u << meshtastic_Config_lora_tag));
        loadConfigBlobIfMatch("cfg_bt", &s_btCfg, sizeof(s_btCfg),
                              &s_persistedConfigMask, (1u << meshtastic_Config_bluetooth_tag));
        loadConfigBlobIfMatch("cfg_power", &s_powerCfg, sizeof(s_powerCfg),
                              &s_persistedConfigMask, (1u << meshtastic_Config_power_tag));
        loadConfigBlobIfMatch("mod_telem", &s_telemCfg, sizeof(s_telemCfg),
                              &s_persistedModuleMask, (1u << meshtastic_ModuleConfig_telemetry_tag));

        LOG_I(TAG, "ConfigManager initialized (configMask=0x%02lx, moduleMask=0x%02lx)",
              (unsigned long)s_persistedConfigMask, (unsigned long)s_persistedModuleMask);
        initialized = true;
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
            if (s_persistedConfigMask & (1u << meshtastic_Config_device_tag))
            {
                config->payload_variant.device = s_deviceCfg;
                LOG_D(TAG, "Config.Device: from NVS (role=%d)", s_deviceCfg.role);
            }
            else
            {
                auto &dev = config->payload_variant.device;
                dev.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
                dev.serial_enabled = true;
                dev.node_info_broadcast_secs = 3600;
                dev.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
                LOG_D(TAG, "Config.Device: role=CLIENT (default)");
            }
            return true;
        }

        case meshtastic_Config_lora_tag:
        {
            if (s_persistedConfigMask & (1u << meshtastic_Config_lora_tag))
            {
                config->payload_variant.lora = s_loraCfg;
                LOG_D(TAG, "Config.LoRa: from NVS (sf=%u, bw=%u, cr=%u)",
                      s_loraCfg.spread_factor, s_loraCfg.bandwidth, s_loraCfg.coding_rate);
            }
            else
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
                LOG_D(TAG, "Config.LoRa: region=US, bw=%u, sf=%u, cr=%u (default)",
                      lora.bandwidth, lora.spread_factor, lora.coding_rate);
            }
            return true;
        }

        case meshtastic_Config_position_tag:
        {
            config->payload_variant.position.fixed_position = NodeDB::hasFixedPosition();
            LOG_D(TAG, "Config.Position: fixed=%d", NodeDB::hasFixedPosition());
            return true;
        }

        case meshtastic_Config_security_tag:
        {
            // No PKC — return empty security config
            LOG_D(TAG, "Config.Security: defaults (no PKC)");
            return true;
        }

        case meshtastic_Config_sessionkey_tag:
        {
            // No session key management — return empty
            LOG_D(TAG, "Config.Sessionkey: defaults");
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
            if (s_persistedConfigMask & (1u << meshtastic_Config_bluetooth_tag))
            {
                config->payload_variant.bluetooth = s_btCfg;
                LOG_D(TAG, "Config.Bluetooth: from NVS");
            }
            else
            {
                auto &bt = config->payload_variant.bluetooth;
                bt.enabled = true;
                bt.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
                bt.fixed_pin = 123456;
                LOG_D(TAG, "Config.Bluetooth: enabled (default)");
            }
            return true;
        }

        default:
            LOG_W(TAG, "Unknown config variant: %d", which);
            return false;
        }
    }

    bool getModuleConfig(meshtastic_ModuleConfig *config, pb_size_t which)
    {
        if (!config) return false;

        memset(config, 0, sizeof(meshtastic_ModuleConfig));
        config->which_payload_variant = which;

        switch (which)
        {
        case meshtastic_ModuleConfig_mqtt_tag:
            LOG_D(TAG, "ModuleConfig.MQTT: disabled");
            return true;
        case meshtastic_ModuleConfig_serial_tag:
            LOG_D(TAG, "ModuleConfig.Serial: disabled");
            return true;
        case meshtastic_ModuleConfig_external_notification_tag:
            LOG_D(TAG, "ModuleConfig.ExtNotif: disabled");
            return true;
        case meshtastic_ModuleConfig_store_forward_tag:
            LOG_D(TAG, "ModuleConfig.StoreForward: disabled");
            return true;
        case meshtastic_ModuleConfig_range_test_tag:
            LOG_D(TAG, "ModuleConfig.RangeTest: disabled");
            return true;
        case meshtastic_ModuleConfig_telemetry_tag:
            if (s_persistedModuleMask & (1u << meshtastic_ModuleConfig_telemetry_tag))
            {
                config->payload_variant.telemetry = s_telemCfg;
                LOG_D(TAG, "ModuleConfig.Telemetry: from NVS (interval=%lus)",
                      (unsigned long)s_telemCfg.device_update_interval);
            }
            else
            {
                config->payload_variant.telemetry.device_update_interval = 3600;
                LOG_D(TAG, "ModuleConfig.Telemetry: interval=3600s (default)");
            }
            return true;
        case meshtastic_ModuleConfig_canned_message_tag:
            LOG_D(TAG, "ModuleConfig.CannedMsg: disabled");
            return true;
        case meshtastic_ModuleConfig_audio_tag:
            LOG_D(TAG, "ModuleConfig.Audio: disabled");
            return true;
        case meshtastic_ModuleConfig_remote_hardware_tag:
            LOG_D(TAG, "ModuleConfig.RemoteHW: disabled");
            return true;
        case meshtastic_ModuleConfig_neighbor_info_tag:
            LOG_D(TAG, "ModuleConfig.NeighborInfo: disabled");
            return true;
        case meshtastic_ModuleConfig_ambient_lighting_tag:
            LOG_D(TAG, "ModuleConfig.AmbientLight: disabled");
            return true;
        case meshtastic_ModuleConfig_detection_sensor_tag:
            LOG_D(TAG, "ModuleConfig.DetectionSensor: disabled");
            return true;
        case meshtastic_ModuleConfig_paxcounter_tag:
            LOG_D(TAG, "ModuleConfig.Paxcounter: disabled");
            return true;
        case meshtastic_ModuleConfig_statusmessage_tag:
            LOG_D(TAG, "ModuleConfig.StatusMessage: disabled");
            return true;
        default:
            LOG_W(TAG, "Unknown module config variant: %d", which);
            return false;
        }
    }

    bool getChannel(meshtastic_Channel *channel, uint8_t index)
    {
        if (!channel) return false;
        if (index >= MeshProtocol::MAX_CHANNELS) return false;

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

        // Secondary channels: only return if configured (channelCount covers it)
        if (index >= MeshProtocol::getChannelCount())
        {
            // Return DISABLED channel so app knows it's empty
            channel->index = index;
            channel->role = meshtastic_Channel_Role_DISABLED;
            channel->has_settings = false;
            return true;
        }

        // Configured secondary channel
        channel->index = index;
        channel->role = meshtastic_Channel_Role_SECONDARY;
        channel->has_settings = true;
        // Settings are stored in MeshProtocol — but we don't expose them directly,
        // so we reconstruct from what we know
        // The app set the channel via admin, so the settings are already in MeshProtocol
        LOG_D(TAG, "Channel[%u]: SECONDARY", index);
        return true;
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

        nodeInfo->last_heard = NodeDB::getCurrentTime();
        nodeInfo->snr = 0.0f;
        nodeInfo->channel = 0;

        // Include fixed position if set
        if (NodeDB::hasFixedPosition())
        {
            nodeInfo->has_position = true;
            nodeInfo->position = NodeDB::getFixedPosition();
        }

        LOG_D(TAG, "OwnNodeInfo: num=%08X, name=%s", nodeInfo->num, user.long_name);
        return true;
    }

    void persistConfig(const meshtastic_Config *config, pb_size_t which)
    {
        if (!config) return;
        switch (which)
        {
        case meshtastic_Config_device_tag:
            s_deviceCfg = config->payload_variant.device;
            s_persistedConfigMask |= (1u << meshtastic_Config_device_tag);
            saveConfigBlob("cfg_device", &s_deviceCfg, sizeof(s_deviceCfg));
            LOG_I(TAG, "Persisted Config.Device");
            break;
        case meshtastic_Config_lora_tag:
            s_loraCfg = config->payload_variant.lora;
            s_persistedConfigMask |= (1u << meshtastic_Config_lora_tag);
            saveConfigBlob("cfg_lora", &s_loraCfg, sizeof(s_loraCfg));
            LOG_I(TAG, "Persisted Config.LoRa (sf=%u, bw=%u, cr=%u)",
                  s_loraCfg.spread_factor, s_loraCfg.bandwidth, s_loraCfg.coding_rate);
            break;
        case meshtastic_Config_bluetooth_tag:
            s_btCfg = config->payload_variant.bluetooth;
            s_persistedConfigMask |= (1u << meshtastic_Config_bluetooth_tag);
            saveConfigBlob("cfg_bt", &s_btCfg, sizeof(s_btCfg));
            LOG_I(TAG, "Persisted Config.Bluetooth");
            break;
        case meshtastic_Config_power_tag:
            s_powerCfg = config->payload_variant.power;
            s_persistedConfigMask |= (1u << meshtastic_Config_power_tag);
            saveConfigBlob("cfg_power", &s_powerCfg, sizeof(s_powerCfg));
            LOG_I(TAG, "Persisted Config.Power");
            break;
        default:
            LOG_D(TAG, "persistConfig: variant %d not stored", which);
            break;
        }
    }

    void persistModuleConfig(const meshtastic_ModuleConfig *config, pb_size_t which)
    {
        if (!config) return;
        switch (which)
        {
        case meshtastic_ModuleConfig_telemetry_tag:
            s_telemCfg = config->payload_variant.telemetry;
            s_persistedModuleMask |= (1u << meshtastic_ModuleConfig_telemetry_tag);
            saveConfigBlob("mod_telem", &s_telemCfg, sizeof(s_telemCfg));
            LOG_I(TAG, "Persisted ModuleConfig.Telemetry (interval=%lus)",
                  (unsigned long)s_telemCfg.device_update_interval);
            break;
        default:
            LOG_D(TAG, "persistModuleConfig: variant %d not stored", which);
            break;
        }
    }

    const meshtastic_Config_LoRaConfig &getLoRaConfig()
    {
        if (s_persistedConfigMask & (1u << meshtastic_Config_lora_tag))
        {
            return s_loraCfg;
        }
        // Return defaults via static initialised-on-demand struct
        static meshtastic_Config_LoRaConfig defaults;
        static bool defaultsInit = false;
        if (!defaultsInit)
        {
            memset(&defaults, 0, sizeof(defaults));
            defaults.use_preset = false;
            defaults.region = meshtastic_Config_LoRaConfig_RegionCode_US;
            defaults.hop_limit = 3;
            defaults.tx_enabled = true;
            defaults.tx_power = static_cast<int8_t>(LoRaConstants::TX_POWER);
            defaults.bandwidth = static_cast<uint16_t>(LoRaConstants::BANDWIDTH);
            defaults.spread_factor = LoRaConstants::SPREADING_FACTOR;
            defaults.coding_rate = LoRaConstants::CODING_RATE;
            defaultsInit = true;
        }
        return defaults;
    }

    void applyLoRaConfig(LoRaManager *lora)
    {
        if (!lora) return;
        if (!(s_persistedConfigMask & (1u << meshtastic_Config_lora_tag))) return;

        const auto &c = s_loraCfg;
        LOG_I(TAG, "Applying persisted LoRa config: sf=%u, bw=%u, cr=%u, pwr=%d",
              c.spread_factor, c.bandwidth, c.coding_rate, c.tx_power);

        if (c.spread_factor != 0)
            lora->setSpreadingFactor(c.spread_factor);
        if (c.bandwidth != 0)
            lora->setBandwidth((float)c.bandwidth);
        if (c.coding_rate != 0)
            lora->setCodingRate(c.coding_rate);
        if (c.tx_power != 0)
            lora->setTxPower(c.tx_power);
        if (c.override_frequency > 0.0f)
            lora->setFrequency(c.override_frequency);
    }
}
