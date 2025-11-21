#ifndef NRF52_PLATFORM_TRAITS_H
#define NRF52_PLATFORM_TRAITS_H

#include "nrf52/BLEManager.h"
#include "nrf52/LoRaManager.h"
#include "nrf52/MessageBuffer.h"
#include "nrf52/PowerManager.h"
#include "nrf52/ApplicationController.h"
#include "nrf52/FirmwareConfig.h"

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
    using LoRaManager = ::LoRaManager;
    using StorageManager = ::MessageBuffer;
    using PowerManager = ::PowerManager;
    using ActivityManager = ::ApplicationController;

    // ========================================================================
    // Platform Capabilities
    // ========================================================================

    static constexpr bool HAS_WATCHDOG = false;
    static constexpr bool HAS_LED_MANAGER = false; // Uses direct GPIO
    static constexpr bool BLE_USES_QUEUE = true;   // nRF52 uses queue

    // ========================================================================
    // Constants
    // ========================================================================

    static constexpr unsigned long INACTIVITY_TIMEOUT_MS = PowerConstants::INACTIVITY_TIMEOUT_MS;
    static constexpr int LED_RX_BLINKS = LEDConstants::RX_BLINKS;
    static constexpr int LED_TX_BLINKS = LEDConstants::TX_BLINKS;

    // ========================================================================
    // Platform-Specific Initialization
    // ========================================================================

    static void initializeWatchdog() {}  // No watchdog on nRF52
    static void resetWatchdog() {}

    static void initializePower() {}  // PowerManager initialized via begin()

    static uint8_t readBatteryLevel(PowerManager &mgr)
    {
        return mgr.readBatteryLevel();
    }

    // ========================================================================
    // LED Control (Direct GPIO)
    // ========================================================================

    static void initializeLED()
    {
        pinMode(LED_PIN, OUTPUT);
        digitalWrite(LED_PIN, LOW);
    }

    static void ledOn() { digitalWrite(LED_PIN, HIGH); }
    static void ledOff() { digitalWrite(LED_PIN, LOW); }

    static void ledBlink(int count)
    {
        for (int i = 0; i < count; i++)
        {
            digitalWrite(LED_PIN, HIGH);
            delay(LEDConstants::BLINK_DURATION_MS);
            digitalWrite(LED_PIN, LOW);
            if (i < count - 1)
            {
                delay(LEDConstants::BLINK_DELAY_MS);
            }
        }
    }

    // ========================================================================
    // Activity Management Wrappers
    // ========================================================================

    static void markActivity(ActivityManager &mgr) { mgr.markActivity(); }
    static unsigned long getInactivityDuration(ActivityManager &mgr) { return mgr.getTimeSinceLastActivity(); }
    static void onBleConnected(ActivityManager &mgr) { mgr.setBLEConnected(true); }
    static void onBleDisconnected(ActivityManager &mgr) { mgr.setBLEConnected(false); }
    static bool isBleConnected(ActivityManager &mgr) { return mgr.isBLEConnected(); }
};

#endif // NRF52_PLATFORM_TRAITS_H
