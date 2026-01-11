#ifndef NRF52_PLATFORM_TRAITS_H
#define NRF52_PLATFORM_TRAITS_H

#include "nrf52/BLEManager.h"
#include "nrf52/MessageBuffer.h"
#include "nrf52/PowerManager.h"
#include "common/FirmwareConfig.h"
#include "common/Logging.h"
#include <nrf.h>

static const char *PLATFORM_TAG = "nRF52";

/**
 * @brief nRF52 Platform Traits
 *
 * Defines platform-specific types, constants, and behaviors for nRF52.
 */
struct NRF52PlatformTraits
{
    // ========================================================================
    // Type Definitions
    // ========================================================================

    using BLEManager = ::BLEManager;
    using StorageManager = ::MessageBuffer;
    using PowerManager = ::PowerManager;

    // ========================================================================
    // Platform-Specific Initialization
    // ========================================================================

    static void initializeWatchdog()
    {
        // Configure WDT timeout: 32768 Hz LFCLK, timeout = (CRV+1)/32768 seconds
        // For 10 seconds: CRV = (10 * 32768) - 1 = 327679
        uint32_t timeout_ticks = (WatchdogConstants::TIMEOUT_SECONDS * 32768) - 1;
        NRF_WDT->CRV = timeout_ticks;

        // Enable reload register 0
        NRF_WDT->RREN = WDT_RREN_RR0_Enabled << WDT_RREN_RR0_Pos;

        // Configure to run during sleep and pause on CPU halt
        NRF_WDT->CONFIG = (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos) |
                          (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos);

        // Start the watchdog
        NRF_WDT->TASKS_START = 1;

        LOG_I(PLATFORM_TAG, "Watchdog enabled: %d ms", WatchdogConstants::TIMEOUT_SECONDS * 1000);
    }

    static void resetWatchdog()
    {
        // Reload watchdog by writing magic value to RR[0]
        NRF_WDT->RR[0] = WDT_RR_RR_Reload;
    }

    static void initializePower()
    {
        PowerManager::printWakeupReason();

        PowerManager::configurePowerManagement();
    }

    static uint8_t readBatteryLevel()
    {
        return PowerManager::readBatteryLevel();
    }

    static void enterDeepSleep()
    {
        PowerManager::enterDeepSleep();
    }
};
#endif // NRF52_PLATFORM_TRAITS_H
