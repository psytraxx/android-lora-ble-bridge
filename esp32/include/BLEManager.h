#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/queue.h>
#include "Protocol.h"

/**
 * @file BLEManager.h
 * @brief Thin wrapper around NimBLE to provide an application-level BLE
 *        interface used by the android-lora-ble-bridge project.
 *
 * This header defines the BLEManager class and lightweight callback
 * helper classes used to adapt NimBLE events into the project's
 * messaging queue and activity callbacks.
 */

// Service and Characteristic UUIDs used by this project. These are
// application-level UUIDs and can be changed if interoperability with
// other clients is required.
#define SERVICE_UUID "00001234-0000-1000-8000-00805f9b34fb"
#define TX_CHARACTERISTIC_UUID "00005678-0000-1000-8000-00805f9b34fb"
#define RX_CHARACTERISTIC_UUID "00005679-0000-1000-8000-00805f9b34fb"

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
 * @brief High-level BLE manager used by the application.
 *
 * Responsibilities:
 *  - Initialize NimBLE and configure the service/characteristics used by the
 *    application
 *  - Accept writes to the RX characteristic and push messages into the
 *    provided FreeRTOS queue for forwarding to LoRa
 *  - Notify a connected client via the TX characteristic
 *  - Expose a small API used by the rest of the firmware (start/stop,
 *    sendMessage, process loop integration, activity callbacks)
 */
class BLEManager
{
public:
    /// Construct with a FreeRTOS queue to post incoming BLE messages to (LoRa side)
    explicit BLEManager(QueueHandle_t bleToLoraQueue);

    /// Initialize BLE stack and create service/characteristics.
    /// @param deviceName The BLE device name to advertise (defaults to DEVICE_NAME)
    /// @return true on success, false on failure
    bool setup(const char *deviceName = DEVICE_NAME);

    /// Start advertising the GATT service
    void startAdvertising();

    /// Stop advertising and optionally power down BLE hardware
    void stopAdvertising();

    /// Disconnect any currently connected BLE client
    void disconnect();

    /// Update activity timestamp; typically called on a BLE event
    void updateActivity();

    /// Set a callback invoked when BLE activity occurs (used by power manager)
    void setActivityCallback(void (*callback)()) { activityCallback = callback; }

    /// Check whether a client is currently connected
    bool isConnected() const;

    /// Send a Message to the connected BLE client using notifications.
    /// @param msg Message object defined in Protocol.h
    /// @return true if the notification was queued/sent, false otherwise
    bool sendMessage(const Message &msg);

    /// Periodic processing hook; call from main loop to let BLEManager service
    /// internal tasks if necessary.
    void process();

    /// Internal handler invoked by characteristic callbacks when data is received
    /// @param data Pointer to received bytes
    /// @param length Number of bytes received
    void onMessageReceived(const uint8_t *data, size_t length);

    /// Called by MyServerCallbacks when a client connects
    void onConnected();

    /// Called by MyServerCallbacks when a client disconnects
    void onDisconnected();

private:
    NimBLEServer *pServer{nullptr};
    NimBLECharacteristic *pTxCharacteristic{nullptr};
    NimBLECharacteristic *pRxCharacteristic{nullptr};
    NimBLEAdvertising *pAdvertising{nullptr};

    QueueHandle_t bleToLoraQueue;
    String deviceNameStr; // Store device name for debugging/logs

    MyServerCallbacks *serverCallbacks{nullptr};
    MyCharacteristicCallbacks *rxCallbacks{nullptr};

    void (*activityCallback)() = nullptr; // Optional activity callback

    unsigned long lastActivityTime{0};
};

#endif // BLE_MANAGER_H
