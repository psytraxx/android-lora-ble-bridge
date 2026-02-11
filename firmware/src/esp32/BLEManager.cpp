#include "esp32/BLEManager.h"
#include "common/ConfigManager.h"
#include "common/NodeDB.h"
#include "common/PeerNodeDB.h"
#include "common/Logging.h"
#include <string.h>
#include <Arduino.h>

static const char *TAG = "BLE";

// ============================================================================
// NimBLE Callback Implementations
// ============================================================================

void MeshServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
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
                              0,    // latency
                              400); // timeout (4000ms)
    LOG_I(TAG, "Requested power-optimized connection params (100-200ms interval)");

    // Stop advertising when connected
    NimBLEDevice::getAdvertising()->stop();
}

void MeshServerCallbacks::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
{
    LOG_I(TAG, "Client disconnected, reason: %d", reason);
    bleManager->onDisconnected(connInfo.getConnHandle());
}

void ToRadioCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
{
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
        LOG_D(TAG, "ToRadio write received (%d bytes)", value.length());
        bleManager->onToRadioReceived((const uint8_t *)value.data(), value.length());
    }
}

void FromRadioCallbacks::onSubscribe(NimBLECharacteristic *pCharacteristic,
                                     NimBLEConnInfo &connInfo, uint16_t subValue)
{
    if (subValue & 0x0001)
    {
        LOG_I(TAG, "FromRadio notifications enabled");
        bleManager->onNotificationsEnabled(true);
    }
    else
    {
        LOG_I(TAG, "FromRadio notifications disabled");
        bleManager->onNotificationsEnabled(false);
    }
}

void FromRadioCallbacks::onRead(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
{
    bleManager->onFromRadioRead(pCharacteristic);
}

// ============================================================================
// BLEManager Implementation
// ============================================================================

BLEManager::BLEManager(MessageQueue<meshtastic_ToRadio> *toRadioQueue)
    : _toRadioQueue(toRadioQueue)
{
}

bool BLEManager::setup(const char *deviceName)
{
    LOG_I(TAG, "Initializing Meshtastic BLE service");

    _deviceNameStr = std::string(deviceName);
    _fqMutex = xSemaphoreCreateMutex();

    NimBLEDevice::init(deviceName);
    NimBLEDevice::setMTU(512);

    // Log MAC address
    NimBLEAddress address = NimBLEDevice::getAddress();
    LOG_I(TAG, "BLE MAC Address: %s", address.toString().c_str());

    // Create server
    _pServer = NimBLEDevice::createServer();
    _serverCallbacks = new MeshServerCallbacks(this);
    _pServer->setCallbacks(_serverCallbacks);

    // Create Meshtastic service
    NimBLEService *pService = _pServer->createService(MeshtasticBLE::SERVICE_UUID);

    // FromRadio characteristic (Device -> Phone): READ only
    // Per official Meshtastic firmware: "Adding NIMBLE_PROPERTY::NOTIFY to
    // FromRadioCharacteristic appears to break things."
    _fromRadioChar = pService->createCharacteristic(
        MeshtasticBLE::FROMRADIO_UUID,
        NIMBLE_PROPERTY::READ);
    _fromRadioCallbacks = new FromRadioCallbacks(this);
    _fromRadioChar->setCallbacks(_fromRadioCallbacks);

    // ToRadio characteristic (Phone -> Device): WRITE
    _toRadioChar = pService->createCharacteristic(
        MeshtasticBLE::TORADIO_UUID,
        NIMBLE_PROPERTY::WRITE);
    _toRadioCallbacks = new ToRadioCallbacks(this);
    _toRadioChar->setCallbacks(_toRadioCallbacks);

    // FromNum characteristic (Packet counter): READ + NOTIFY + WRITE
    _fromNumChar = pService->createCharacteristic(
        MeshtasticBLE::FROMNUM_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::WRITE);
    _fromNumChar->setValue(_fromNum);

    pService->start();

    // Configure advertising
    _pAdvertising = NimBLEDevice::getAdvertising();
    _pAdvertising->addServiceUUID(MeshtasticBLE::SERVICE_UUID);
    _pAdvertising->enableScanResponse(true);
    _pAdvertising->setMinInterval(BLEConstants::ADV_MIN_INTERVAL);
    _pAdvertising->setMaxInterval(BLEConstants::ADV_MAX_INTERVAL);
    _pAdvertising->setName(deviceName);

    NimBLEDevice::setPower(BLEConstants::TX_POWER_LEVEL);

    LOG_I(TAG, "Meshtastic BLE service initialized");
    LOG_I(TAG, "Service UUID: %s", MeshtasticBLE::SERVICE_UUID);
    return true;
}

void BLEManager::startAdvertising()
{
    LOG_I(TAG, "Starting advertising");
    NimBLEDevice::startAdvertising();
}

void BLEManager::stopAdvertising()
{
    if (_pAdvertising)
    {
        _pAdvertising->stop();
        LOG_I(TAG, "Advertising stopped");
    }
}

void BLEManager::disconnect()
{
    if (!isConnected()) return;

    LOG_I(TAG, "Disconnecting client");
    if (_pServer && _currentConnHandle != kInvalidConnHandle)
    {
        _pServer->disconnect(_currentConnHandle);
    }
}

bool BLEManager::isConnected() const
{
    NimBLEServer *srv = NimBLEDevice::getServer();
    if (!srv) return false;
    return srv->getConnectedCount() > 0;
}

// ============================================================================
// Meshtastic Protocol Methods
// ============================================================================

void BLEManager::onToRadioReceived(const uint8_t *data, size_t length)
{
    // Decode ToRadio protobuf
    meshtastic_ToRadio toRadio = meshtastic_ToRadio_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, length);

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
    // Defer to processPendingConfig() called from the main loop.
    // Running sendConfigDownload() here would overflow the nimble_host
    // task stack (4-8 KB) due to large protobuf structs on the stack.
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
    }

    // 2. DeviceMetadata
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_metadata_tag;
    if (ConfigManager::getDeviceMetadata(&fromRadio.metadata))
    {
        sendFromRadio(&fromRadio);
    }

    // 3. Config.Device
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_config_tag;
    if (ConfigManager::getConfig(&fromRadio.config, meshtastic_Config_device_tag))
    {
        sendFromRadio(&fromRadio);
    }

    // 4. Config.LoRa
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_config_tag;
    if (ConfigManager::getConfig(&fromRadio.config, meshtastic_Config_lora_tag))
    {
        sendFromRadio(&fromRadio);
    }

    // 5. Config.Bluetooth
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_config_tag;
    if (ConfigManager::getConfig(&fromRadio.config, meshtastic_Config_bluetooth_tag))
    {
        sendFromRadio(&fromRadio);
    }

    // 6. Channel[0] (primary)
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_channel_tag;
    if (ConfigManager::getChannel(&fromRadio.channel, 0))
    {
        sendFromRadio(&fromRadio);
    }

    // 7. Own NodeInfo
    fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = _fromNum + 1;
    fromRadio.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    if (ConfigManager::getOwnNodeInfo(&fromRadio.node_info))
    {
        sendFromRadio(&fromRadio);
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

    if (!enqueueFromRadio(buffer, len))
    {
        LOG_W(TAG, "FromRadio queue full, message dropped");
        return false;
    }

    // Only notify FromNum during steady-state packet forwarding.
    // During config download, the phone polls FromRadio continuously.
    if (!_configDownloadInProgress)
    {
        incrementFromNum();
    }

    LOG_D(TAG, "Queued FromRadio: %d bytes, variant=%d", len, fromRadio->which_payload_variant);
    return true;
}

