#ifndef NRF52_PLATFORM_TRAITS_H
#define NRF52_PLATFORM_TRAITS_H

#include "nrf52/BLEManager.h"
#include "nrf52/MessageBuffer.h"
#include "nrf52/PowerManager.h"
#include "nrf52/FirmwareConfig.h"
#include <Adafruit_SleepyDog.h>

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
    // Platform Capabilities
    // ========================================================================

    static constexpr bool HAS_WATCHDOG = true;
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

    static void initializeWatchdog()
    {
        int watchdogMS = Watchdog.enable(WatchdogConstants::TIMEOUT_SECONDS * 1000);
        Serial.print("Watchdog enabled: ");
        Serial.print(watchdogMS);
        Serial.println(" ms");
    }

    static void resetWatchdog()
    {
        Watchdog.reset();
    }

    static void initializePower() {} // PowerManager initialized via begin()

    static uint8_t readBatteryLevel(PowerManager &mgr)
    {
        return mgr.readBatteryLevel();
    }

    // ========================================================================
    // LED Control (Direct GPIO) - Non-blocking
    // ========================================================================

    static bool blinkActive;
    static int blinkCount;
    static int blinkTarget;
    static unsigned long lastBlinkChange;
    static bool blinkLedState;

    static void initializeLED()
    {
        pinMode(LED_PIN, OUTPUT);
        digitalWrite(LED_PIN, LOW);
        blinkActive = false;
        blinkCount = 0;
        blinkTarget = 0;
        lastBlinkChange = 0;
        blinkLedState = false;
    }

    static void ledOn() { digitalWrite(LED_PIN, HIGH); }
    static void ledOff() { digitalWrite(LED_PIN, LOW); }

    static void ledBlink(int count)
    {
        blinkActive = true;
        blinkTarget = count;
        blinkCount = 0;
        blinkLedState = false;
        lastBlinkChange = millis();
        digitalWrite(LED_PIN, HIGH); // Start first blink
        blinkLedState = true;
    }

    static void updateLED()
    {
        if (!blinkActive)
            return;

        unsigned long now = millis();
        unsigned long elapsed = (unsigned long)(now - lastBlinkChange);

        if (blinkLedState)
        {
            // LED is currently ON
            if (elapsed >= LEDConstants::BLINK_DURATION_MS)
            {
                digitalWrite(LED_PIN, LOW);
                blinkLedState = false;
                lastBlinkChange = now;
                blinkCount++;

                if (blinkCount >= blinkTarget)
                {
                    blinkActive = false; // Completed all blinks
                }
            }
        }
        else
        {
            // LED is currently OFF (between blinks)
            if (elapsed >= LEDConstants::BLINK_DELAY_MS && blinkCount < blinkTarget)
            {
                digitalWrite(LED_PIN, HIGH);
                blinkLedState = true;
                lastBlinkChange = now;
            }
        }
    }
};

// Define static LED state variables
bool NRF52PlatformTraits::blinkActive = false;
int NRF52PlatformTraits::blinkCount = 0;
int NRF52PlatformTraits::blinkTarget = 0;
unsigned long NRF52PlatformTraits::lastBlinkChange = 0;
bool NRF52PlatformTraits::blinkLedState = false;

#endif // NRF52_PLATFORM_TRAITS_H
