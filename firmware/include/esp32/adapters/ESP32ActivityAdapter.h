#ifndef ESP32_ACTIVITY_ADAPTER_H
#define ESP32_ACTIVITY_ADAPTER_H

#include "ports/IActivityPort.h"
#include "esp32/ApplicationController.h"

/**
 * @brief ESP32 Activity Tracking Adapter
 *
 * Wraps ApplicationController (complex state machine with mutex)
 */
class ESP32ActivityAdapter : public IActivityPort
{
public:
    ESP32ActivityAdapter() : appController() {}

    void markActivity() override
    {
        appController.notifyActivity();
    }

    unsigned long getInactivityDuration() override
    {
        return appController.getInactivityDuration();
    }

    void onBleConnected() override
    {
        appController.onBleConnected();
    }

    void onBleDisconnected() override
    {
        appController.onBleDisconnected();
    }

    bool isBleConnected() override
    {
        return appController.isConnected();
    }

private:
    ApplicationController appController;
};

#endif // ESP32_ACTIVITY_ADAPTER_H
