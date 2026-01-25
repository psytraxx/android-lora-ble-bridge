#ifndef ESP32_PLATFORM_TRAITS_H
#define ESP32_PLATFORM_TRAITS_H

#include "esp32/BLEManager.h"
#include "esp32/MessageBuffer.h"
#include "esp32/PowerManager.h"
#include "common/FirmwareConfig.h"
#include "common/Logging.h"
#include <Adafruit_SleepyDog.h>

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
        int watchdogMS = Watchdog.enable(WatchdogConstants::TIMEOUT_SECONDS * 1000);
        LOG_I(PLATFORM_TAG, "Watchdog enabled: %d ms", watchdogMS);
    }

    static void resetWatchdog()
    {
        Watchdog.reset();
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

    static bool isLoraWakeUp()
    {
        return PowerManager::isLoraWakeUp();
    }

    static void enterDeepSleep()
    {
        PowerManager::enterDeepSleep();
    }

    static String getMacSuffix()
    {
        uint64_t mac = ESP.getEfuseMac();
        uint8_t byte0 = (mac >> 40) & 0xFF;
        uint8_t byte1 = (mac >> 32) & 0xFF;
        uint16_t suffix = (byte1 << 8) | byte0;
        char buf[5];
        sprintf(buf, "%04X", suffix);
        return String(buf);
    }
};

#endif // ESP32_PLATFORM_TRAITS_H
