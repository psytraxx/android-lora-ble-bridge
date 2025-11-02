#include "LoRaManager.h"
#include "esp_log.h"
#include "EspHal.h"

// Static instance for ISR access
LoRaManager *LoRaManager::instance = nullptr;

static const char *TAG_LORA = "LoRaManager";

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
      receiveCallback(nullptr)
{
    // Set singleton instance for ISR access
    instance = this;

    EspHal *hal = new EspHal(pinSCK, pinMISO, pinMOSI);

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

bool LoRaManager::transmit(const uint8_t *data, size_t len)
{
    if (!initialized)
    {
        ESP_LOGE(TAG_LORA, "Cannot transmit - not initialized");
        return false;
    }

    ESP_LOGI(TAG_LORA, "Transmitting %d bytes", len);

    // Clear interrupt handler to allow DIO0 to signal TX completion
    radio->clearPacketReceivedAction();

    // Transmit the data
    int state = radio->transmit(const_cast<uint8_t *>(data), len);

    bool success = (state == RADIOLIB_ERR_NONE);

    if (success)
    {
        ESP_LOGI(TAG_LORA, "Transmission successful");
    }
    else
    {
        ESP_LOGE(TAG_LORA, "Transmission failed, code %d", state);
    }

    // Restore interrupt handler and return to RX mode
    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);
    radio->startReceive();

    // Wait for radio to settle in RX mode
    waitForRadioSettle();

    return success;
}

void LoRaManager::setReceiveCallback(LoRaReceiveCallback callback)
{
    receiveCallback = callback;
}

void LoRaManager::process()
{
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

void LoRaManager::handleReceiveInterrupt()
{
    // Set flag only - do NOT read data in ISR
    // Data reading happens in process() called from main loop
    packetReceived = true;
}

void LoRaManager::waitForRadioSettle(int delayMs)
{
    // Wait for SX1278 hardware to stabilize after mode change
    vTaskDelay(delayMs / portTICK_PERIOD_MS);
}
