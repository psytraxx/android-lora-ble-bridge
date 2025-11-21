#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <bluefruit.h>
#include <Arduino.h>
#include "Protocol.h"
#include "FirmwareConfig.h"
#include "common/MessageQueue.h"

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
    explicit BLEManager(MessageQueue* bleToLoraQueue);

    /// Initialize BLE stack and create service/characteristics.
    /// @param deviceName The BLE device name to advertise
    /// @return true on success, false on failure
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
    /// @param msg Message object defined in Protocol.h
    /// @return true if the notification was sent, false otherwise
    bool sendMessage(const Message &msg);

    /// Check if client has enabled notifications
    bool areNotificationsEnabled() const { return notificationsEnabled; }

    /// Set callbacks for connection state changes
    void setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)());

    /// Update battery level (0-100%)
    void updateBatteryLevel(uint8_t level);

    // Bluefruit callbacks (public for callback registration)
    static void connectCallback(uint16_t conn_handle);
    static void disconnectCallback(uint16_t conn_handle, uint8_t reason);
    static void rxWriteCallback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len);
    static void cccdCallback(uint16_t conn_hdl, BLECharacteristic* chr, uint16_t value);

private:
    // BLE Services
    BLEDis bledis;    // Device Information Service
    BLEUart bleuart;  // UART Service (not used, but helpful for debugging)
    BLEBas blebas;    // Battery Service

    // Custom LoRa service and characteristics
    BLEService loraService;
    BLECharacteristic txCharacteristic;
    BLECharacteristic rxCharacteristic;

    MessageQueue* bleToLoraQueue;
    String deviceNameStr;

    bool notificationsEnabled{false};
    bool isConnectedFlag{false};
    uint8_t lastBatteryLevel{100};

    // Connection state callbacks
    void (*connectCallback_user)(){nullptr};
    void (*disconnectCallback_user)(){nullptr};

    // Singleton for callbacks
    static BLEManager* instance;

    // Internal handlers
    void handleRxWrite(uint8_t* data, uint16_t len);
    void handleCccdWrite(uint16_t value);
};

#endif // BLE_MANAGER_H
