#include "BLEManager.h"
#include <string.h>

static const char *TAG_BLE = "BLE";

// Server callbacks implementation
void MyServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
{
    ESP_LOGI(TAG_BLE, "BLE client connected: %s", connInfo.getAddress().toString().c_str());
    ESP_LOGI(TAG_BLE, " (conn=%d, mtu=%d)", connInfo.getConnHandle(), connInfo.getMTU());

    bleManager->onConnected(connInfo.getConnHandle());

    // Stop advertising when connected
    NimBLEDevice::getAdvertising()->stop();
    ESP_LOGI(TAG_BLE, "BLE connected - advertising stopped");
}

void MyServerCallbacks::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
{
    ESP_LOGI(TAG_BLE, "BLE client disconnected, reason: %d", reason);
    bleManager->onDisconnected(connInfo.getConnHandle());
}

// Characteristic callbacks implementation
void MyCharacteristicCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
{
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
        ESP_LOGI(TAG_BLE, "BLE write received (%d bytes)", value.length());
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
    ESP_LOGI(TAG_BLE, "Initializing BLE");

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

    ESP_LOGI(TAG_BLE, "BLE service created");

    return true;
}

void BLEManager::startAdvertising()
{
    ESP_LOGI(TAG_BLE, "Starting BLE advertising");
    NimBLEDevice::startAdvertising();
}

bool BLEManager::sendMessage(const Message &msg)
{
    if (!isConnected())
    {
        ESP_LOGW(TAG_BLE, "Cannot send message: BLE not connected");
        return false;
    }

    uint8_t buf[64];
    int len = msg.serialize(buf, sizeof(buf));

    if (len < 0)
    {
        ESP_LOGE(TAG_BLE, "Failed to serialize message for BLE");
        return false;
    }

    pTxCharacteristic->setValue(buf, len);

    // Check if notify() succeeds - it returns false if client hasn't enabled notifications
    // or if the notification queue is full
    if (!pTxCharacteristic->notify())
    {
        ESP_LOGW(TAG_BLE, "BLE notify failed - client may not be subscribed or queue full");
        return false;
    }

    ESP_LOGI(TAG_BLE, "BLE notify sent (%d bytes)", len);
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
        ESP_LOGI(TAG_BLE, "BLE advertising manually stopped");
    }
}

void BLEManager::disconnect()
{
    if (!isConnected())
    {
        ESP_LOGW(TAG_BLE, "Disconnect requested but no BLE client is connected");
        return;
    }

    ESP_LOGI(TAG_BLE, "Disconnecting BLE client...");

    if (pServer)
    {
        if (currentConnHandle != kInvalidConnHandle)
        {
            pServer->disconnect(currentConnHandle);
        }
        else
        {
            ESP_LOGW(TAG_BLE, "Warning: No active connection handle tracked; disconnect request skipped");
        }
    }
    else
    {
        ESP_LOGW(TAG_BLE, "Warning: BLE server not initialized; cannot issue disconnect");
    }
}

void BLEManager::onMessageReceived(const uint8_t *data, size_t length)
{
    ESP_LOGI(TAG_BLE, "Parsing BLE message, length: %d", length);

    Message msg;
    if (msg.deserialize(data, length))
    {
        ESP_LOGI(TAG_BLE, "Deserialized message type: %d", (int)msg.type);
        // Send to queue instead of storing internally
        if (xQueueSend(bleToLoraQueue, &msg, 0) != pdTRUE)
        {
            ESP_LOGW(TAG_BLE, "Warning: BLE to LoRa queue full, message dropped");
        }
        else
        {
            ESP_LOGI(TAG_BLE, "Message forwarded from BLE to LoRa queue");
        }
    }
    else
    {
        ESP_LOGE(TAG_BLE, "Failed to deserialize message from BLE");
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
