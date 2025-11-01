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

    bleManager->onConnected(connInfo.getConnHandle());

    // Stop advertising when connected
    NimBLEDevice::getAdvertising()->stop();
    Serial.println("BLE connected - advertising stopped");
}

void MyServerCallbacks::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
{
    Serial.print("BLE client disconnected, reason: ");
    Serial.println(reason);
    bleManager->onDisconnected(connInfo.getConnHandle());
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
    // Note: Values are in 0.625ms units per BLE spec
    const int BLE_ADV_MIN_INTERVAL = 1600; // 1600 * 0.625ms = 1000ms (1 second)
    const int BLE_ADV_MAX_INTERVAL = 3200; // 3200 * 0.625ms = 2000ms (2 seconds)
    pAdvertising->setMinInterval(BLE_ADV_MIN_INTERVAL);
    pAdvertising->setMaxInterval(BLE_ADV_MAX_INTERVAL);

    // Add device name to advertising data for easier identification
    pAdvertising->setName(deviceName);

    // Lower TX power to save energy; adjust as needed for your range requirements.
    // ESP_PWR_LVL_P3 = +3dBm provides good balance of range (~10m) vs power consumption
    // Options: P9(+9dBm max range), P6(+6dBm), P3(+3dBm balanced), P0(0dBm), N3(-3dBm min power)
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
        const int BLE_DISCONNECT_SETTLE_MS = 300; // 300ms for NimBLE stack to clean up connection
        delay(BLE_DISCONNECT_SETTLE_MS);
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
    // Notify PowerController of activity (e.g., LoRa packet received)
    if (activityCallback)
    {
        activityCallback();
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

void BLEManager::onConnected(uint16_t connHandle)
{
    currentConnHandle = connHandle;

    // Stateless: just notify activity
    if (activityCallback)
    {
        activityCallback();
    }
}

void BLEManager::onDisconnected(uint16_t connHandle)
{
    if (connHandle == currentConnHandle)
    {
        currentConnHandle = kInvalidConnHandle;
    }
}
