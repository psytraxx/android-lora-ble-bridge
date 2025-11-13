#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <functional>

/**
 * @file PowerManager.h
 * @brief Stateless power management helper for ESP32
 *
 * This class provides hardware-level power management operations without
 * application state. It is responsible for:
 *  - CPU frequency scaling configuration
 *  - Deep sleep mode entry
 *  - GPIO wakeup source configuration
 *
 * Design Goals:
 *  - Stateless: No state machine, timers, or application logic
 *  - Hardware-focused: Only deals with power/sleep registers
 *  - Decoupled: No dependencies on BLE, LoRa, or application components
 *  - Testable: Pure functions that can be tested in isolation
 *
 * Application-level decisions (when to sleep, when to wake, timeout policies)
 * are handled by ApplicationController.
 */
class PowerManager
{
public:
    /**
     * @brief Configure ESP32 power management (CPU frequency scaling)
     *
     * Sets up dynamic frequency scaling with:
     *  - Max frequency: Default CPU frequency (e.g., 160 MHz)  
     *  - Min frequency: 20 MHz
     *  - Light sleep: Disabled (manual sleep via enterDeepSleep)
     *
     * Call this once during setup() before other initialization.
     */
    static void configurePowerManagement();

    /**
     * @brief Configure GPIO pins for wakeup from deep sleep
     *
     * Sets up wake sources with proper pull resistors and RTC GPIO configuration:
     *  - Button: LOW level trigger with internal pull-up (pressed = LOW)
     *  - LoRa DIO0: HIGH level trigger with pull-down (LoRa interrupt = HIGH)
     *
     * Uses EXT0 for LoRa DIO0 (HIGH level) and EXT1 for button (LOW level).
     *
     * @param wakeButton GPIO pin for boot button (e.g., GPIO0)
     * @param loraDio0 GPIO pin for LoRa DIO0 interrupt
     */
    static void configureWakeupSources(int wakeButton, int loraDio0);

    /**
     * @brief Enter deep sleep mode (does not return)
     *
     * Enters deep sleep with configured wakeup sources. This function does NOT return -
     * the device will reset on wakeup and execution starts from setup().
     *
     * Wakeup sources must be configured via configureWakeupSources() first.
     *
     * The function will:
     *  1. Flush UART buffers to ensure logs are sent
     *  2. Enter deep sleep (CPU and most peripherals powered off)
     *  3. Device resets on wake - execution starts from beginning
     *
     * Wakeup sources:
     *  - EXT0: LoRa DIO0 going HIGH
     *  - EXT1: Wake button going LOW
     */
    static void enterDeepSleep();

    /**
     * @brief Print wakeup reason after boot from deep sleep
     *
     * Call this in setup() to log why the device woke up.
     * Useful for debugging and understanding boot behavior.
     */
    static void printWakeupReason();
};

#endif // POWER_MANAGER_H
