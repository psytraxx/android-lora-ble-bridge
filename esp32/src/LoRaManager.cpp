#include "LoRaManager.h"
#include "FirmwareConfig.h"
#include "esp_log.h"
#if CONFIG_IDF_TARGET_ESP32
#include "Esp32Hal.h"
#endif
#if CONFIG_IDF_TARGET_ESP32S3
#include "Esp32S3Hal.h"
#endif

// Static instance for ISR access
LoRaManager *LoRaManager::instance = nullptr;

static const char *TAG = "LORA";

LoRaManager::LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0)
    : pinSCK(sck),
      pinMISO(miso),
      pinMOSI(mosi),
      pinSS(ss),
      pinRST(rst),
      pinDIO0(dio0),
      radio(nullptr),
      state(STATE_UNINITIALIZED),
      receiveCallback(nullptr),
      transmitCallback(nullptr)
{
    // Set singleton instance for ISR access
    instance = this;

#if CONFIG_IDF_TARGET_ESP32
    EspHal *hal = new EspHal(pinSCK, pinMISO, pinMOSI);
#elif CONFIG_IDF_TARGET_ESP32S3
    EspS3Hal *hal = new EspS3Hal(pinSCK, pinMISO, pinMOSI);
#endif

    // Create RadioLib module instance
    radio = new SX1278(new Module(hal, pinSS, pinDIO0, pinRST));
}

bool LoRaManager::begin(const LoRaConfig &config)
{
    ESP_LOGI(TAG, "Initializing radio");
    // Attempt initialization with retries
    for (int attempt = 1; attempt <= LoRaConstants::INIT_RETRY_COUNT; attempt++)
    {

        ESP_LOGI(TAG, "Setup attempt %d/%d", attempt, LoRaConstants::INIT_RETRY_COUNT);

        int state = radio->begin(
            config.frequency,
            config.bandwidth,
            config.spreadingFactor,
            config.codingRate,
            config.syncWord,
            config.txPower);

        radio->setCRC(config.useCrc);

        if (state == RADIOLIB_ERR_NONE)
        {
            this->state = STATE_IDLE;
            ESP_LOGI(TAG, "Setup successful");
            ESP_LOGI(TAG, "  Frequency: %.2f MHz", config.frequency);
            ESP_LOGI(TAG, "  Bandwidth: %.2f kHz", config.bandwidth);
            ESP_LOGI(TAG, "  Spreading Factor: %d", config.spreadingFactor);
            ESP_LOGI(TAG, "  Coding Rate: 4/%d", config.codingRate);
            ESP_LOGI(TAG, "  TX Power: %d dBm", config.txPower);
            return true;
        }

        ESP_LOGW(TAG, "Setup failed, code %d", state);

        if (attempt < LoRaConstants::INIT_RETRY_COUNT)
        {
            ESP_LOGW(TAG, "Retrying in 1 second...");
            vTaskDelay(pdMS_TO_TICKS(LoRaConstants::INIT_RETRY_DELAY_MS));
        }
    }

    ESP_LOGE(TAG, "Setup failed permanently");
    return false;
}

bool LoRaManager::startReceive()
{
    if (state == STATE_UNINITIALIZED)
    {
        ESP_LOGE(TAG, "Cannot start receive - not initialized");
        return false;
    }

    restoreReceiveMode();
    state = STATE_IDLE;
    ESP_LOGI(TAG, "Continuous receive mode started");
    return true;
}

bool LoRaManager::startTransmit(const uint8_t *data, size_t len)
{
    if (state == STATE_UNINITIALIZED)
    {
        ESP_LOGE(TAG, "Cannot transmit - not initialized");
        return false;
    }

    if (state == STATE_TRANSMITTING)
    {
        ESP_LOGW(TAG, "Transmission already in progress");
        return false;
    }

    ESP_LOGI(TAG, "Starting transmission of %d bytes", len);

    // Switch to transmit mode with interrupt
    radio->clearPacketReceivedAction();
    radio->setPacketSentAction(LoRaManager::onTransmitISR);

    // Start non-blocking transmission
    state = STATE_TRANSMITTING;
    int txState = radio->startTransmit(const_cast<uint8_t *>(data), len);

    if (txState != RADIOLIB_ERR_NONE)
    {
        ESP_LOGE(TAG, "Failed to start transmission, code %d", txState);
        restoreReceiveMode();
        state = STATE_IDLE;
        return false;
    }

    ESP_LOGI(TAG, "Transmission started (non-blocking)");
    return true;
}

void LoRaManager::setReceiveCallback(LoRaReceiveCallback callback)
{
    receiveCallback = callback;
}

void LoRaManager::setTransmitCallback(LoRaTransmitCallback callback)
{
    transmitCallback = callback;
}

void LoRaManager::process()
{
    // Check for completed transmission
    if (state == STATE_PACKET_SENT)
    {
        ESP_LOGI(TAG, "Transmission completed");

        // Return to receive mode
        restoreReceiveMode();
        state = STATE_IDLE;

        // Invoke transmit callback
        if (transmitCallback)
        {
            transmitCallback(true);
        }
    }

    // Check for received packets
    if (state != STATE_PACKET_RECEIVED)
    {
        return;
    }

    // Read packet data
    LoRaPacket packet;
    int rxState = radio->readData(packet.buffer, sizeof(packet.buffer));

    if (rxState == RADIOLIB_ERR_NONE)
    {
        packet.len = radio->getPacketLength();
        packet.rssi = radio->getRSSI();
        packet.snr = radio->getSNR();

        ESP_LOGI(TAG, "Packet received (%d bytes, RSSI: %d dBm, SNR: %.2f dB)",
                 packet.len, packet.rssi, packet.snr);

        if (receiveCallback)
        {
            receiveCallback(packet);
        }
    }
    else if (rxState == RADIOLIB_ERR_CRC_MISMATCH)
    {
        ESP_LOGW(TAG, "CRC error");
    }
    else
    {
        ESP_LOGE(TAG, "Read failed, code %d", rxState);
    }

    // Restart receive mode
    radio->startReceive();
    state = STATE_IDLE;
}

int LoRaManager::getRSSI() const
{
    if (state == STATE_UNINITIALIZED)
        return 0;
    return radio->getRSSI();
}

float LoRaManager::getSNR() const
{
    if (state == STATE_UNINITIALIZED)
        return 0.0f;
    return radio->getSNR();
}

void IRAM_ATTR LoRaManager::onReceiveISR()
{
    if (instance)
    {
        instance->handleReceiveInterrupt();
    }
}

void IRAM_ATTR LoRaManager::onTransmitISR()
{
    if (instance)
    {
        instance->handleTransmitInterrupt();
    }
}

void LoRaManager::handleReceiveInterrupt()
{
    // Set state - do NOT read data in ISR
    // Data reading happens in process() called from main loop
    state = STATE_PACKET_RECEIVED;
}

void LoRaManager::handleTransmitInterrupt()
{
    // Set state - do NOT perform cleanup in ISR
    // Cleanup happens in process() called from main loop
    state = STATE_PACKET_SENT;
}

void LoRaManager::restoreReceiveMode()
{
    radio->clearPacketSentAction();
    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);
    radio->startReceive();
}
