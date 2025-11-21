#ifndef IBLE_PORT_H
#define IBLE_PORT_H

#include "Protocol.h"

/**
 * @brief BLE Port Interface (Hexagonal Architecture)
 *
 * Abstracts BLE functionality across different platforms:
 * - ESP32: NimBLE
 * - nRF52: Bluefruit (Adafruit)
 */
class IBLEPort
{
public:
    virtual ~IBLEPort() = default;

    /**
     * @brief Initialize BLE with device name
     * @param deviceName The BLE device name to advertise
     * @return true on success, false on failure
     */
    virtual bool setup(const char *deviceName) = 0;

    /**
     * @brief Start BLE advertising
     */
    virtual void startAdvertising() = 0;

    /**
     * @brief Check if a BLE client is connected
     * @return true if connected, false otherwise
     */
    virtual bool isConnected() = 0;

    /**
     * @brief Send a message to the connected BLE client
     * @param msg The message to send
     * @return true on success, false on failure
     */
    virtual bool sendMessage(const Message &msg) = 0;

    /**
     * @brief Update the battery level characteristic
     * @param level Battery level percentage (0-100)
     */
    virtual void updateBatteryLevel(uint8_t level) = 0;

    /**
     * @brief Disconnect the current BLE client
     */
    virtual void disconnect() = 0;

    /**
     * @brief Set connection/disconnection callbacks
     * @param onConnect Callback when client connects
     * @param onDisconnect Callback when client disconnects
     */
    virtual void setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)()) = 0;

    /**
     * @brief Set message received callback
     * @param onMessage Callback when message received from BLE client
     */
    virtual void setMessageCallback(void (*onMessage)(const Message &)) = 0;
};

#endif // IBLE_PORT_H
