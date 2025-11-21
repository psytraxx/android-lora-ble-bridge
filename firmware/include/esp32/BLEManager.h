#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <NimBLEDevice.h>
#include "Protocol.h"
#include "FirmwareConfig.h"

/**
 * @file BLEManager.h
 * @brief Thin wrapper around NimBLE to provide an application-level BLE
 *        interface used by the android-lora-ble-bridge project.
 *
 * This header defines the BLEManager class and lightweight callback
 * helper classes used to adapt NimBLE events into the project's
 * messaging queue and activity callbacks.
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

    /**
     * @brief Called by NimBLE when a client connects.
     * @param pServer NimBLE server instance.
     * @param connInfo Connection metadata.
     */
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo);

    /**
     * @brief Called by NimBLE when a client disconnects.
     * @param pServer NimBLE server instance.
     * @param connInfo Connection metadata.
     * @param reason Disconnect reason code (implementation defined).
     */
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

    /**
     * @brief Called by NimBLE when the RX characteristic is written by a
     *        connected client.
     * @param pCharacteristic The characteristic object that was written.
     * @param connInfo Connection metadata.
     */
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

    /**
     * @brief Called when client subscribes/unsubscribes to notifications
     * @param pCharacteristic The characteristic being subscribed to
     * @param connInfo Connection metadata
     * @param subValue Subscription value (0x0001 = notifications enabled)
     */
    void onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue);

private:
    BLEManager *bleManager;
};

/**
 * @brief Adapter for NimBLECharacteristicCallbacks to handle battery level reads
 */
class BatteryCharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
public:
    /**
     * @brief Called when client reads battery level characteristic
     * @param pCharacteristic The characteristic being read
     * @param connInfo Connection metadata
     */
    void onRead(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo);
};

/**
 * @brief High-level BLE manager used by the application.
 *
 * Responsibilities:
 *  - Initialize NimBLE and configure the service/characteristics used by the
 *    application
 *  - Accept writes to the RX characteristic and call message callback
 *  - Notify a connected client via the TX characteristic
 *  - Expose a small API used by the rest of the firmware (start/stop,
 *    sendMessage, activity callbacks)
 */
class BLEManager
{
public:
    /// Construct BLE manager
    BLEManager();

    /// Initialize BLE stack and create service/characteristics.
    /// @param deviceName The BLE device name to advertise (defined in platformio.ini)
    /// @return true on success, false on failure
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
    /// @param msg Message object defined in Protocol.h
    /// @return true if the notification was queued/sent, false otherwise
    bool sendMessage(const Message &msg);

    /// Internal handler invoked by characteristic callbacks when data is received
    /// @param data Pointer to received bytes
    /// @param length Number of bytes received
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
    /// @param onConnect Callback when client connects
    /// @param onDisconnect Callback when client disconnects
    void setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)());

    /// Set callback for when BLE message is received
    /// @param callback Function to call with received message
    void setMessageCallback(void (*callback)(const Message &msg));

    /// Update battery level characteristic
    /// @param level Battery percentage (0-100)
    void updateBatteryLevel(uint8_t level);

private:
    static constexpr uint16_t kInvalidConnHandle = 0xFFFF;

    NimBLEServer *pServer{nullptr};
    NimBLECharacteristic *pTxCharacteristic{nullptr};
    NimBLECharacteristic *pRxCharacteristic{nullptr};
    NimBLECharacteristic *pBatteryCharacteristic{nullptr};
    NimBLEAdvertising *pAdvertising{nullptr};

    std::string deviceNameStr; // Store device name for debugging/logs

    MyServerCallbacks *serverCallbacks{nullptr};
    MyCharacteristicCallbacks *rxCallbacks{nullptr};
    TxCharacteristicCallbacks *txCallbacks{nullptr};

    uint16_t currentConnHandle{kInvalidConnHandle};
    bool notificationsEnabled{false};

    // Callbacks
    void (*connectCallback)(){nullptr};
    void (*disconnectCallback)(){nullptr};
    void (*messageCallback)(const Message &msg){nullptr};
};

#endif // BLE_MANAGER_H
