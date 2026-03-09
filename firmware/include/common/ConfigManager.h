#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <cstdint>
#include "meshtastic/mesh.pb.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/module_config.pb.h"

// Forward declaration at global scope so ConfigManager::applyLoRaConfig can reference it
class LoRaManager;

/**
 * @file ConfigManager.h
 * @brief Centralized device configuration for Meshtastic BLE config download
 *
 * Generates FromRadio config messages for the want_config_id flow.
 * Phase 2: Hard-coded values. Phase 4: NVS-persisted config.
 */

namespace ConfigManager
{
    /**
     * @brief Initialize ConfigManager (call after NodeDB::init())
     */
    void init();

    /**
     * @brief Populate MyNodeInfo for config download
     * @param info Output MyNodeInfo structure
     * @return true on success
     */
    bool getMyNodeInfo(meshtastic_MyNodeInfo *info);

    /**
     * @brief Populate DeviceMetadata for config download
     * @param metadata Output DeviceMetadata structure
     * @return true on success
     */
    bool getDeviceMetadata(meshtastic_DeviceMetadata *metadata);

    /**
     * @brief Populate Config for config download
     * @param config Output Config structure
     * @param which Config variant tag (meshtastic_Config_device_tag, meshtastic_Config_lora_tag, etc.)
     * @return true on success
     */
    bool getConfig(meshtastic_Config *config, pb_size_t which);

    /**
     * @brief Populate Channel for config download
     * @param channel Output Channel structure
     * @param index Channel index (0 = primary)
     * @return true on success
     */
    bool getChannel(meshtastic_Channel *channel, uint8_t index);

    /**
     * @brief Populate ModuleConfig for config download
     * @param config Output ModuleConfig structure
     * @param which ModuleConfig variant tag (meshtastic_ModuleConfig_mqtt_tag, etc.)
     * @return true on success
     */
    bool getModuleConfig(meshtastic_ModuleConfig *config, pb_size_t which);

    /**
     * @brief Get own NodeInfo for config download
     * @param nodeInfo Output NodeInfo structure
     * @return true on success
     */
    bool getOwnNodeInfo(meshtastic_NodeInfo *nodeInfo);

    /**
     * @brief Persist a Config variant to NVS/LittleFS (call after set_config)
     */
    void persistConfig(const meshtastic_Config *config, pb_size_t which);

    /**
     * @brief Persist a ModuleConfig variant to NVS/LittleFS (call after set_module_config)
     */
    void persistModuleConfig(const meshtastic_ModuleConfig *config, pb_size_t which);

    /**
     * @brief Get the active LoRa config (persisted values or compile-time defaults)
     */
    const meshtastic_Config_LoRaConfig &getLoRaConfig();

    /**
     * @brief Apply persisted LoRa params to a live LoRaManager (call after begin())
     */
    void applyLoRaConfig(LoRaManager *lora);
}

#endif // CONFIG_MANAGER_H
