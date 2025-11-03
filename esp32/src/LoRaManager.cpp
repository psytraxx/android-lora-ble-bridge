#include "LoRaManager.h"
#include "esp_log.h"
#if CONFIG_IDF_TARGET_ESP32
#include "Esp32Hal.h"
#endif
#if CONFIG_IDF_TARGET_ESP32S3
#include "Esp32S3Hal.h"
#endif

// Static instance for ISR access
LoRaManager *LoRaManager::instance = nullptr;

static const char *TAG_LORA = "LORA";

LoRaManager::LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0)
    : pinSCK(sck),
      pinMISO(miso),
      pinMOSI(mosi),
      pinSS(ss),
      pinRST(rst),
      pinDIO0(dio0),
      radio(nullptr),
      initialized(false),
      packetReceived(false),
      packetTransmitted(false),
      transmitting(false),
      transmissionState(RADIOLIB_ERR_NONE),
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

bool LoRaManager::begin(const LoRaConfig &config, int retryCount)
{
    ESP_LOGI(TAG_LORA, "Initializing radio");
    // Attempt initialization with retries
    for (int attempt = 1; attempt <= retryCount; attempt++)
    {

        ESP_LOGI(TAG_LORA, "Setup attempt %d/%d", attempt, retryCount);

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
            initialized = true;
            ESP_LOGI(TAG_LORA, "Setup successful");
            ESP_LOGI(TAG_LORA, "  Frequency: %.2f MHz", config.frequency);
            ESP_LOGI(TAG_LORA, "  Bandwidth: %.2f kHz", config.bandwidth);
            ESP_LOGI(TAG_LORA, "  Spreading Factor: %d", config.spreadingFactor);
            ESP_LOGI(TAG_LORA, "  Coding Rate: 4/%d", config.codingRate);
            ESP_LOGI(TAG_LORA, "  TX Power: %d dBm", config.txPower);
            return true;
        }

        ESP_LOGW(TAG_LORA, "Setup failed, code %d", state);

        if (attempt < retryCount)
        {
            ESP_LOGW(TAG_LORA, "Retrying in 1 second...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    ESP_LOGE(TAG_LORA, "Setup failed permanently");
    return false;
}

bool LoRaManager::startReceive()
{
    if (!initialized)
    {
        ESP_LOGE(TAG_LORA, "Cannot start receive - not initialized");
        return false;
    }

    // Set up interrupt-driven receive
    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);

    int state = radio->startReceive();
    if (state == RADIOLIB_ERR_NONE)
    {
        ESP_LOGI(TAG_LORA, "Continuous receive mode started");
        return true;
    }

    ESP_LOGE(TAG_LORA, "Failed to start receive mode, code %d", state);
    return false;
}

bool LoRaManager::startTransmit(const uint8_t *data, size_t len)
{
    if (!initialized)
    {
        ESP_LOGE(TAG_LORA, "Cannot transmit - not initialized");
        return false;
    }

    if (transmitting)
    {
        ESP_LOGW(TAG_LORA, "Transmission already in progress");
        return false;
    }

    ESP_LOGI(TAG_LORA, "Starting transmission of %d bytes", len);

    // Clear receive interrupt handler before transmitting
    radio->clearPacketReceivedAction();

    // Set transmit interrupt handler
    radio->setPacketSentAction(LoRaManager::onTransmitISR);

    // Start non-blocking transmission
    transmitting = true;
    transmissionState = radio->startTransmit(const_cast<uint8_t *>(data), len);

    if (transmissionState != RADIOLIB_ERR_NONE)
    {
        ESP_LOGE(TAG_LORA, "Failed to start transmission, code %d", transmissionState);
        transmitting = false;

        // Restore receive mode
        radio->clearPacketSentAction();
        radio->setPacketReceivedAction(LoRaManager::onReceiveISR);
        radio->startReceive();

        return false;
    }

    ESP_LOGI(TAG_LORA, "Transmission started (non-blocking)");
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
    if (packetTransmitted)
    {
        // Clear flag
        packetTransmitted = false;
        transmitting = false;

        bool success = (transmissionState == RADIOLIB_ERR_NONE);

        if (success)
        {
            ESP_LOGI(TAG_LORA, "Transmission completed successfully");
        }
        else
        {
            ESP_LOGE(TAG_LORA, "Transmission failed, code %d", transmissionState);
        }

        // Clean up after transmission is finished
        // This ensures transmitter is disabled
        radio->finishTransmit();

        // Restore receive interrupt handler and return to RX mode
        radio->setPacketReceivedAction(LoRaManager::onReceiveISR);
        radio->startReceive();

        // Wait for radio to settle in RX mode
        waitForRadioSettle();

        // Invoke transmit callback if set
        if (transmitCallback)
        {
            transmitCallback(success);
        }
    }

    // Check for received packets
    if (!packetReceived)
    {
        return;
    }

    // Clear flag
    packetReceived = false;

    // Read packet data
    LoRaPacket packet;
    int state = radio->readData(packet.buffer, sizeof(packet.buffer));

    if (state == RADIOLIB_ERR_NONE)
    {
        packet.len = radio->getPacketLength();
        packet.rssi = radio->getRSSI();
        packet.snr = radio->getSNR();

        ESP_LOGI(TAG_LORA, "Packet received (%d bytes, RSSI: %d dBm, SNR: %.2f dB)",
                 packet.len, packet.rssi, packet.snr);

        // Invoke callback if set
        if (receiveCallback)
        {
            receiveCallback(packet);
        }
    }
    else if (state == RADIOLIB_ERR_CRC_MISMATCH)
    {
        ESP_LOGW(TAG_LORA, "CRC error");
    }
    else
    {
        ESP_LOGE(TAG_LORA, "Read failed, code %d", state);
    }

    // Restart receive mode
    radio->startReceive();
}

int LoRaManager::getRSSI() const
{
    if (!initialized)
        return 0;
    return radio->getRSSI();
}

float LoRaManager::getSNR() const
{
    if (!initialized)
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
    // Set flag only - do NOT read data in ISR
    // Data reading happens in process() called from main loop
    packetReceived = true;
}

void LoRaManager::handleTransmitInterrupt()
{
    // Set flag only - do NOT perform cleanup in ISR
    // Cleanup happens in process() called from main loop
    packetTransmitted = true;
}

void LoRaManager::waitForRadioSettle(int delayMs)
{
    // Wait for SX1278 hardware to stabilize after mode change
    vTaskDelay(delayMs / portTICK_PERIOD_MS);
}
