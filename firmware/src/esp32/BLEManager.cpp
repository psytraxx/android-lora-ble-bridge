#include "esp32/BLEManager.h"
#include "esp32/PowerManager.h"
#include "common/Logging.h"
#include <string.h>
#include <Arduino.h>

static const char *TAG = "BLE";

// Server callbacks implementation
void MyServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
{
    LOG_I(TAG, "Client connected: %s (conn=%d, mtu=%d)",
          connInfo.getAddress().toString().c_str(),
          connInfo.getConnHandle(),
          connInfo.getMTU());

    bleManager->onConnected(connInfo.getConnHandle());

    // Request longer connection intervals for power savings
    pServer->updateConnParams(connInfo.getConnHandle(),
                              80,   // min interval (100ms)
                              160,  // max interval (200ms)
                              0,    // latency (no slave latency)
                              400); // timeout (4000ms = 4s)
    LOG_I(TAG, "Requested power-optimized connection params (100-200ms interval)");

    // Stop advertising when connected
    NimBLEDevice::getAdvertising()->stop();
    LOG_I(TAG, "Advertising stopped");
}

void MyServerCallbacks::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
{
    LOG_I(TAG, "Client disconnected, reason: %d", reason);
    bleManager->onDisconnected(connInfo.getConnHandle());
}

// RX Characteristic callbacks implementation
void MyCharacteristicCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
{
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
        LOG_D(TAG, "Write received (%d bytes)", value.length());
        bleManager->onMessageReceived((const uint8_t *)value.data(), value.length());
    }
}

// TX Characteristic callbacks implementation
void TxCharacteristicCallbacks::onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue)
{
    if (subValue & 0x0001)
    {
        LOG_I(TAG, "Client enabled notifications - Android ready to receive!");
        bleManager->onNotificationsEnabled(true);
    }
    else
    {
        LOG_I(TAG, "Client disabled notifications");
        bleManager->onNotificationsEnabled(false);
    }
}

// Info Characteristic callbacks implementation
void InfoCharacteristicCallbacks::onRead()
{
    bleManager->updateDeviceInfo();
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
    LOG_I(TAG, "Initializing BLE");

    // Store device name for debugging
    deviceNameStr = std::string(deviceName);

    // Create the BLE Device
    NimBLEDevice::init(deviceName);

    // Log MAC address for debugging
    NimBLEAddress address = NimBLEDevice::getAddress();
    LOG_I(TAG, "BLE MAC Address: %s", address.toString().c_str());

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

    // Create the Device Info Characteristic (read-only, 16 bytes)
    pInfoCharacteristic = pService->createCharacteristic(
        BLEConstants::INFO_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ);
    pInfoCharacteristic->setCallbacks(new InfoCharacteristicCallbacks(this));

    // Start the service
    pService->start();

    LOG_I(TAG, "Device info characteristic created");

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
    NimBLEDevice::setPower(BLEConstants::TX_POWER_LEVEL);

    LOG_I(TAG, "Service created successfully");

    return true;
}

void BLEManager::startAdvertising()
{
    LOG_I(TAG, "Starting advertising");
    NimBLEDevice::startAdvertising();
}

bool BLEManager::sendMessage(const Message &msg)
{
    if (!isConnected())
    {
        LOG_W(TAG, "Cannot send message: not connected");
        return false;
    }

    if (!areNotificationsEnabled())
    {
        LOG_W(TAG, "Cannot send message: notifications not enabled by client");
        return false;
    }

    uint8_t buf[BufferConstants::MAX_PROTOCOL_MESSAGE];
    int len = msg.serialize(buf, sizeof(buf));

    if (len < 0)
    {
        LOG_E(TAG, "Failed to serialize message");
        return false;
    }

    pTxCharacteristic->setValue(buf, len);

    bool success = pTxCharacteristic->notify();
    if (success)
    {
        int seq = (msg.type == MessageType::Ack) ? msg.ackData.seq : msg.textData.seq;
        LOG_D(TAG, "Sent message, type: %d, seq: %d, size: %d", (int)msg.type, seq, len);
    }
    else
    {
        LOG_E(TAG, "Failed to send notification");
    }
    return success;
}

bool BLEManager::isConnected() const
{
    NimBLEServer *srv = NimBLEDevice::getServer();
    if (!srv)
        return false;

    return srv->getConnectedCount() > 0;
}

void BLEManager::stopAdvertising()
{
    if (pAdvertising)
    {
        pAdvertising->stop();
        LOG_I(TAG, "Advertising stopped");
    }
}

void BLEManager::disconnect()
{
    if (!isConnected())
    {
        LOG_W(TAG, "Disconnect requested but no client is connected");
        return;
    }

    LOG_I(TAG, "Disconnecting client...");

    if (pServer)
    {
        if (currentConnHandle != kInvalidConnHandle)
        {
            pServer->disconnect(currentConnHandle);
        }
        else
        {
            LOG_W(TAG, "No active connection handle tracked; disconnect request skipped");
        }
    }
    else
    {
        LOG_W(TAG, "Server not initialized; cannot issue disconnect");
    }
}

void BLEManager::onMessageReceived(const uint8_t *data, size_t length)
{
    LOG_D(TAG, "Parsing message, length: %d", length);

    Message msg;
    if (msg.deserialize(data, length))
    {
        LOG_D(TAG, "Deserialized message type: %d", (int)msg.type);

        if (bleToLoraQueue != nullptr)
        {
            if (!bleToLoraQueue->push(msg))
            {
                LOG_E(TAG, "Queue full, message dropped");
            }
        }
        else
        {
            LOG_W(TAG, "No message queue configured, message dropped");
        }
    }
    else
    {
        LOG_E(TAG, "Failed to deserialize message");
    }
}

void BLEManager::onConnected(uint16_t connHandle)
{
    currentConnHandle = connHandle;
    notificationsEnabled = false;

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
        notificationsEnabled = false;

        if (disconnectCallback)
        {
            disconnectCallback();
        }
    }
}

void BLEManager::onNotificationsEnabled(bool enabled)
{
    notificationsEnabled = enabled;
    LOG_I(TAG, "Notifications state changed: %s", enabled ? "ENABLED" : "DISABLED");
}

void BLEManager::setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)())
{
    connectCallback = onConnect;
    disconnectCallback = onDisconnect;
    LOG_D(TAG, "Connection callbacks registered");
}

DeviceInfoData BLEManager::getDeviceInfo() const
{
    if (infoProvider)
    {
        return infoProvider();
    }
    // Return zeroed data if no provider registered
    return DeviceInfoData{};
}

void BLEManager::updateDeviceInfo()
{
    if (!pInfoCharacteristic)
    {
        return;
    }

    DeviceInfoData info = getDeviceInfo();
    uint8_t buf[16];
    info.serialize(buf);
    pInfoCharacteristic->setValue(buf, sizeof(buf));
    LOG_D(TAG, "Device info updated: battery=%d%%, rssi=%d, snr=%d", info.batteryLevel, info.rssi, info.snrX100);
}
