#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "Protocol.h"

// Service and Characteristic UUIDs
#define SERVICE_UUID "00001234-0000-1000-8000-00805f9b34fb"
#define TX_CHARACTERISTIC_UUID "00005678-0000-1000-8000-00805f9b34fb"
#define RX_CHARACTERISTIC_UUID "00005679-0000-1000-8000-00805f9b34fb"

class BLEManager;

// Callback for BLE connection events
class MyServerCallbacks : public NimBLEServerCallbacks
{
public:
    MyServerCallbacks(BLEManager *manager) : bleManager(manager) {}
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo);
    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason);

private:
    BLEManager *bleManager;
};

// Callback for RX characteristic writes
class MyCharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
public:
    MyCharacteristicCallbacks(BLEManager *manager) : bleManager(manager) {}
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo);

private:
    BLEManager *bleManager;
};

class BLEManager
{
public:
    BLEManager();

    /// Initialize BLE with device name
    bool setup(const char *deviceName = "ESP32-LoRa");

    /// Start BLE advertising
    void startAdvertising();

    /// Check if a BLE client is connected
    bool isConnected() const { return deviceConnected; }

    /// Send a message to the connected BLE client via notification
    bool sendMessage(const Message &msg);

    /// Check if there's a message received from BLE
    bool hasMessage() const { return messageAvailable; }

    /// Get the received message (clears the internal buffer)
    Message getMessage();

    /// Process BLE events (call in main loop)
    void process();

    /// Called when RX characteristic is written
    void onMessageReceived(const uint8_t *data, size_t length);

    /// Connection state callbacks
    void onConnected();
    void onDisconnected();

private:
    NimBLEServer *pServer;
    NimBLECharacteristic *pTxCharacteristic;
    NimBLECharacteristic *pRxCharacteristic;
    NimBLEAdvertising *pAdvertising;

    bool deviceConnected;
    bool oldDeviceConnected;
    bool messageAvailable;

    Message receivedMessage;

    MyServerCallbacks *serverCallbacks;
    MyCharacteristicCallbacks *rxCallbacks;
};

#endif // BLE_MANAGER_H
