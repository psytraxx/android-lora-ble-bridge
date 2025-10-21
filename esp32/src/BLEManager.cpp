#include "BLEManager.h"

// Server callbacks implementation
void MyServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
{
    Serial.println("BLE client connected");
    bleManager->onConnected();
}

void MyServerCallbacks::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
{
    Serial.println("BLE client disconnected");
    bleManager->onDisconnected();
}

// Characteristic callbacks implementation
void MyCharacteristicCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
{
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
        Serial.print("Received BLE write on RX characteristic, ");
        Serial.print(value.length());
        Serial.println(" bytes");
        bleManager->onMessageReceived((const uint8_t *)value.data(), value.length());
    }
}

// BLEManager implementation
BLEManager::BLEManager()
    : pServer(nullptr),
      pTxCharacteristic(nullptr),
      pRxCharacteristic(nullptr),
      pAdvertising(nullptr),
      deviceConnected(false),
      oldDeviceConnected(false),
      messageAvailable(false),
      serverCallbacks(nullptr),
      rxCallbacks(nullptr)
{
}

bool BLEManager::setup(const char *deviceName)
{
    Serial.println("Initializing BLE...");

    // Create the BLE Device
    NimBLEDevice::init(deviceName);

    // Create the BLE Server
    pServer = NimBLEDevice::createServer();
    serverCallbacks = new MyServerCallbacks(this);
    pServer->setCallbacks(serverCallbacks);

    // Create the BLE Service
    NimBLEService *pService = pServer->createService(SERVICE_UUID);

    // Create the TX Characteristic (for sending data to phone)
    pTxCharacteristic = pService->createCharacteristic(
        TX_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::NOTIFY);

    // Create the RX Characteristic (for receiving data from phone)
    pRxCharacteristic = pService->createCharacteristic(
        RX_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::NOTIFY);
    rxCallbacks = new MyCharacteristicCallbacks(this);
    pRxCharacteristic->setCallbacks(rxCallbacks);

    // Start the service
    pService->start();

    // Get advertising instance
    pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->enableScanResponse(true);

    Serial.println("BLE service created");
    Serial.print("Device name: ");
    Serial.println(deviceName);
    Serial.print("Service UUID: ");
    Serial.println(SERVICE_UUID);
    Serial.print("TX Characteristic UUID: ");
    Serial.println(TX_CHARACTERISTIC_UUID);
    Serial.print("RX Characteristic UUID: ");
    Serial.println(RX_CHARACTERISTIC_UUID);

    return true;
}

void BLEManager::startAdvertising()
{
    Serial.println("Starting BLE advertising...");
    NimBLEDevice::startAdvertising();
    Serial.println("BLE advertising started, waiting for connection...");
}

bool BLEManager::sendMessage(const Message &msg)
{
    if (!deviceConnected)
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

    Serial.print("Sending ");
    Serial.print(len);
    Serial.println(" bytes via BLE notification");

    pTxCharacteristic->setValue(buf, len);
    pTxCharacteristic->notify();

    Serial.println("Message forwarded from LoRa to BLE via notification");
    return true;
}

Message BLEManager::getMessage()
{
    messageAvailable = false;
    return receivedMessage;
}

void BLEManager::process()
{
    // Handle disconnection/reconnection
    if (!deviceConnected && oldDeviceConnected)
    {
        delay(500); // give the bluetooth stack the chance to get things ready
        startAdvertising();
        Serial.println("Restarted advertising after disconnect");
        oldDeviceConnected = deviceConnected;
    }

    // Handle connection
    if (deviceConnected && !oldDeviceConnected)
    {
        oldDeviceConnected = deviceConnected;
        Serial.println("Connection state updated");
    }
}

void BLEManager::onMessageReceived(const uint8_t *data, size_t length)
{
    Serial.print("Parsing BLE message, length: ");
    Serial.println(length);

    if (receivedMessage.deserialize(data, length))
    {
        Serial.print("Deserialized message type: ");
        Serial.println((int)receivedMessage.type);
        messageAvailable = true;
        Serial.println("Message forwarded from BLE to LoRa queue");
    }
    else
    {
        Serial.println("Failed to deserialize message from BLE");
    }
}

void BLEManager::onConnected()
{
    deviceConnected = true;
}

void BLEManager::onDisconnected()
{
    deviceConnected = false;
}
