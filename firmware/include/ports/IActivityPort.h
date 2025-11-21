#ifndef IACTIVITY_PORT_H
#define IACTIVITY_PORT_H

/**
 * @brief Activity Tracking Port Interface (Hexagonal Architecture)
 *
 * Abstracts activity tracking and inactivity timeout management:
 * - ESP32: ApplicationController with mutex and complex state machine
 * - nRF52: ApplicationController with simple state tracking
 */
class IActivityPort
{
public:
    virtual ~IActivityPort() = default;

    /**
     * @brief Mark activity (resets inactivity timer)
     * Called when BLE message received or LoRa packet handled
     */
    virtual void markActivity() = 0;

    /**
     * @brief Get time since last activity
     * @return Milliseconds since last activity
     */
    virtual unsigned long getInactivityDuration() = 0;

    /**
     * @brief Handle BLE connection event
     */
    virtual void onBleConnected() = 0;

    /**
     * @brief Handle BLE disconnection event
     */
    virtual void onBleDisconnected() = 0;

    /**
     * @brief Check if BLE is currently connected
     * @return true if connected, false otherwise
     */
    virtual bool isBleConnected() = 0;
};

#endif // IACTIVITY_PORT_H
