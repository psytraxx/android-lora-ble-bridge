#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H
#include <cstdint>
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
 * are handled by FreeRTOS timers in unified_main.cpp.
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
    static bool configurePowerManagement();

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
     *  1. Disable external peripherals via VEXT
     *  2. Set unused GPIOs to input mode to minimize leakage current
     *  3. Flush UART buffers to ensure logs are sent
     *  4. Enter deep sleep (CPU and most peripherals powered off)
     *  5. Device resets on wake - execution starts from beginning
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

    /**
     * @brief Read battery voltage from ADC
     *
     * Reads the battery voltage via ADC and applies the voltage divider ratio.
     * On Heltec boards, enables ADC control pin before reading.
     *
     * Features:
     *  - Multiple sample averaging to reduce noise
     *  - Low-pass filtering for smooth readings
     *  - ESP32 ADC calibration using eFuse data
     *  - Minimum read interval throttling (5 seconds)
     *
     * @return Battery voltage in millivolts (e.g., 3700mV)
     */
    static uint16_t readBatteryVoltage();

    /**
     * @brief Read battery level as percentage
     *
     * Reads battery voltage and converts to percentage (0-100%) using
     * OCV (Open Circuit Voltage) lookup table for accurate Li-ion readings.
     *
     * This follows the BLE Battery Service standard which uses uint8 (0-100%).
     * Uses interpolation between OCV points for smooth percentage values.
     *
     * @return Battery level percentage (0-100)
     */
    static uint8_t readBatteryLevel();

    /**
     * @brief Disable WiFi to save power
     *
     * Disables WiFi completely, including stopping the WiFi driver
     * and deinitializing WiFi resources. This saves significant power
     * when WiFi is not needed.
     */
    static void disableWiFi();

    static void disableBluetoothClassic();

    // Import common WakeupReason into class scope for unified access
    using WakeupReason = ::WakeupReason;

    /**
     * @brief Determine the cause of the system wakeup was from LoRa
     *
     * @return true if the wakeup source was LoRa, false otherwise
     */
    static bool isLoraWakeUp();

private:
    /**
     * @brief Disable external peripherals by setting VEXT to input mode
     *
     * On Heltec boards, VEXT (GPIO 36) controls power to external peripherals
     * like sensors and displays. Setting it to INPUT mode cuts power.
     */
    static void disableExternalPeripherals();

    /**
     * @brief Get battery percentage from voltage using OCV lookup table
     *
     * Uses actual LiPo discharge curve data with interpolation between points
     * for more accurate battery percentage reporting.
     *
     * Based on Meshtastic OCV lookup table for single-cell Li-ion batteries.
     *
     * @param voltage Battery voltage in millivolts (per cell)
     * @return Battery percentage (0-100)
     */
    static uint8_t voltageToPercentage(uint16_t voltagePerCellMv);

    /**
     * @brief Enable ADC for battery voltage reading
     *
     * On Heltec boards with ADC_CTRL pin, this enables the voltage divider
     * circuit. Waits for voltage to stabilize before reading.
     */
    static void batteryAdcEnable();

    /**
     * @brief Disable ADC after battery voltage reading
     *
     * On Heltec boards with ADC_CTRL pin, this disables the voltage divider
     * circuit to save power.
     */
    static void batteryAdcDisable();

    /**
     * @brief Read calibrated ADC value with multiple samples
     *
     * Uses ESP32 ADC calibration from eFuse and averages multiple samples
     * to reduce noise.
     *
     * @return Calibrated ADC value
     */
    static uint32_t espAdcRead();
};

#endif // POWER_MANAGER_H
