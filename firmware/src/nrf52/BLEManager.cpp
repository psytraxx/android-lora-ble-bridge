#include "nrf52/BLEManager.h"
#include "common/ConfigManager.h"
#include "common/NodeDB.h"
#include "common/PeerNodeDB.h"
#include "common/Logging.h"

static const char *TAG = "BLE";

// Static instance for callbacks
BLEManager *BLEManager::instance = nullptr;

BLEManager::BLEManager(MessageQueue<meshtastic_ToRadio> *toRadioQueue)
    : _meshService(MeshtasticBLE::SERVICE_UUID),
      _fromRadioChar(MeshtasticBLE::FROMRADIO_UUID),
      _toRadioChar(MeshtasticBLE::TORADIO_UUID),
      _fromNumChar(MeshtasticBLE::FROMNUM_UUID),
      _toRadioQueue(toRadioQueue)
{
    instance = this;
}

bool BLEManager::setup(const char *deviceName)
{
    _deviceNameStr = String(deviceName);
    LOG_I(TAG, "Initializing Meshtastic BLE service: %s", deviceName);

    // Initialize Bluefruit with max bandwidth
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    if (!Bluefruit.begin())
    {
        LOG_E(TAG, "Failed to initialize Bluefruit");
        return false;
    }
    Bluefruit.setTxPower(BLEConstants::TX_POWER_DBM);
    Bluefruit.setName(deviceName);

    // Log MAC address
    uint8_t mac[6];
    Bluefruit.getAddr(mac);
    LOG_I(TAG, "BLE MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
          mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);

    // Set connection callbacks
    Bluefruit.Periph.setConnectCallback(BLEManager::connectCallback);
    Bluefruit.Periph.setDisconnectCallback(BLEManager::disconnectCallback);

    // Configure Device Information Service
    _bledis.setManufacturer("Meshtastenstein");
    _bledis.setModel("nRF52840 LoRa Bridge");
    _bledis.begin();

    // Configure Meshtastic service
    _meshService.begin();

    // FromRadio characteristic (Device -> Phone): READ + NOTIFY
    _fromRadioChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    _fromRadioChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    _fromRadioChar.setCccdWriteCallback(BLEManager::fromRadioCccdCallback);
    _fromRadioChar.setMaxLen(MeshtasticBLE::MAX_TO_FROM_RADIO_SIZE);
    _fromRadioChar.begin();

    // ToRadio characteristic (Phone -> Device): WRITE
    _toRadioChar.setProperties(CHR_PROPS_WRITE);
    _toRadioChar.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
    _toRadioChar.setWriteCallback(BLEManager::toRadioWriteCallback);
    _toRadioChar.setMaxLen(MeshtasticBLE::MAX_TO_FROM_RADIO_SIZE);
    _toRadioChar.begin();

    // FromNum characteristic (Counter): READ + NOTIFY + WRITE
    _fromNumChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY | CHR_PROPS_WRITE);
    _fromNumChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    _fromNumChar.setFixedLen(4);
    _fromNumChar.begin();
    _fromNumChar.write32(_fromNum);

    LOG_I(TAG, "Meshtastic BLE service initialized");
    LOG_I(TAG, "Service UUID: %s", MeshtasticBLE::SERVICE_UUID);
    return true;
}

void BLEManager::startAdvertising()
{
    LOG_I(TAG, "Starting advertising");

    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(_meshService);
    Bluefruit.Advertising.addName();

    Bluefruit.ScanResponse.addName();

    Bluefruit.Advertising.restartOnDisconnect(false);
    Bluefruit.Advertising.setInterval(BLEConstants::ADV_MIN_INTERVAL, BLEConstants::ADV_MAX_INTERVAL);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);
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
    return _isConnectedFlag;
}

// ============================================================================
// Meshtastic Protocol Methods
// ============================================================================

