#ifndef NRF52_POWER_ADAPTER_H
#define NRF52_POWER_ADAPTER_H

#include "ports/IPowerPort.h"
#include "nrf52/PowerManager.h"

/**
 * @brief nRF52 Power Management Adapter
 *
 * Wraps nRF52 PowerManager (instance-based, internal ADC)
 */
class NRF52PowerAdapter : public IPowerPort
{
public:
    NRF52PowerAdapter() : powerManager() {}

    bool begin() override
    {
        return powerManager.begin();
    }

    uint8_t readBatteryLevel() override
    {
        return powerManager.readBatteryLevel();
    }

private:
    PowerManager powerManager;
};

#endif // NRF52_POWER_ADAPTER_H
