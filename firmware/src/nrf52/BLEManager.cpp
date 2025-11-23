#include "nrf52/BLEManager.h"
#include "common/Logging.h"

static const char *TAG = "BLE";

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
    LOG_I(TAG, "Initializing BLE: %s", deviceName);

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
    // Use variable-length characteristic (default) for efficient BLE bandwidth usage
    // Removes fixed-length padding - matches ESP32 implementation
    txCharacteristic.setCccdWriteCallback(BLEManager::cccdCallback);
    txCharacteristic.begin();

    // Configure RX Characteristic (Android -> ESP32)
    rxCharacteristic.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
    rxCharacteristic.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
    // Use variable-length characteristic (default) for efficient BLE bandwidth usage
    // Allows messages of any size up to MTU - matches ESP32 implementation
    rxCharacteristic.setWriteCallback(BLEManager::rxWriteCallback);
    rxCharacteristic.begin();

    LOG_I(TAG, "Initialized successfully");
    return true;
}

void BLEManager::startAdvertising()
{
    LOG_I(TAG, "Starting advertising");

    // Advertising packet
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(loraService);
    Bluefruit.Advertising.addName();

    // Secondary Scan Response packet (optional)
    Bluefruit.ScanResponse.addName();

    // Start advertising - restart handled manually in onBleDisconnected callback
    Bluefruit.Advertising.restartOnDisconnect(false);
    Bluefruit.Advertising.setInterval(BLEConstants::ADV_MIN_INTERVAL, BLEConstants::ADV_MAX_INTERVAL);
    Bluefruit.Advertising.setFastTimeout(30); // Fast advertising for 30 seconds
    Bluefruit.Advertising.start(0);           // 0 = Don't stop advertising after n seconds
}

void BLEManager::stopAdvertising()
{
    LOG_I(TAG, "Stopping advertising");
    Bluefruit.Advertising.stop();
}

void BLEManager::disconnect()
{
    if (Bluefruit.connected())
    {
        LOG_I(TAG, "Disconnecting client");
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
        LOG_W(TAG, "Cannot send: not connected or notifications disabled");
        return false;
    }

    // Serialize message
    uint8_t buffer[BufferConstants::MAX_PROTOCOL_MESSAGE];
    int length = msg.serialize(buffer, sizeof(buffer));

    if (length <= 0)
    {
        LOG_E(TAG, "Failed to serialize message");
        return false;
    }

    // Send notification
    bool success = txCharacteristic.notify(buffer, length);

    if (success)
    {
        LOG_D(TAG, "Sent message, type: %d, seq: %d, size: %d", (int)msg.type, (int)msg.textData.seq, length);
    }
    else
    {
        LOG_E(TAG, "Failed to send notification");
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

        LOG_I(TAG, "Connected: %s", central_name);

        instance->isConnectedFlag = true;

        // Request power-optimized connection parameters
        // Bluefruit API: conn_interval (1.25ms units), slave_latency, sup_timeout (10ms units)
        // 160 units * 1.25ms = 200ms interval for power savings
        if (connection)
        {
            connection->requestConnectionParameter(160, 0, 400);
            LOG_I(TAG, "Requested power-optimized connection params (200ms interval)");
        }

        if (instance->connectCallback_user)
        {
            instance->connectCallback_user();
        }
    }
}

void BLEManager::disconnectCallback(uint16_t conn_handle, uint8_t reason)
{
    (void)conn_handle;

    if (instance)
    {
        LOG_I(TAG, "Disconnected, reason: %d", reason);

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
    LOG_D(TAG, "RX received %d bytes", len);

    if (len == 0 || len > BufferConstants::MAX_PROTOCOL_MESSAGE)
    {
        LOG_E(TAG, "Invalid message length");
        return;
    }

    // Deserialize message
    Message msg;
    if (!msg.deserialize(data, len))
    {
        LOG_E(TAG, "Failed to deserialize message");
        return;
    }

    // Send to LoRa queue
    if (bleToLoraQueue != nullptr)
    {
        if (!bleToLoraQueue->push(msg))
        {
            LOG_E(TAG, "Queue full");
        }
        else
        {
            LOG_D(TAG, "Message queued for LoRa, type: %d", (int)msg.type);
        }
    }
}

void BLEManager::handleCccdWrite(uint16_t value)
{
    if (value & BLE_GATT_HVX_NOTIFICATION)
    {
        LOG_I(TAG, "TX notifications enabled by client");
        notificationsEnabled = true;
    }
    else
    {
        LOG_I(TAG, "TX notifications disabled by client");
        notificationsEnabled = false;
    }
}
