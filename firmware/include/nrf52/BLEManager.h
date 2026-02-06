#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <bluefruit.h>
#include <Arduino.h>
#include <common/Protocol.h>
#include <common/FirmwareConfig.h>
#include <common/MessageQueue.h>

/// Callback type for providing device info data on demand
typedef DeviceInfoData (*InfoDataProvider)();

/**
 * @brief BLE manager for nRF52 using Bluefruit library
 *
 * Uses the Adafruit Bluefruit nRF52 library for BLE communication.
 * Maintains protocol compatibility with ESP32 firmware.
 */
class BLEManager
{
public:
    /// Construct with a message queue to post incoming BLE messages to (LoRa side)
    explicit BLEManager(MessageQueue *bleToLoraQueue);

    /// Initialize BLE stack and create service/characteristics.
    bool setup(const char *deviceName);

    /// Start advertising the GATT service
    void startAdvertising();

    /// Stop advertising
    void stopAdvertising();

    /// Disconnect any currently connected BLE client
    void disconnect();

    /// Check whether a client is currently connected
    bool isConnected() const;

    /// Send a Message to the connected BLE client using notifications.
    bool sendMessage(const Message &msg);

    /// Check if client has enabled notifications
    bool areNotificationsEnabled() const { return notificationsEnabled; }

    /// Set callbacks for connection state changes
    void setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)());

    /// Set the callback that provides device info data on demand
    void setInfoDataProvider(InfoDataProvider provider) { infoProvider = provider; }

    /// Update the device info characteristic value (call periodically or when info changes)
    void updateDeviceInfo();

    // Bluefruit callbacks (public for callback registration)
    static void connectCallback(uint16_t conn_handle);
    static void disconnectCallback(uint16_t conn_handle, uint8_t reason);
    static void rxWriteCallback(uint16_t conn_hdl, BLECharacteristic *chr, uint8_t *data, uint16_t len);
    static void cccdCallback(uint16_t conn_hdl, BLECharacteristic *chr, uint16_t value);

private:
    // BLE Services
    BLEDis bledis;   // Device Information Service

    // Custom service and characteristics to exchange Protocol messages
    BLEService dataService;
    BLECharacteristic txCharacteristic;
    BLECharacteristic rxCharacteristic;
    BLECharacteristic infoCharacteristic;

    MessageQueue *bleToLoraQueue;
    String deviceNameStr;

    bool notificationsEnabled{false};
    bool isConnectedFlag{false};

    // Connection state callbacks
    void (*connectCallback_user)(){nullptr};
    void (*disconnectCallback_user)(){nullptr};

    // Device info provider callback
    InfoDataProvider infoProvider{nullptr};

    // Singleton for callbacks
    static BLEManager *instance;

    // Internal handlers
    void handleRxWrite(uint8_t *data, uint16_t len);
    void handleCccdWrite(uint16_t value);
};

#endif // BLE_MANAGER_H
