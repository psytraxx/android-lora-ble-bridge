#ifndef NRF52_ACTIVITY_ADAPTER_H
#define NRF52_ACTIVITY_ADAPTER_H

#include "ports/IActivityPort.h"
#include "nrf52/ApplicationController.h"

/**
 * @brief nRF52 Activity Tracking Adapter
 *
 * Wraps ApplicationController (simple state tracking, no mutex)
 */
class NRF52ActivityAdapter : public IActivityPort
{
public:
    NRF52ActivityAdapter() : appController() {}

    void markActivity() override
    {
        appController.markActivity();
    }

    unsigned long getInactivityDuration() override
    {
        return appController.getTimeSinceLastActivity();
    }

    void onBleConnected() override
    {
        appController.setBLEConnected(true);
    }

    void onBleDisconnected() override
    {
        appController.setBLEConnected(false);
    }

    bool isBleConnected() override
    {
        return appController.isBLEConnected();
    }

private:
    ApplicationController appController;
};

#endif // NRF52_ACTIVITY_ADAPTER_H
