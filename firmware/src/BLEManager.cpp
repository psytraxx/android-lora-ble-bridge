#include "BLEManager.h"
#include <string.h>

// Server callbacks implementation
void MyServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
{
    Serial.printf("BLE client connected: %s\n", connInfo.getAddress().toString().c_str());
    Serial.printf(" (conn=%d, mtu=%d)\n", connInfo.getConnHandle(), connInfo.getMTU());

    bleManager->onConnected(connInfo.getConnHandle());

    // Stop advertising when connected
    NimBLEDevice::getAdvertising()->stop();
    Serial.println("BLE connected - advertising stopped");
}

void MyServerCallbacks::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
{
    Serial.printf("BLE client disconnected, reason: %d\n", reason);
    bleManager->onDisconnected(connInfo.getConnHandle());
}

// Characteristic callbacks implementation
void MyCharacteristicCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
{
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
        Serial.printf("BLE write received (%d bytes)\n", value.length());
        bleManager->onMessageReceived((const uint8_t *)value.data(), value.length());
    }
}

// BLEManager implementation
BLEManager::BLEManager(QueueHandle_t queue)
    : pServer(nullptr),
      pTxCharacteristic(nullptr),
      pRxCharacteristic(nullptr),
      pAdvertising(nullptr),
      bleToLoraQueue(queue),
      deviceNameStr(""),
      serverCallbacks(nullptr),
      rxCallbacks(nullptr)
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
    pTxCharacteristic->notify();
    Serial.printf("BLE notify sent (%d bytes)\n", len);
    return true;
}

void BLEManager::process()
{
    // Minimal processing - let NimBLE handle internal tasks if needed
    // Connection state management moved to ApplicationController
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
// Note: Removed BLE advertising inactivity timeout
// Requirement: Always able to receive LoRa messages and deliver to Android
// Therefore, advertising must never stop automatically

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
        // Send to queue instead of storing internally
        if (xQueueSend(bleToLoraQueue, &msg, 0) != pdTRUE)
        {
            Serial.println("Warning: BLE to LoRa queue full, message dropped");
        }
        else
        {
            Serial.println("Message forwarded from BLE to LoRa queue");
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
}

void BLEManager::onDisconnected(uint16_t connHandle)
{
    if (connHandle == currentConnHandle)
    {
        currentConnHandle = kInvalidConnHandle;
    }
}
