#ifndef APPLICATION_CONTROLLER_H
#define APPLICATION_CONTROLLER_H

#include <Arduino.h>

/**
 * @brief Application state manager for nRF52
 *
 * Provides centralized state management (simplified, no threading)
 */
class ApplicationController
{
public:
    ApplicationController();

    // BLE state management
    void setBLEConnected(bool connected);
    bool isBLEConnected() const;

    // Activity tracking (for power management)
    void markActivity();
    unsigned long getLastActivityTime() const;
    unsigned long getTimeSinceLastActivity() const;

    // Connection duration tracking (for Android BLE setup delay)
    unsigned long getConnectionDuration() const;

    // Message counters
    void incrementMessagesSent();
    void incrementMessagesReceived();
    uint32_t getMessagesSent() const;
    uint32_t getMessagesReceived() const;

private:
    // State variables
    bool bleConnected;
    unsigned long lastActivityTime;
    unsigned long connectionEstablishedTime;
    uint32_t messagesSent;
    uint32_t messagesReceived;
};

#endif // APPLICATION_CONTROLLER_H
