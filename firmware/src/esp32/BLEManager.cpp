#include "esp32/BLEManager.h"
#include "esp32/PowerManager.h"
#include <string.h>
#include <Arduino.h>

// Server callbacks implementation
void MyServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
{
    Serial.printf("BLE client connected: %s", connInfo.getAddress().toString().c_str());
    Serial.printf(" (conn=%d, mtu=%d)", connInfo.getConnHandle(), connInfo.getMTU());

    bleManager->onConnected(connInfo.getConnHandle());

    // Request longer connection intervals for power savings
    // Intervals in 1.25ms units: 80 = 100ms, 160 = 200ms
    // Longer intervals allow more CPU sleep time between BLE events
    pServer->updateConnParams(connInfo.getConnHandle(),
                              80,   // min interval (100ms)
                              160,  // max interval (200ms)
                              0,    // latency (no slave latency)
                              400); // timeout (4000ms = 4s)
    Serial.println("Requested power-optimized connection params (100-200ms interval)");

    // Stop advertising when connected
    NimBLEDevice::getAdvertising()->stop();
    Serial.println("BLE connected - advertising stopped");
}

void MyServerCallbacks::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
{
    Serial.printf("BLE client disconnected, reason: %d", reason);
    bleManager->onDisconnected(connInfo.getConnHandle());
}

// RX Characteristic callbacks implementation
void MyCharacteristicCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
{
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
        Serial.printf("BLE write received (%d bytes)\n", value.length());
        bleManager->onMessageReceived((const uint8_t *)value.data(), value.length());
    }
}

// TX Characteristic callbacks implementation
void TxCharacteristicCallbacks::onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue)
{
    if (subValue & 0x0001)
    {
        Serial.println("Client enabled notifications - Android ready to receive!");
        bleManager->onNotificationsEnabled(true);
    }
    else
    {
        Serial.println("Client disabled notifications");
        bleManager->onNotificationsEnabled(false);
    }
}

// Battery Characteristic callbacks implementation
void BatteryCharacteristicCallbacks::onRead(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
{
    uint8_t batteryLevel = PowerManager::readBatteryLevel();
    pCharacteristic->setValue(&batteryLevel, 1);
    Serial.printf("Battery level read: %d%%\n", batteryLevel);
}

// BLEManager implementation
BLEManager::BLEManager(MessageQueue *bleToLoraQueue)
    : pServer(nullptr),
      pTxCharacteristic(nullptr),
      pRxCharacteristic(nullptr),
      pAdvertising(nullptr),
      deviceNameStr(""),
      serverCallbacks(nullptr),
      rxCallbacks(nullptr),
      bleToLoraQueue(bleToLoraQueue)
{
}

bool BLEManager::setup(const char *deviceName)
{
    Serial.println("Initializing BLE");

    // Store device name for debugging
    deviceNameStr = std::string(deviceName);

    // Create the BLE Device
    NimBLEDevice::init(deviceName);

    // Create the BLE Server
    pServer = NimBLEDevice::createServer();
    serverCallbacks = new MyServerCallbacks(this);
    pServer->setCallbacks(serverCallbacks);

    // Create the BLE Service
    NimBLEService *pService = pServer->createService(BLEConstants::SERVICE_UUID);

    // Create the TX Characteristic (for sending data to phone)
    pTxCharacteristic = pService->createCharacteristic(
        BLEConstants::TX_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::NOTIFY);
    txCallbacks = new TxCharacteristicCallbacks(this);
    pTxCharacteristic->setCallbacks(txCallbacks);

    // Create the RX Characteristic (for receiving data from phone)
    pRxCharacteristic = pService->createCharacteristic(
        BLEConstants::RX_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::WRITE_NR | // Write without response for faster writes
            NIMBLE_PROPERTY::NOTIFY);
    rxCallbacks = new MyCharacteristicCallbacks(this);
    pRxCharacteristic->setCallbacks(rxCallbacks);

    // Start the service
    pService->start();

    // Create the Battery Service (standard BLE service 0x180F)
    NimBLEService *pBatteryService = pServer->createService(BLEConstants::BATTERY_SERVICE_UUID);

    // Create the Battery Level Characteristic (standard characteristic 0x2A19)
    // BLE Battery Service standard: uint8 value (0-100%)
    pBatteryCharacteristic = pBatteryService->createCharacteristic(
        BLEConstants::BATTERY_LEVEL_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

    // Set callback to update battery level on read
    pBatteryCharacteristic->setCallbacks(new BatteryCharacteristicCallbacks());

    // Set initial battery level
    uint8_t initialBattery = PowerManager::readBatteryLevel();
    pBatteryCharacteristic->setValue(&initialBattery, 1);

    // Start the battery service
    pBatteryService->start();

    Serial.printf("Battery service created (initial level: %d%%)\n", initialBattery);

    // Get advertising instance and configure for better discoverability
    pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLEConstants::SERVICE_UUID);
    pAdvertising->enableScanResponse(true);

    // Set advertising parameters tuned for battery life
    pAdvertising->setMinInterval(BLEConstants::ADV_MIN_INTERVAL);
    pAdvertising->setMaxInterval(BLEConstants::ADV_MAX_INTERVAL);

    // Add device name to advertising data for easier identification
    pAdvertising->setName(deviceName);

    // Lower TX power to save energy
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);

    Serial.println("BLE service created");

    return true;
}

