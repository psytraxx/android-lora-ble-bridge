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
 * enter deep sleep to conserve battery. The implementation is intentionally
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
     * @brief Periodic update called from the main loop.
     *
     * The PowerController implements a deep-sleep-first policy and will
     * transition the system into deep sleep where appropriate. Keep the call
     * frequency reasonable (e.g., once every 100..500 ms).
     */
    void update();

    /**
     * @brief Static shim used as an activity callback by BLEManager.
     *
     * This static method forwards to the single PowerController instance.
     * It is safe to register as a C-style callback with external libraries.
     */
    static void activityCallbackStatic();

    /**
     * @brief Start a BLE pairing/advertising window (called after a boot-button wake)
     * The window duration is PAIR_WINDOW_MS (60 seconds).
     */
    void startPairingWindow();

    /**
     * @brief Enter deep sleep immediately (optionally set a short RTC timer wake)
     * @param microseconds If non-zero, set RTC timer wake for this duration before deep-sleep
     */
    void enterDeepSleepNow(uint64_t microseconds = 0);

private:
    BLEManager *bleManager{nullptr};
    MessageBuffer *messageBuffer{nullptr};

    enum State
    {
        STATE_DEEP_SLEEP,
        STATE_ADVERTISING_WINDOW,
        STATE_CONNECTED
    } state{STATE_DEEP_SLEEP};

    // Timing constants (ms)
    static const unsigned long PAIR_WINDOW_MS = 60000UL;    // 60s pairing/advertise window
    static const unsigned long PAIRED_TIMEOUT_MS = 60000UL; // 60s paired idle timeout
    static const unsigned long POST_FORWARD_MS = 10000UL;   // 10s grace after forwarding

    unsigned long advertiseStartMillis{0};
    unsigned long lastActivityMillis{0};

    static PowerController *instance; // single instance for static callback

    void resetInactivityTimer();
};

#endif // POWER_CONTROLLER_H
