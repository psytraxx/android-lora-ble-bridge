#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <NimBLEDevice.h>
#include <common/Protocol.h>
#include <common/FirmwareConfig.h>
#include <common/MessageQueue.h>

/**
 * @file BLEManager.h
 * @brief Thin wrapper around NimBLE to provide an application-level BLE
 *        interface used by the android-lora-ble-bridge project.
 */

class BLEManager;

/**
 * @brief Adapter for NimBLEServerCallbacks that forwards connect/disconnect
 *        events to the owning BLEManager instance.
 */
class MyServerCallbacks : public NimBLEServerCallbacks
{
public:
    explicit MyServerCallbacks(BLEManager *manager) : bleManager(manager) {}

    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo);
    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason);

private:
    BLEManager *bleManager;
};

/**
 * @brief Adapter for NimBLECharacteristicCallbacks to receive writes to the
 *        RX characteristic and forward the payload to BLEManager.
 */
class MyCharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
public:
    explicit MyCharacteristicCallbacks(BLEManager *manager) : bleManager(manager) {}

    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo);

private:
    BLEManager *bleManager;
};

/**
 * @brief Adapter for NimBLECharacteristicCallbacks to detect when Android
 *        enables/disables notifications on the TX characteristic.
 */
class TxCharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
public:
    explicit TxCharacteristicCallbacks(BLEManager *manager) : bleManager(manager) {}

    void onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue);

private:
    BLEManager *bleManager;
};

/**
 * @brief Adapter for NimBLECharacteristicCallbacks to handle device info reads.
 *        Populates the characteristic with fresh data on each read.
 */
class InfoCharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
public:
    explicit InfoCharacteristicCallbacks(BLEManager *manager) : bleManager(manager) {}

    void onRead(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override;

private:
    BLEManager *bleManager;
};

/// Callback type for providing device info data on demand
typedef DeviceInfoData (*InfoDataProvider)();

/**
 * @brief High-level BLE manager used by the application.
 */
class BLEManager
{
public:
    /// Construct BLE manager with message queue for received messages
    BLEManager(MessageQueue *bleToLoraQueue);

    /// Initialize BLE stack and create service/characteristics.
    bool setup(const char *deviceName);

    /// Start advertising the GATT service
    void startAdvertising();

    /// Stop advertising and optionally power down BLE hardware
    void stopAdvertising();

    /// Disconnect any currently connected BLE client
    void disconnect();

    /// Check whether a client is currently connected
    bool isConnected() const;

    /// Send a Message to the connected BLE client using notifications.
    bool sendMessage(const Message &msg);

    /// Internal handler invoked by characteristic callbacks when data is received
    void onMessageReceived(const uint8_t *data, size_t length);

    /// Called by MyServerCallbacks when a client connects
    void onConnected(uint16_t connHandle);

    /// Called by MyServerCallbacks when a client disconnects
    void onDisconnected(uint16_t connHandle);

    /// Called by TxCharacteristicCallbacks when notifications are enabled/disabled
    void onNotificationsEnabled(bool enabled);

    /// Check if client has enabled notifications (Android is ready to receive)
    bool areNotificationsEnabled() const { return notificationsEnabled; }

    /// Set callbacks for connection state changes
    void setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)());

    /// Set the callback that provides device info data on demand
    void setInfoDataProvider(InfoDataProvider provider) { infoProvider = provider; }

    /// Update the device info characteristic value
    void updateDeviceInfo();

    /// Get device info from the registered provider (called by InfoCharacteristicCallbacks)
    DeviceInfoData getDeviceInfo() const;

private:
    static constexpr uint16_t kInvalidConnHandle = 0xFFFF;

    NimBLEServer *pServer{nullptr};
    NimBLECharacteristic *pTxCharacteristic{nullptr};
    NimBLECharacteristic *pRxCharacteristic{nullptr};
    NimBLECharacteristic *pInfoCharacteristic{nullptr};
    NimBLEAdvertising *pAdvertising{nullptr};

    std::string deviceNameStr;

    MyServerCallbacks *serverCallbacks{nullptr};
    MyCharacteristicCallbacks *rxCallbacks{nullptr};
    TxCharacteristicCallbacks *txCallbacks{nullptr};

    uint16_t currentConnHandle{kInvalidConnHandle};
    bool notificationsEnabled{false};

    MessageQueue *bleToLoraQueue{nullptr};

    // Connection state callbacks
    void (*connectCallback)(){nullptr};
    void (*disconnectCallback)(){nullptr};

    // Device info provider callback
    InfoDataProvider infoProvider{nullptr};
};

#endif // BLE_MANAGER_H