bool BLEManager::enqueueFromRadio(const uint8_t *data, size_t len)
{
    if (xSemaphoreTake(_fqMutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;

    if (_fromRadioQueue.size() >= FROM_RADIO_QUEUE_CAPACITY)
    {
        xSemaphoreGive(_fqMutex);
        return false;
    }

    _fromRadioQueue.push(std::vector<uint8_t>(data, data + len));
    xSemaphoreGive(_fqMutex);
    return true;
}

void BLEManager::onFromRadioRead(NimBLECharacteristic *pCharacteristic)
{
    // The phone reads FromRadio immediately after writing want_config_id.
    // If the queue is empty but config is pending/in-progress, busy-wait
    // for the main task to fill the queue via processPendingConfig().
    // This matches the official Meshtastic firmware pattern.
    if (_pendingConfigId != 0 || _configDownloadInProgress)
    {
        for (int tries = 0; tries < 200; tries++)
        {
            if (xSemaphoreTake(_fqMutex, pdMS_TO_TICKS(5)) == pdTRUE)
            {
                bool hasData = !_fromRadioQueue.empty();
                xSemaphoreGive(_fqMutex);
                if (hasData)
                    break;
            }
            delay(1); // yield to FreeRTOS so main loop can run
        }
    }

    if (xSemaphoreTake(_fqMutex, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        pCharacteristic->setValue((const uint8_t *)"", 0);
        return;
    }

    if (!_fromRadioQueue.empty())
    {
        auto &item = _fromRadioQueue.front();
        pCharacteristic->setValue(item.data(), item.size());
        _fromRadioQueue.pop();
    }
    else
    {
        pCharacteristic->setValue((const uint8_t *)"", 0);
    }

    xSemaphoreGive(_fqMutex);
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
    _fromNumChar->setValue(_fromNum);
    _fromNumChar->notify();
}

// ============================================================================
// Connection State
// ============================================================================

void BLEManager::onConnected(uint16_t connHandle)
{
    _currentConnHandle = connHandle;
    _notificationsEnabled = false;

    if (_connectCallback)
    {
        _connectCallback();
    }
}

void BLEManager::onDisconnected(uint16_t connHandle)
{
    if (connHandle == _currentConnHandle)
    {
        _currentConnHandle = kInvalidConnHandle;
        _notificationsEnabled = false;
        _configDownloadInProgress = false;
        _pendingConfigId = 0;

        // Drain stale FromRadio queue so next client starts fresh
        if (xSemaphoreTake(_fqMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            while (!_fromRadioQueue.empty())
                _fromRadioQueue.pop();
            xSemaphoreGive(_fqMutex);
        }

        if (_disconnectCallback)
        {
            _disconnectCallback();
        }
    }
}

void BLEManager::onNotificationsEnabled(bool enabled)
{
    _notificationsEnabled = enabled;
    LOG_I(TAG, "FromRadio notifications: %s", enabled ? "ENABLED" : "DISABLED");
}

void BLEManager::setConnectionCallbacks(void (*onConnect)(), void (*onDisconnect)())
{
    _connectCallback = onConnect;
    _disconnectCallback = onDisconnect;
}
