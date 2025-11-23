#ifndef ESP32_PLATFORM_TRAITS_H
#define ESP32_PLATFORM_TRAITS_H

#include "esp32/BLEManager.h"
#include "esp32/MessageBuffer.h"
#include "esp32/PowerManager.h"
#include "esp32/LEDManager.h"
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

    // ========================================================================
    // Platform Capabilities
    // ========================================================================

    static constexpr bool HAS_WATCHDOG = true;
    static constexpr bool HAS_LED_MANAGER = true;
    static constexpr bool BLE_USES_QUEUE = false; // ESP32 uses callbacks

    // ========================================================================
    // Constants
    // ========================================================================

    static constexpr unsigned long INACTIVITY_TIMEOUT_MS = PowerConstants::INACTIVITY_TIMEOUT_MS;
    static constexpr int LED_RX_BLINKS = LEDConstants::RX_BLINKS;
    static constexpr int LED_TX_BLINKS = LEDConstants::TX_BLINKS;

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
        PowerManager::configurePowerManagement();

        // Disable unused radios to save power
        PowerManager::disableWiFi();
        PowerManager::disableBluetoothClassic();

        // Print wakeup reason if resuming from deep sleep
        PowerManager::printWakeupReason();

        // Configure wake sources for future deep sleep
        PowerManager::configureWakeupSources(WAKE_BUTTON, LORA_DIO0);
    }

    static uint8_t readBatteryLevel()
    {
        return PowerManager::readBatteryLevel();
    }

    static void sleep()
    {
        PowerManager::enterDeepSleep();
    }

    // ========================================================================
    // LED Control
    // ========================================================================

#ifdef LED_PIN
    static LEDManager ledManager;

    static void initializeLED()
    {
        ledManager.setup();
    }

    static void ledOn() { ledManager.setOn(); }
    static void ledOff() { ledManager.setOff(); }
    static void ledBlink(int count) { ledManager.blink(count); }
    static void updateLED() { ledManager.update(); }
#else
    static void initializeLED() {}
    static void ledOn() {}
    static void ledOff() {}
    static void ledBlink(int count) { (void)count; }
    static void updateLED() {}
#endif
};

#ifdef LED_PIN
LEDManager ESP32PlatformTraits::ledManager(LED_PIN);
#endif

#endif // ESP32_PLATFORM_TRAITS_H
