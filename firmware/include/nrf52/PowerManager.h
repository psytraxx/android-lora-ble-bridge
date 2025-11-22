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

    /// Read battery voltage in millivolts with filtering and averaging
    uint16_t readBatteryVoltage();

    /// Read battery level (0-100%) using OCV lookup table
    uint8_t readBatteryLevel();

    /// Enter low-power mode (System OFF)
    void enterLowPowerMode();

private:
    /// Get battery percentage from voltage using OCV lookup table
    static uint8_t voltageToPercentage(uint16_t voltagePerCellMv);

    uint8_t lastBatteryLevel;
    bool initial_read_done;
    float last_read_value;
    uint32_t last_read_time_ms;
};

#endif // POWER_MANAGER_H
