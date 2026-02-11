#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <NimBLEDevice.h>
#include <common/FirmwareConfig.h>
#include <common/MessageQueue.h>
#include "meshtastic/mesh.pb.h"
#include <pb_encode.h>
#include <pb_decode.h>

/**
 * @file BLEManager.h (ESP32)
 * @brief Meshtastic-compatible BLE service using NimBLE
 *
 * Provides FromRadio/ToRadio/FromNum characteristics matching
 * the official Meshtastic BLE API for app compatibility.
 */

class BLEManager;

/**
 * @brief Server callbacks for connect/disconnect events
 */
class MeshServerCallbacks : public NimBLEServerCallbacks
{
public:
    explicit MeshServerCallbacks(BLEManager *manager) : bleManager(manager) {}
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo);
    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason);

private:
    BLEManager *bleManager;
};

/**
 * @brief ToRadio write callback — receives protobuf from phone
 */
class ToRadioCallbacks : public NimBLECharacteristicCallbacks
{
public:
    explicit ToRadioCallbacks(BLEManager *manager) : bleManager(manager) {}
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo);

private:
    BLEManager *bleManager;
};

/**
 * @brief FromRadio subscribe callback — detects notification enable/disable
 */
class FromRadioCallbacks : public NimBLECharacteristicCallbacks
{
public:
    explicit FromRadioCallbacks(BLEManager *manager) : bleManager(manager) {}
    void onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue);

private:
    BLEManager *bleManager;
};

/**
 * @brief Meshtastic BLE manager for ESP32 (NimBLE)
 */
class BLEManager
{
public:
    /// Construct with queues for BLE<->LoRa message passing
    BLEManager(MessageQueue<meshtastic_ToRadio> *toRadioQueue);

    /// Initialize BLE stack and create Meshtastic service
    bool setup(const char *deviceName);

    /// Start BLE advertising
    void startAdvertising();

    /// Stop BLE advertising
    void stopAdvertising();

    /// Disconnect current client
    void disconnect();

    /// Check if a client is connected
    bool isConnected() const;

    /// Check if notifications are enabled
    bool areNotificationsEnabled() const { return _notificationsEnabled; }

    /// Send a FromRadio message to the connected phone
    bool sendFromRadio(const meshtastic_FromRadio *fromRadio);

    /// Increment and notify the FromNum counter
    void incrementFromNum();

    /// Get current FromNum value
    uint32_t getFromNum() const { return _fromNum; }

    /// Handle config download request (want_config_id)
    void handleConfigRequest(uint32_t configId);

    /// Check if config download is in progress
    bool isConfigDownloadInProgress() const { return _configDownloadInProgress; }

    // Callbacks from NimBLE adapter classes
    void onConnected(uint16_t connHandle);
    void onDisconnected(uint16_t connHandle);
    void onNotificationsEnabled(bool enabled);

    /// Process a received ToRadio message (called from callback)
    void onToRadioReceived(const uint8_t *data, size_t length);

    /// Set callbacks for connection state changes
    void setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)());

private:
    static constexpr uint16_t kInvalidConnHandle = 0xFFFF;

    NimBLEServer *_pServer{nullptr};
    NimBLECharacteristic *_fromRadioChar{nullptr};
    NimBLECharacteristic *_toRadioChar{nullptr};
    NimBLECharacteristic *_fromNumChar{nullptr};
    NimBLEAdvertising *_pAdvertising{nullptr};

    std::string _deviceNameStr;

    MeshServerCallbacks *_serverCallbacks{nullptr};
    ToRadioCallbacks *_toRadioCallbacks{nullptr};
    FromRadioCallbacks *_fromRadioCallbacks{nullptr};

    uint16_t _currentConnHandle{kInvalidConnHandle};
    bool _notificationsEnabled{false};
    uint32_t _fromNum{0};
    bool _configDownloadInProgress{false};

    MessageQueue<meshtastic_ToRadio> *_toRadioQueue{nullptr};

    void (*_connectCallback)(){nullptr};
    void (*_disconnectCallback)(){nullptr};

    /// Serialize FromRadio protobuf to bytes
    size_t serializeFromRadio(const meshtastic_FromRadio *fromRadio,
                              uint8_t *buffer, size_t maxSize);

    /// Send the full config download sequence
    void sendConfigDownload(uint32_t configId);
};

#endif // BLE_MANAGER_H
