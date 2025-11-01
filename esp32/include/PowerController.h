#ifndef POWER_CONTROLLER_H
#define POWER_CONTROLLER_H

#include <Arduino.h>
#include "BLEManager.h"
#include "MessageBuffer.h"

/**
 * @file PowerController.h
 * @brief Power management helper that coordinates BLE advertising, sleep and
 *        activity timeouts.
 *
 * The PowerController observes BLE activity (via a callback) and drives a
 * simple state machine that decides when to advertise, remain connected, or
 * enter light sleep to conserve battery. The implementation is intentionally
 * lightweight and suitable for devices with limited power budgets.
 */

class PowerController
{
public:
    /**
     * @brief Construct PowerController.
     *
     * The constructor does not start timers or sleep; call begin() to pass
     * references to the BLE manager and message buffer used by the controller.
     */
    PowerController();

    /**
     * @brief Initialize the PowerController with required managers.
     * @param bleMgr Pointer to the BLEManager used to control advertising and
     *               query connection state. Must remain valid for the lifetime
     *               of this object.
     * @param buf Pointer to a MessageBuffer used to hold outgoing messages.
     */
    void begin(BLEManager *bleMgr, MessageBuffer *buf);

    /**
     * @brief Configure ESP32 power management settings.
     *
     * Sets CPU frequency scaling and enables light sleep mode for power savings.
     * This should be called early in setup() before other initialization.
     */
    void configurePowerManagement();

    /**
     * @brief Configure GPIO wakeup sources for light sleep.
     * @param wakeButton GPIO pin for boot button wakeup (LOW trigger)
     * @param loraDio0 GPIO pin for LoRa DIO0 wakeup (HIGH trigger)
     *
     * Configures the ESP32 to wake from light sleep on either:
     * - Boot button press (LOW level)
     * - LoRa packet reception (HIGH level on DIO0)
     */
    void configureWakeupSources(int wakeButton, int loraDio0);

    /**
     * @brief Periodic update called from the main loop.
     *
     * The PowerController manages BLE advertising and sleep cycles. After 30 seconds
     * of advertising, it enters light sleep mode. The device wakes from sleep when:
     * - Boot button (GPIO0) is pressed
     * - LoRa packet is received (DIO0 interrupt)
     *
     * Keep the call frequency reasonable (e.g., once every 100..500 ms).
     */
    void update();

    /**
     * @brief Static shim used as an activity callback by BLEManager.
     *
     * This static method forwards to the single PowerController instance.
     * It is safe to register as a C-style callback with external libraries.
     */
    static void activityCallbackStatic();

private:
    BLEManager *bleManager{nullptr};
    MessageBuffer *messageBuffer{nullptr};

    enum State
    {
        STATE_DISCONNECTED_ADVERTISING,
        STATE_DISCONNECTED_SLEEPING,
        STATE_CONNECTED
    } state{STATE_DISCONNECTED_ADVERTISING};

    unsigned long advertiseStartMillis{0};
    unsigned long lastActivityMillis{0};

    static PowerController *instance; // single instance for static callback

    void resetInactivityTimer();
};

#endif // POWER_CONTROLLER_H