void BLEManager::handleToRadioWrite(uint8_t *data, uint16_t len)
{
    if (len == 0 || len > MeshtasticBLE::MAX_TO_FROM_RADIO_SIZE)
    {
        LOG_E(TAG, "Invalid ToRadio length: %d", len);
        return;
    }

    // Decode ToRadio protobuf
    meshtastic_ToRadio toRadio = meshtastic_ToRadio_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, len);

    if (!pb_decode(&stream, meshtastic_ToRadio_fields, &toRadio))
    {
        LOG_E(TAG, "ToRadio decode failed");
        return;
    }

    switch (toRadio.which_payload_variant)
    {
    case meshtastic_ToRadio_packet_tag:
        LOG_D(TAG, "ToRadio: packet for LoRa TX (to=%08X, id=%u)",
              toRadio.packet.to, toRadio.packet.id);
        if (_toRadioQueue)
        {
            if (!_toRadioQueue->push(toRadio))
            {
                LOG_E(TAG, "ToRadio queue full, packet dropped");
            }
        }
        break;

    case meshtastic_ToRadio_want_config_id_tag:
        LOG_I(TAG, "ToRadio: want_config_id=%u", toRadio.want_config_id);
        handleConfigRequest(toRadio.want_config_id);
        break;

    case meshtastic_ToRadio_disconnect_tag:
        LOG_I(TAG, "ToRadio: disconnect");
        break;

    case meshtastic_ToRadio_heartbeat_tag:
        LOG_D(TAG, "ToRadio: heartbeat");
        break;

    default:
        LOG_W(TAG, "ToRadio: unknown variant %d", toRadio.which_payload_variant);
        break;
    }
}

void BLEManager::handleConfigRequest(uint32_t configId)
{
    // Defer to processPendingConfig() called from the main loop
    // to avoid deep stack usage in the BLE callback context.
    _pendingConfigId = configId;
}

void BLEManager::processPendingConfig()
{
    uint32_t configId = _pendingConfigId;
    if (configId == 0)
        return;

    _pendingConfigId = 0;
    _configDownloadInProgress = true;
    sendConfigDownload(configId);
    _configDownloadInProgress = false;
}

void BLEManager::sendConfigDownload(uint32_t configId)
{
    LOG_I(TAG, "Starting config download (id=%u)", configId);
    meshtastic_FromRadio fromRadio = meshtastic_FromRadio_init_zero;

    // 1. MyNodeInfo
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_my_info_tag;
    if (ConfigManager::getMyNodeInfo(&fromRadio.my_info))
    {
        sendFromRadio(&fromRadio);
        delay(50);
    }

    // 2. DeviceMetadata
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_metadata_tag;
    if (ConfigManager::getDeviceMetadata(&fromRadio.metadata))
    {
        sendFromRadio(&fromRadio);
        delay(50);
    }

    // 3. Config.Device
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_config_tag;
    if (ConfigManager::getConfig(&fromRadio.config, meshtastic_Config_device_tag))
    {
        sendFromRadio(&fromRadio);
        delay(50);
    }

    // 4. Config.LoRa
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_config_tag;
    if (ConfigManager::getConfig(&fromRadio.config, meshtastic_Config_lora_tag))
    {
        sendFromRadio(&fromRadio);
        delay(50);
    }

    // 5. Config.Bluetooth
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_config_tag;
    if (ConfigManager::getConfig(&fromRadio.config, meshtastic_Config_bluetooth_tag))
    {
        sendFromRadio(&fromRadio);
        delay(50);
    }

    // 6. Channel[0] (primary)
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_channel_tag;
    if (ConfigManager::getChannel(&fromRadio.channel, 0))
    {
        sendFromRadio(&fromRadio);
        delay(50);
    }

    // 7. Own NodeInfo
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    if (ConfigManager::getOwnNodeInfo(&fromRadio.node_info))
    {
        sendFromRadio(&fromRadio);
        delay(50);
    }

    // 7b. Peer NodeInfo entries (Phase 4)
    uint16_t peerCount = PeerNodeDB::getCount();
    LOG_I(TAG, "Sending %u peer NodeInfo entries", peerCount);
    for (uint16_t i = 0; i < peerCount; i++)
    {
        fromRadio = meshtastic_FromRadio_init_zero;
        fromRadio.id = _fromNum + 1;
        fromRadio.which_payload_variant = meshtastic_FromRadio_node_info_tag;
        if (PeerNodeDB::getNodeInfo(i, &fromRadio.node_info))
        {
            sendFromRadio(&fromRadio);
            delay(50);
        }
    }

    // 8. config_complete_id
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    fromRadio.config_complete_id = configId;
    sendFromRadio(&fromRadio);

    LOG_I(TAG, "Config download complete (id=%u)", configId);
}

