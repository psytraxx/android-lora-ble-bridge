#ifndef ESP32_PLATFORM_TRAITS_H
#define ESP32_PLATFORM_TRAITS_H

#include "esp32/BLEManager.h"
#include "esp32/MessageBuffer.h"
#include "esp32/PowerManager.h"
#include "common/FirmwareConfig.h"
#include "common/Logging.h"
#include "esp_task_wdt.h"

static const char *PLATFORM_TAG = "ESP32";

/**
 * @brief ESP32 Platform Traits
 *
 * Defines platform-specific types, constants, and behaviors for ESP32.
 */
struct ESP32PlatformTraits
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
        esp_task_wdt_config_t wdt_config = {
            .timeout_ms = WatchdogConstants::TIMEOUT_SECONDS * 1000,
            .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
            .trigger_panic = true
        };
        esp_task_wdt_init(&wdt_config);
        esp_task_wdt_add(NULL); // Add current task to watchdog
        LOG_I(PLATFORM_TAG, "Watchdog enabled: %d ms", WatchdogConstants::TIMEOUT_SECONDS * 1000);
    }

    static void resetWatchdog()
    {
        esp_task_wdt_reset();
    }

    static void initializePower()
    {
        // Print wakeup reason if resuming from deep sleep
        PowerManager::printWakeupReason();

        PowerManager::configurePowerManagement();

        // Disable unused radios to save power
        PowerManager::disableWiFi();
        PowerManager::disableBluetoothClassic();

        // Configure wake sources for future deep sleep
        PowerManager::configureWakeupSources(WAKE_BUTTON, LORA_DIO0);
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

#endif // ESP32_PLATFORM_TRAITS_H
