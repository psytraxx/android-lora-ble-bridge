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
    /// Initialize power management
    static bool configurePowerManagement();

    /// Read battery voltage in millivolts with filtering and averaging
    static uint16_t readBatteryVoltage();

    /// Read battery level (0-100%) using OCV lookup table
    static uint8_t readBatteryLevel();

    /// Enter low-power mode (System OFF)
    static void enterLowPowerMode();

private:
    /// Enable battery ADC (set VBAT_ENABLE low)
    static void battery_adcEnable();

    /// Disable battery ADC to save power (set VBAT_ENABLE high)
    static void battery_adcDisable();

    /// Get battery percentage from voltage using OCV lookup table
    static uint8_t voltageToPercentage(uint16_t voltagePerCellMv);
};

#endif // POWER_MANAGER_H
