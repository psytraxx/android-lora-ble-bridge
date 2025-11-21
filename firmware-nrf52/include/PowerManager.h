#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>

/**
 * @brief Power management for nRF52
 *
 * Handles battery monitoring and low-power modes.
 * Simplified version for initial implementation.
 */
class PowerManager
{
public:
    PowerManager();

    /// Initialize power management
    bool begin();

    /// Read battery voltage (V)
    float readBatteryVoltage();

    /// Read battery level (0-100%)
    uint8_t readBatteryLevel();

    /// Enter low-power mode (not implemented yet)
    void enterLowPowerMode();

private:
    uint8_t lastBatteryLevel;
};

#endif // POWER_MANAGER_H
