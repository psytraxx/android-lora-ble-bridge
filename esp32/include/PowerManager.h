#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

/**
 * @file PowerManager.h
 * @brief Stateless power management helper for ESP32
 *
 * This class provides hardware-level power management operations without
 * application state. It is responsible for:
 *  - CPU frequency scaling configuration
 *  - Light sleep mode entry/exit
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
     *  - Max frequency: Configured via CPU_FREQ_MHZ build flag
     *  - Min frequency: 20 MHz
     *  - Light sleep: Disabled (manual sleep via enterLightSleep)
     *
     * Call this once during setup() before other initialization.
     */
    static void configurePowerManagement();

    /**
     * @brief Configure GPIO pins for wakeup from light sleep
     *
     * Sets up wake sources with proper pull resistors and interrupt detection:
     *  - Button: LOW level trigger with internal pull-up (pressed = LOW)
     *  - LoRa DIO0: HIGH level trigger with pull-down (LoRa interrupt = HIGH)
     *
     * On ESP32 (original): Uses EXT0 wakeup for RTC-capable GPIOs (lower power),
     *                      falls back to GPIO wakeup for non-RTC GPIOs
     * On ESP32-S3/C3:      Uses GPIO wakeup (no EXT0 support)
     *
     * This method automatically selects the best wakeup mechanism based on
     * chip capabilities and GPIO pin properties.
     *
     * @param wakeButton GPIO pin for boot button (e.g., GPIO0)
     * @param loraDio0 GPIO pin for LoRa DIO0 interrupt
     */
    static void configureWakeupSources(int wakeButton, int loraDio0);

    /**
     * @brief Enter light sleep mode (blocking)
     *
     * Enters light sleep with RTC peripherals enabled until woken by
     * configured GPIO sources. This is a BLOCKING call - execution
     * resumes after a wake event occurs.
     *
     * Wakeup sources must be configured via configureWakeupSources() first.
     *
     * The function will:
     *  1. Configure RTC domain to stay powered (needed for GPIO wakeup)
     *  2. Flush UART buffers to ensure logs are sent
     *  3. Enter light sleep (CPU halted, RAM preserved)
     *  4. Wake on GPIO interrupt or timer
     *  5. Return the wakeup cause
     *
     * @return Wakeup cause (see esp_sleep_wakeup_cause_t):
     *         - ESP_SLEEP_WAKEUP_GPIO: GPIO interrupt (button or LoRa)
     *         - ESP_SLEEP_WAKEUP_EXT0: EXT0 wakeup (RTC GPIO, ESP32 only)
     *         - ESP_SLEEP_WAKEUP_TIMER: Timer expired
     *         - ESP_SLEEP_WAKEUP_UNDEFINED: Error occurred
     */
    static int enterLightSleep();

    /**
     * @brief Re-enable GPIO wakeup before sleep
     *
     * Some ESP32 variants require re-enabling GPIO wakeup before each sleep.
     * Call this immediately before enterLightSleep() if needed.
     */
    static void refreshWakeupSources();
};

#endif // POWER_MANAGER_H
