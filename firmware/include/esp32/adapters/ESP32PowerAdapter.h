#ifndef ESP32_POWER_ADAPTER_H
#define ESP32_POWER_ADAPTER_H

#include "ports/IPowerPort.h"
#include "esp32/PowerManager.h"

/**
 * @brief ESP32 Power Management Adapter
 *
 * Wraps ESP32 PowerManager (static functions, ADC-based)
 */
class ESP32PowerAdapter : public IPowerPort
{
public:
    bool begin() override
    {
        PowerManager::configurePowerManagement();
        return true;
    }

    uint8_t readBatteryLevel() override
    {
        return PowerManager::readBatteryLevel();
    }
};

#endif // ESP32_POWER_ADAPTER_H
