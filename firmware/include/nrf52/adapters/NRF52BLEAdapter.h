#ifndef NRF52_BLE_ADAPTER_H
#define NRF52_BLE_ADAPTER_H

#include "ports/IBLEPort.h"
#include "nrf52/BLEManager.h"
#include "common/MessageQueue.h"

/**
 * @brief nRF52 BLE Adapter (Bluefruit)
 *
 * Wraps BLEManager which uses Bluefruit (Adafruit nRF52)
 * NOTE: nRF52 BLEManager requires a MessageQueue pointer for incoming messages
 */
class NRF52BLEAdapter : public IBLEPort
{
public:
    NRF52BLEAdapter(MessageQueue *bleToLoraQueue)
        : bleManager(bleToLoraQueue), messageCallback(nullptr) {}

    bool setup(const char *deviceName) override
    {
        return bleManager.setup(deviceName);
    }

    void startAdvertising() override
    {
        bleManager.startAdvertising();
    }

    bool isConnected() override
    {
        return bleManager.isConnected() && bleManager.areNotificationsEnabled();
    }

    bool sendMessage(const Message &msg) override
    {
        return bleManager.sendMessage(msg);
    }

    void updateBatteryLevel(uint8_t level) override
    {
        bleManager.updateBatteryLevel(level);
    }

    void disconnect() override
    {
        bleManager.disconnect();
    }

    void setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)()) override
    {
        bleManager.setConnectionCallbacks(onConnect, onDisconnect);
    }

    void setMessageCallback(void (*onMessage)(const Message &)) override
    {
        // nRF52 BLEManager uses a queue directly instead of a callback
        // Store the callback for potential future use, but the queue handles it
        messageCallback = onMessage;
    }

private:
    BLEManager bleManager;
    void (*messageCallback)(const Message &);
};

#endif // NRF52_BLE_ADAPTER_H