void BLEManager::startAdvertising()
{
    Serial.println("Starting BLE advertising");
    NimBLEDevice::startAdvertising();
}

bool BLEManager::sendMessage(const Message &msg)
{
    if (!isConnected())
    {
        Serial.println("Cannot send message: BLE not connected");
        return false;
    }

    uint8_t buf[64];
    int len = msg.serialize(buf, sizeof(buf));

    if (len < 0)
    {
        Serial.println("Failed to serialize message for BLE");
        return false;
    }

    pTxCharacteristic->setValue(buf, len);

    // Check if notify() succeeds - it returns false if client hasn't enabled notifications
    // or if the notification queue is full
    if (!pTxCharacteristic->notify())
    {
        Serial.println("BLE notify failed - client may not be subscribed or queue full");
        return false;
    }

    Serial.printf("BLE notify sent (%d bytes)\n", len);
    return true;
}

bool BLEManager::isConnected() const
{
    // Query NimBLE server for active connections
    NimBLEServer *srv = NimBLEDevice::getServer();
    if (!srv)
        return false;

    // NimBLEServer::getConnectedCount() works in both Arduino and ESP-IDF
    return srv->getConnectedCount() > 0;
}

void BLEManager::stopAdvertising()
{
    if (pAdvertising)
    {
        pAdvertising->stop();
        Serial.println("BLE advertising manually stopped");
    }
}

void BLEManager::disconnect()
{
    if (!isConnected())
    {
        Serial.println("Disconnect requested but no BLE client is connected");
        return;
    }

    Serial.println("Disconnecting BLE client...");

    if (pServer)
    {
        if (currentConnHandle != kInvalidConnHandle)
        {
            pServer->disconnect(currentConnHandle);
        }
        else
        {
            Serial.println("Warning: No active connection handle tracked; disconnect request skipped");
        }
    }
    else
    {
        Serial.println("Warning: BLE server not initialized; cannot issue disconnect");
    }
}

void BLEManager::onMessageReceived(const uint8_t *data, size_t length)
{
    Serial.printf("Parsing BLE message, length: %d\n", length);

    Message msg;
    if (msg.deserialize(data, length))
    {
        Serial.printf("Deserialized message type: %d\n", (int)msg.type);

        // Push message to queue for processing in main loop
        if (bleToLoraQueue != nullptr)
        {
            if (!bleToLoraQueue->push(msg))
            {
                Serial.println("BLE->LoRa queue full, message dropped");
            }
        }
        else
        {
            Serial.println("Warning: No message queue configured, message dropped");
        }
    }
    else
    {
        Serial.println("Failed to deserialize message from BLE");
    }
}

void BLEManager::onConnected(uint16_t connHandle)
{
    currentConnHandle = connHandle;
    notificationsEnabled = false; // Reset notification flag on new connection

    // Call connection callback if registered
    if (connectCallback)
    {
        connectCallback();
    }
}

void BLEManager::onDisconnected(uint16_t connHandle)
{
    if (connHandle == currentConnHandle)
    {
        currentConnHandle = kInvalidConnHandle;
        notificationsEnabled = false; // Clear notification flag on disconnect

        // Call disconnection callback if registered
        if (disconnectCallback)
        {
            disconnectCallback();
        }
    }
}

void BLEManager::onNotificationsEnabled(bool enabled)
{
    notificationsEnabled = enabled;
    Serial.printf("Notifications state changed: %s\n", enabled ? "ENABLED" : "DISABLED");
}

void BLEManager::setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)())
{
    connectCallback = onConnect;
    disconnectCallback = onDisconnect;
    Serial.println("Connection callbacks registered");
}

void BLEManager::updateBatteryLevel(uint8_t level)
{
    if (pBatteryCharacteristic)
    {
        pBatteryCharacteristic->setValue(&level, 1);
        if (isConnected())
        {
            pBatteryCharacteristic->notify();
        }
        Serial.printf("Battery level updated: %d%%\n", level);
    }
}
