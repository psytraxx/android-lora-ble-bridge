#include "nrf52/BLEManager.h"

// Static instance for callbacks
BLEManager *BLEManager::instance = nullptr;

BLEManager::BLEManager(MessageQueue *bleToLoraQueue)
    : loraService(BLEConstants::SERVICE_UUID),
      txCharacteristic(BLEConstants::TX_CHARACTERISTIC_UUID),
      rxCharacteristic(BLEConstants::RX_CHARACTERISTIC_UUID),
      bleToLoraQueue(bleToLoraQueue)
{
    instance = this;
}

bool BLEManager::setup(const char *deviceName)
{
    deviceNameStr = String(deviceName);
    Serial.print("Initializing BLE: ");
    Serial.println(deviceName);

    // Initialize Bluefruit
    Bluefruit.begin();
    Bluefruit.setTxPower(BLEConstants::TX_POWER_DBM);
    Bluefruit.setName(deviceName);

    // Set connection callbacks
    Bluefruit.Periph.setConnectCallback(BLEManager::connectCallback);
    Bluefruit.Periph.setDisconnectCallback(BLEManager::disconnectCallback);

    // Configure Device Information Service
    bledis.setManufacturer("Adafruit Industries");
    bledis.setModel("nRF52840 LoRa");
    bledis.begin();

    // Configure Battery Service
    blebas.begin();
    blebas.write(lastBatteryLevel);

    // Configure custom LoRa Service
    loraService.begin();

    // Configure TX Characteristic (ESP32 -> Android)
    txCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    txCharacteristic.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    txCharacteristic.setFixedLen(BufferConstants::MAX_PROTOCOL_MESSAGE);
    txCharacteristic.setCccdWriteCallback(BLEManager::cccdCallback);
    txCharacteristic.begin();

    // Configure RX Characteristic (Android -> ESP32)
    rxCharacteristic.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
    rxCharacteristic.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
    rxCharacteristic.setFixedLen(BufferConstants::MAX_PROTOCOL_MESSAGE);
    rxCharacteristic.setWriteCallback(BLEManager::rxWriteCallback);
    rxCharacteristic.begin();

    Serial.println("BLE initialized successfully");
    return true;
}

void BLEManager::startAdvertising()
{
    Serial.println("Starting BLE advertising");

    // Advertising packet
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(loraService);
    Bluefruit.Advertising.addName();

    // Secondary Scan Response packet (optional)
    Bluefruit.ScanResponse.addName();

    // Start advertising
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(BLEConstants::ADV_MIN_INTERVAL, BLEConstants::ADV_MAX_INTERVAL);
    Bluefruit.Advertising.setFastTimeout(30); // Fast advertising for 30 seconds
    Bluefruit.Advertising.start(0);           // 0 = Don't stop advertising after n seconds
}

void BLEManager::stopAdvertising()
{
    Serial.println("Stopping BLE advertising");
    Bluefruit.Advertising.stop();
}

void BLEManager::disconnect()
{
    if (Bluefruit.connected())
    {
        Serial.println("Disconnecting BLE client");
        Bluefruit.disconnect(Bluefruit.connHandle());
    }
}

bool BLEManager::isConnected() const
{
    return isConnectedFlag;
}

bool BLEManager::sendMessage(const Message &msg)
{
    if (!isConnected() || !notificationsEnabled)
    {
        Serial.println("Cannot send: not connected or notifications disabled");
        return false;
    }

    // Serialize message
    uint8_t buffer[BufferConstants::MAX_PROTOCOL_MESSAGE];
    int length = msg.serialize(buffer, sizeof(buffer));

    if (length <= 0)
    {
        Serial.println("Failed to serialize message");
        return false;
    }

    // Send notification
    bool success = txCharacteristic.notify(buffer, length);

    if (success)
    {
        Serial.print("Sent BLE message, type: ");
        Serial.println((int)msg.type);
    }
    else
    {
        Serial.println("Failed to send BLE notification");
    }

    return success;
}

void BLEManager::setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)())
{
    connectCallback_user = onConnect;
    disconnectCallback_user = onDisconnect;
}

void BLEManager::updateBatteryLevel(uint8_t level)
{
    if (level != lastBatteryLevel)
    {
        lastBatteryLevel = level;
        blebas.write(level);
    }
}

// Static callback implementations
void BLEManager::connectCallback(uint16_t conn_handle)
{
    if (instance)
    {
        BLEConnection *connection = Bluefruit.Connection(conn_handle);
        char central_name[32] = {0};
        connection->getPeerName(central_name, sizeof(central_name));

        Serial.print("BLE connected: ");
        Serial.println(central_name);

        instance->isConnectedFlag = true;

        if (instance->connectCallback_user)
        {
            instance->connectCallback_user();
        }
    }
}

void BLEManager::disconnectCallback(uint16_t conn_handle, uint8_t reason)
{
    (void)conn_handle;
    (void)reason;

    if (instance)
    {
        Serial.println("BLE disconnected");

        instance->isConnectedFlag = false;
        instance->notificationsEnabled = false;

        if (instance->disconnectCallback_user)
        {
            instance->disconnectCallback_user();
        }
    }
}

void BLEManager::rxWriteCallback(uint16_t conn_hdl, BLECharacteristic *chr, uint8_t *data, uint16_t len)
{
    (void)conn_hdl;
    (void)chr;

    if (instance)
    {
        instance->handleRxWrite(data, len);
    }
}

void BLEManager::cccdCallback(uint16_t conn_hdl, BLECharacteristic *chr, uint16_t value)
{
    (void)conn_hdl;
    (void)chr;

    if (instance)
    {
        instance->handleCccdWrite(value);
    }
}

// Internal handlers
void BLEManager::handleRxWrite(uint8_t *data, uint16_t len)
{
    Serial.print("BLE RX received ");
    Serial.print(len);
    Serial.println(" bytes");

    if (len == 0 || len > BufferConstants::MAX_PROTOCOL_MESSAGE)
    {
        Serial.println("Invalid message length");
        return;
    }

    // Deserialize message
    Message msg;
    if (!msg.deserialize(data, len))
    {
        Serial.println("Failed to deserialize message");
        return;
    }

    // Send to LoRa queue
    if (bleToLoraQueue != nullptr)
    {
        if (!bleToLoraQueue->push(msg))
        {
            Serial.println("BLE->LoRa queue full");
        }
        else
        {
            Serial.print("Message queued for LoRa, type: ");
            Serial.println((int)msg.type);
        }
    }
}

void BLEManager::handleCccdWrite(uint16_t value)
{
    if (value & BLE_GATT_HVX_NOTIFICATION)
    {
        Serial.println("TX notifications enabled by client");
        notificationsEnabled = true;
    }
    else
    {
        Serial.println("TX notifications disabled by client");
        notificationsEnabled = false;
    }
}