bool BLEManager::sendFromRadio(const meshtastic_FromRadio *fromRadio)
{
    if (!isConnected())
    {
        LOG_W(TAG, "Cannot send FromRadio: not connected");
        return false;
    }

    uint8_t buffer[MeshtasticBLE::MAX_TO_FROM_RADIO_SIZE];
    size_t len = serializeFromRadio(fromRadio, buffer, sizeof(buffer));

    if (len == 0)
    {
        LOG_E(TAG, "FromRadio serialization failed");
        return false;
    }

    bool success = _fromRadioChar.notify(buffer, len);
    if (success)
    {
        incrementFromNum();
        LOG_D(TAG, "Sent FromRadio: %d bytes, variant=%d", len, fromRadio->which_payload_variant);
    }
    else
    {
        LOG_E(TAG, "FromRadio notify failed");
    }
    return success;
}

size_t BLEManager::serializeFromRadio(const meshtastic_FromRadio *fromRadio,
                                      uint8_t *buffer, size_t maxSize)
{
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, maxSize);
    if (!pb_encode(&stream, meshtastic_FromRadio_fields, fromRadio))
    {
        LOG_E(TAG, "pb_encode failed: %s", PB_GET_ERROR(&stream));
        return 0;
    }
    return stream.bytes_written;
}

void BLEManager::incrementFromNum()
{
    _fromNum++;
    _fromNumChar.write32(_fromNum);
    _fromNumChar.notify32(_fromNum);
}

// ============================================================================
// Static Callback Implementations
// ============================================================================

void BLEManager::connectCallback(uint16_t conn_handle)
{
    if (instance)
    {
        BLEConnection *connection = Bluefruit.Connection(conn_handle);
        char central_name[32] = {0};
        connection->getPeerName(central_name, sizeof(central_name));

        LOG_I(TAG, "Connected: %s", central_name);
        instance->_isConnectedFlag = true;

        // Request power-optimized connection parameters
        if (connection)
        {
            connection->requestConnectionParameter(160, 0, 400);
            connection->requestMtuExchange(MeshtasticBLE::MAX_TO_FROM_RADIO_SIZE + 3);
        }

        if (instance->_connectCallback_user)
        {
            instance->_connectCallback_user();
        }
    }
}

void BLEManager::disconnectCallback(uint16_t conn_handle, uint8_t reason)
{
    (void)conn_handle;

    if (instance)
    {
        LOG_I(TAG, "Disconnected, reason: %d", reason);
        instance->_isConnectedFlag = false;
        instance->_notificationsEnabled = false;

        if (instance->_disconnectCallback_user)
        {
            instance->_disconnectCallback_user();
        }
    }
}

void BLEManager::toRadioWriteCallback(uint16_t conn_hdl, BLECharacteristic *chr,
                                      uint8_t *data, uint16_t len)
{
    (void)conn_hdl;
    (void)chr;

    if (instance)
    {
        instance->handleToRadioWrite(data, len);
    }
}

void BLEManager::fromRadioCccdCallback(uint16_t conn_hdl, BLECharacteristic *chr,
                                       uint16_t value)
{
    (void)conn_hdl;
    (void)chr;

    if (instance)
    {
        instance->handleCccdWrite(value);
    }
}

void BLEManager::handleCccdWrite(uint16_t value)
{
    if (value & BLE_GATT_HVX_NOTIFICATION)
    {
        LOG_I(TAG, "FromRadio notifications enabled");
        _notificationsEnabled = true;
    }
    else
    {
        LOG_I(TAG, "FromRadio notifications disabled");
        _notificationsEnabled = false;
    }
}

void BLEManager::setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)())
{
    _connectCallback_user = onConnect;
    _disconnectCallback_user = onDisconnect;
}
