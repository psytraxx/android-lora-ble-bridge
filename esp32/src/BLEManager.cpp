#include "BLEManager.h"

// Server callbacks implementation
void MyServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
{
    Serial.print("BLE client connected: ");
    Serial.println(connInfo.getAddress().toString().c_str());
    Serial.print("Connection ID: ");
    Serial.println(connInfo.getConnHandle());
    Serial.print("MTU: ");
    Serial.println(connInfo.getMTU());

    bleManager->onConnected();

    // Stop advertising when connected
    NimBLEDevice::getAdvertising()->stop();
    Serial.println("Stopped advertising (connected)");
}

void MyServerCallbacks::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
{
    Serial.print("BLE client disconnected, reason: ");
    Serial.println(reason);
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
        Serial.print(" bytes from client: ");
        Serial.println(connInfo.getAddress().toString().c_str());

        // Add hex dump for debugging
        Serial.print("Data (hex): ");
        for (size_t i = 0; i < value.length(); i++)
        {
            Serial.printf("%02X ", (uint8_t)value[i]);
        }
        Serial.println();

        bleManager->onMessageReceived((const uint8_t *)value.data(), value.length());
    }
}

// BLEManager implementation
BLEManager::BLEManager(QueueHandle_t queue)
    : pServer(nullptr),
      pTxCharacteristic(nullptr),
      pRxCharacteristic(nullptr),
      pAdvertising(nullptr),
      deviceConnected(false),
      oldDeviceConnected(false),
      bleToLoraQueue(queue),
      deviceNameStr(""),
      serverCallbacks(nullptr),
      rxCallbacks(nullptr),
      activityCallback(nullptr)
{
}

bool BLEManager::setup(const char *deviceName)
{
    Serial.println("Initializing BLE...");

    // Store device name for debugging
    deviceNameStr = String(deviceName);

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
            NIMBLE_PROPERTY::WRITE_NR | // Write without response for faster writes
            NIMBLE_PROPERTY::NOTIFY);
    rxCallbacks = new MyCharacteristicCallbacks(this);
    pRxCharacteristic->setCallbacks(rxCallbacks);

    // Start the service
    pService->start();

    // Get advertising instance and configure for better discoverability
    pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->enableScanResponse(true);

    // Set advertising parameters for power saving while maintaining discoverability
    pAdvertising->setMinInterval(200);  // 200ms minimum (power saving)
    pAdvertising->setMaxInterval(1000); // 1 second maximum (power saving)

    // Add device name to advertising data for easier identification
    pAdvertising->setName(deviceName);

    // Set TX power to balance range and power consumption
    NimBLEDevice::setPower(ESP_PWR_LVL_P6); // +6dBm (reduced from +9dBm for power saving)

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

    // Additional debugging information
    Serial.print("Advertising with device name: ");
    Serial.println(deviceNameStr);
    Serial.print("MAC Address: ");
    Serial.println(NimBLEDevice::getAddress().toString().c_str());

    NimBLEDevice::startAdvertising();
    Serial.println("BLE advertising started, waiting for connection...");
    Serial.print("Device should now be discoverable as '");
    Serial.print(deviceNameStr);
    Serial.println("'");
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

    // Update activity callback if set
    if (activityCallback)
    {
        activityCallback();
    }

    Message msg;
    if (msg.deserialize(data, length))
    {
        Serial.print("Deserialized message type: ");
        Serial.println((int)msg.type);
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

void BLEManager::onConnected()
{
    deviceConnected = true;

    // Update activity callback if set
    if (activityCallback)
    {
        activityCallback();
    }
}

void BLEManager::onDisconnected()
{
    deviceConnected = false;
}
