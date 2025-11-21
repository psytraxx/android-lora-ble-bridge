#ifndef ESP32_BLE_ADAPTER_H
#define ESP32_BLE_ADAPTER_H

#include "ports/IBLEPort.h"
#include "esp32/BLEManager.h"

/**
 * @brief ESP32 BLE Adapter (NimBLE)
 *
 * Wraps BLEManager which uses NimBLE
 */
class ESP32BLEAdapter : public IBLEPort
{
public:
    ESP32BLEAdapter() : bleManager() {}

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
        return bleManager.isConnected();
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
        bleManager.setMessageCallback(onMessage);
    }

private:
    BLEManager bleManager;
};

#endif // ESP32_BLE_ADAPTER_H
