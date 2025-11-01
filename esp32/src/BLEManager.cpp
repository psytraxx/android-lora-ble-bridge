#include "BLEManager.h"

// Server callbacks implementation
void MyServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
{
    Serial.print("BLE client connected: ");
    Serial.print(connInfo.getAddress().toString().c_str());
    Serial.print(" (conn=");
    Serial.print(connInfo.getConnHandle());
    Serial.print(", mtu=");
    Serial.print(connInfo.getMTU());
    Serial.println(")");

    bleManager->onConnected();

    // Stop advertising when connected
    NimBLEDevice::getAdvertising()->stop();
    Serial.println("BLE connected - advertising stopped");
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
        Serial.print("BLE write received (");
        Serial.print(value.length());
        Serial.println(" bytes)");
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
      rxCallbacks(nullptr),
      activityCallback(nullptr)
{
}

bool BLEManager::setup(const char *deviceName)
{
    Serial.println("Initializing BLE");

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

    // Set advertising parameters tuned for battery life.
    // Larger interval values reduce radio duty cycle and save power.
    // Use 1-2s intervals for polite battery usage while remaining discoverable.
    pAdvertising->setMinInterval(1000); // ~1000ms
    pAdvertising->setMaxInterval(2000); // ~2000ms

    // Add device name to advertising data for easier identification
    pAdvertising->setName(deviceName);

    // Lower TX power to save energy; adjust as needed for your range requirements.
    // ESP_PWR_LVL_P3 is about +3dBm which is a good trade-off for many use cases.
    NimBLEDevice::setPower(ESP_PWR_LVL_P3); // ~+3dBm

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
    Serial.print("BLE notify sent (");
    Serial.print(len);
    Serial.println(" bytes)");
    return true;
}

void BLEManager::process()
{
    // Stateless connection detection: compare current server connection count
    static bool prevConnected = false;
    bool curConnected = isConnected();

    if (!curConnected && prevConnected)
    {
        // Transition: was connected, now disconnected
        delay(300); // brief pause for the BLE stack
        startAdvertising();
        Serial.println("BLE disconnected - restarting advertising");
    }

    if (curConnected && !prevConnected)
    {
        // Transition: newly connected
        Serial.println("BLE connected");
    }

    prevConnected = curConnected;
}

bool BLEManager::isConnected() const
{
    // Query NimBLE server for active connections
    NimBLEServer *srv = NimBLEDevice::getServer();
    if (!srv)
        return false;
#ifdef ARDUINO_ARCH_ESP32
    // NimBLEServer provides getConnectedCount()
    return srv->getConnectedCount() > 0;
#else
    // Fallback: assume false on other platforms
    return false;
#endif
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

void BLEManager::updateActivity()
{
    lastActivityTime = millis();
}

void BLEManager::disconnect()
{
    if (isConnected())
    {
        Serial.println("Disconnecting BLE client...");
        NimBLEDevice::stopAdvertising();
    }

    if (pAdvertising)
    {
        pAdvertising->stop();
        Serial.println("BLE advertising stopped");
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
    // Stateless: just notify activity
    if (activityCallback)
    {
        activityCallback();
    }
}

void BLEManager::onDisconnected()
{
    // Stateless: nothing to track here
}
