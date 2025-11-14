#include "BLEManager.h"
#include "LoraTask.h"
#include "BleTask.h"
#include "PowerManager.h"
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

// RX Characteristic callbacks implementation
void MyCharacteristicCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
{
    auto value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
        ESP_LOGI(TAG_BLE, "BLE write received (%d bytes)", value.length());
        bleManager->onMessageReceived((const uint8_t *)value.data(), value.length());
    }
}

// TX Characteristic callbacks implementation
void TxCharacteristicCallbacks::onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue)
{
    if (subValue & 0x0001)
    {
        ESP_LOGI(TAG_BLE, "Client enabled notifications - Android ready to receive!");
        bleManager->onNotificationsEnabled(true);
    }
    else
    {
        ESP_LOGI(TAG_BLE, "Client disabled notifications");
        bleManager->onNotificationsEnabled(false);
    }
}

// Battery Characteristic callbacks implementation
void BatteryCharacteristicCallbacks::onRead(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
{
    auto batteryLevel = PowerManager::readBatteryLevel();
    pCharacteristic->setValue(&batteryLevel, 1);
    ESP_LOGI(TAG_BLE, "Battery level read: %d%%", batteryLevel);
}

// BLEManager implementation
BLEManager::BLEManager(QueueHandle_t queue)
    : pServer(nullptr),
      pTxCharacteristic(nullptr),
      pRxCharacteristic(nullptr),
      pAdvertising(nullptr),
      bleToLoraQueue(queue),
      deviceNameStr("")
{
    // Smart pointers initialized to nullptr by default
}

BLEManager::~BLEManager()
{
    ESP_LOGW(TAG_BLE, "BLEManager destructor called - this should NEVER happen in normal operation!");
    ESP_LOGW(TAG_BLE, "If you see this message, there's a bug in your application lifecycle management.");

    // Clean up dynamically allocated callback objects
    // Note: We do NOT clean up NimBLE objects (pServer, pTxCharacteristic, etc.)
    // because they are owned by the NimBLE singleton and cannot be safely destroyed
    // without calling NimBLEDevice::deinit(), which would break the entire BLE stack

    // Smart pointers automatically cleaned up (serverCallbacks, rxCallbacks,
    // txCallbacks, batteryCallbacks) - no manual delete needed

    ESP_LOGW(TAG_BLE, "BLEManager destructor complete - device should be reset!");
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
    serverCallbacks = std::make_unique<MyServerCallbacks>(this);
    pServer->setCallbacks(serverCallbacks.get());

    // Create the BLE Service
    NimBLEService *pService = pServer->createService(BLEConstants::SERVICE_UUID);

    // Create the TX Characteristic (for sending data to phone)
    pTxCharacteristic = pService->createCharacteristic(
        BLEConstants::TX_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::NOTIFY);
    txCallbacks = std::make_unique<TxCharacteristicCallbacks>(this);
    pTxCharacteristic->setCallbacks(txCallbacks.get());

    // Create the RX Characteristic (for receiving data from phone)
    pRxCharacteristic = pService->createCharacteristic(
        BLEConstants::RX_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::WRITE_NR | // Write without response for faster writes
            NIMBLE_PROPERTY::NOTIFY);
    rxCallbacks = std::make_unique<MyCharacteristicCallbacks>(this);
    pRxCharacteristic->setCallbacks(rxCallbacks.get());

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
    batteryCallbacks = std::make_unique<BatteryCharacteristicCallbacks>();
    pBatteryCharacteristic->setCallbacks(batteryCallbacks.get());

    // Set initial battery level
    auto initialBattery = PowerManager::readBatteryLevel();
    pBatteryCharacteristic->setValue(&initialBattery, 1);

    // Start the battery service
    pBatteryService->start();

    ESP_LOGI(TAG_BLE, "Battery service created (initial level: %d%%)", initialBattery);

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
    auto srv = NimBLEDevice::getServer();
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

            // Notify LoRa task that a message is ready
            LoraTask::notifyMessageQueued();
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
    ESP_LOGI(TAG_BLE, "Notifications state changed: %s", enabled ? "ENABLED" : "DISABLED");

    // Notify BLE task to immediately forward buffered messages
    if (enabled)
    {
        BleTask::notifyMessageReceived();
    }
}

void BLEManager::setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)())
{
    connectCallback = onConnect;
    disconnectCallback = onDisconnect;
    ESP_LOGI(TAG_BLE, "Connection callbacks registered");
}

void BLEManager::updateBatteryLevel()
{
    // Only update if we have a connected client with a battery characteristic
    if (!isConnected() || !pBatteryCharacteristic)
    {
        return;
    }

    // Read current battery level
    auto batteryLevel = PowerManager::readBatteryLevel();

    // Update the characteristic value
    pBatteryCharacteristic->setValue(&batteryLevel, 1);

    // Notify connected clients (only if they've enabled notifications via CCCD)
    pBatteryCharacteristic->notify();

    ESP_LOGI(TAG_BLE, "Battery level updated and notified: %d%%", batteryLevel);
}
