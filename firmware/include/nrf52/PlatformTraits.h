#ifndef NRF52_PLATFORM_TRAITS_H
#define NRF52_PLATFORM_TRAITS_H

#include "nrf52/BLEManager.h"
#include "nrf52/MessageBuffer.h"
#include "nrf52/PowerManager.h"
#include "common/FirmwareConfig.h"
#include "common/Logging.h"
#include <Adafruit_SleepyDog.h>

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
        int watchdogMS = Watchdog.enable(WatchdogConstants::TIMEOUT_SECONDS * 1000);
        LOG_I(PLATFORM_TAG, "Watchdog enabled: %d ms", watchdogMS);
    }

    static void resetWatchdog()
    {
        Watchdog.reset();
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

    static bool isLoraWakeUp()
    {
        return PowerManager::isLoraWakeUp();
    }

    static void enterDeepSleep()
    {
        PowerManager::enterDeepSleep();
    }
};
#endif // NRF52_PLATFORM_TRAITS_H
