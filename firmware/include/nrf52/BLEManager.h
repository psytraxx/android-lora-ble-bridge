#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <bluefruit.h>
#include <Arduino.h>
#include <common/FirmwareConfig.h>
#include <common/MessageQueue.h>
#include "meshtastic/mesh.pb.h"
#include <pb_encode.h>
#include <pb_decode.h>

/**
 * @file BLEManager.h (nRF52)
 * @brief Meshtastic-compatible BLE service using Bluefruit
 *
 * Provides FromRadio/ToRadio/FromNum characteristics matching
 * the official Meshtastic BLE API for app compatibility.
 */
class BLEManager
{
public:
    /// Construct with queue for BLE->LoRa message passing
    explicit BLEManager(MessageQueue<meshtastic_ToRadio> *toRadioQueue);

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

    /// Set callbacks for connection state changes
    void setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)());

    // Bluefruit static callbacks
    static void connectCallback(uint16_t conn_handle);
    static void disconnectCallback(uint16_t conn_handle, uint8_t reason);
    static void toRadioWriteCallback(uint16_t conn_hdl, BLECharacteristic *chr,
                                     uint8_t *data, uint16_t len);
    static void fromRadioCccdCallback(uint16_t conn_hdl, BLECharacteristic *chr,
                                      uint16_t value);

private:
    // BLE Services
    BLEDis _bledis;

    // Meshtastic service and characteristics
    BLEService _meshService;
    BLECharacteristic _fromRadioChar;
    BLECharacteristic _toRadioChar;
    BLECharacteristic _fromNumChar;

    MessageQueue<meshtastic_ToRadio> *_toRadioQueue;
    String _deviceNameStr;

    bool _notificationsEnabled{false};
    bool _isConnectedFlag{false};
    uint32_t _fromNum{0};
    bool _configDownloadInProgress{false};

    void (*_connectCallback_user)(){nullptr};
    void (*_disconnectCallback_user)(){nullptr};

    // Singleton for callbacks
    static BLEManager *instance;

    // Internal handlers
    void handleToRadioWrite(uint8_t *data, uint16_t len);
    void handleCccdWrite(uint16_t value);

    /// Serialize FromRadio protobuf to bytes
    size_t serializeFromRadio(const meshtastic_FromRadio *fromRadio,
                              uint8_t *buffer, size_t maxSize);

    /// Send the full config download sequence
    void sendConfigDownload(uint32_t configId);
};

#endif // BLE_MANAGER_H
