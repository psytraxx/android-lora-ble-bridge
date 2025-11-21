#include "esp32/LoRaManager.h"
#include "esp32/FirmwareConfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Static instance for ISR access
LoRaManager *LoRaManager::instance = nullptr;

static const char *TAG = "LORA";

LoRaManager::LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, int busy)
    : pinSCK(sck),
      pinMISO(miso),
      pinMOSI(mosi),
      pinSS(ss),
      pinRST(rst),
      pinDIO0(dio0),
      pinBusy(busy),
      radio(nullptr),
      state(STATE_UNINITIALIZED),
      receiveCallback(nullptr),
      transmitCallback(nullptr)
{
    // Set singleton instance for ISR access
    instance = this;
    // Create RadioLib module instance (stored as base class pointer for polymorphism)
#if defined(RADIO_SX1278)
    radio = new SX1278(new Module(pinSS, pinDIO0, pinRST));
#elif defined(RADIO_SX1262)
    radio = new SX1262(new Module(pinSS, pinDIO0, pinRST, pinBusy));
#else
#error "No supported RADIO defined! Please define RADIO_SX1278 or RADIO_SX1262 in platformio.ini"
#endif
}

bool LoRaManager::begin(const LoRaConfig &config)
{
    ESP_LOGI(TAG, "Initializing radio");

    SPI.begin(pinSCK, pinMISO, pinMOSI, pinSS);
    // Attempt initialization with retries
    for (int attempt = 1; attempt <= LoRaConstants::INIT_RETRY_COUNT; attempt++)
    {

        ESP_LOGI(TAG, "Setup attempt %d/%d", attempt, LoRaConstants::INIT_RETRY_COUNT);

#if defined(RADIO_SX1278)
        int state = radio->begin(
            config.frequency,
            config.bandwidth,
            config.spreadingFactor,
            config.codingRate,
            LoRaConstants::SYNC_WORD,
            config.txPower,
            LoRaConstants::PREAMBLE_LENGTH);
#elif defined(RADIO_SX1262)
        int state = radio->begin(
            config.frequency,
            config.bandwidth,
            config.spreadingFactor,
            config.codingRate,
            LoRaConstants::SYNC_WORD,
            config.txPower,
            LoRaConstants::PREAMBLE_LENGTH,
            LoRaConstants::TCXO_VOLTAGE,
            false);
#else
#error "No supported RADIO defined! Please define RADIO_SX1278 or RADIO_SX1262 in platformio.ini"
#endif
        int res = radio->setCurrentLimit(140);
        if (res != RADIOLIB_ERR_NONE)
        {
            ESP_LOGE(TAG, "Failed to set current limit, code %d", res);
            return false;
        }

        if (state == RADIOLIB_ERR_NONE)
        {
            // Using RadioLib default preamble (8 symbols)
            // WakeUp messages are now used to wake duty-cycled receivers
            this->state = STATE_IDLE;
            ESP_LOGI(TAG, "Setup successful");
            ESP_LOGI(TAG, "  Frequency: %.2f MHz", config.frequency);
            ESP_LOGI(TAG, "  Bandwidth: %.2f kHz", config.bandwidth);
            ESP_LOGI(TAG, "  Spreading Factor: %d", config.spreadingFactor);
            ESP_LOGI(TAG, "  Coding Rate: 4/%d", config.codingRate);
            ESP_LOGI(TAG, "  TX Power: %d dBm", config.txPower);
            ESP_LOGI(TAG, "  Preamble Length: %d symbols", LoRaConstants::PREAMBLE_LENGTH);
            ESP_LOGI(TAG, "  Sync Word: 0x%02X", LoRaConstants::SYNC_WORD);
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

bool LoRaManager::startReceive(bool dutyCycle)
{
    if (state == STATE_UNINITIALIZED)
    {
        ESP_LOGE(TAG, "Cannot start receive - not initialized");
        return false;
    }

    // SX1278 or duty cycle disabled: Standard continuous receive mode
    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);

#if defined(RADIO_SX1262)
    if (dutyCycle)
    {
        ESP_LOGI(TAG, "Starting duty cycle RX mode");
        int rxState = radio->startReceiveDutyCycleAuto(LoRaConstants::PREAMBLE_LENGTH, 8, (RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1 << RADIOLIB_IRQ_PREAMBLE_DETECTED)));
        if (rxState != RADIOLIB_ERR_NONE)
        {
            ESP_LOGE(TAG, "Failed to start duty cycle RX mode, code %d", rxState);
            return false;
        }
        ESP_LOGI(TAG, "Duty cycle receive mode started");
    }
    else
    {
        int rxState = radio->startReceive();
        if (rxState != RADIOLIB_ERR_NONE)
        {
            ESP_LOGE(TAG, "Failed to start continuous receive mode, code %d", rxState);
            return false;
        }
        ESP_LOGI(TAG, "Continuous receive mode started");
    }

#else
    // SX1278: Standard continuous receive mode
    int rxState = radio->startReceive();
    if (rxState != RADIOLIB_ERR_NONE)
    {
        ESP_LOGE(TAG, "Failed to start continuous receive mode, code %d", rxState);
        return false;
    }
    ESP_LOGI(TAG, "Continuous receive mode started");
#endif
    state = STATE_IDLE;
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

    // Step 1: Send WakeUp message (blocking) to wake duty-cycled receivers
    ESP_LOGI(TAG, "Sending WakeUp message...");
    Message wakeUpMsg = Message::createWakeUp();
    uint8_t wakeUpBuf[64];
    int wakeUpLen = wakeUpMsg.serialize(wakeUpBuf, sizeof(wakeUpBuf));

    if (wakeUpLen > 0)
    {
        // Clear RX interrupt temporarily
        radio->clearPacketReceivedAction();

        // Send WakeUp synchronously (blocking)
        int wakeUpState = radio->transmit(wakeUpBuf, wakeUpLen);

        if (wakeUpState != RADIOLIB_ERR_NONE)
        {
            ESP_LOGW(TAG, "WakeUp transmission failed, code %d - continuing anyway", wakeUpState);
        }
        else
        {
            ESP_LOGI(TAG, "WakeUp sent successfully");
        }

        // Wait for receiver to wake up and switch to continuous RX
        vTaskDelay(pdMS_TO_TICKS(LoRaConstants::WAKEUP_TO_MESSAGE_DELAY_MS));
    }
    else
    {
        ESP_LOGW(TAG, "Failed to serialize WakeUp message");
    }

    // Step 2: Send actual message (non-blocking)
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
        startReceive();
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
        ESP_LOGI(TAG, "TX complete, restoring RX mode");

        // Return to receive mode
        startReceive();
        state = STATE_IDLE;

        // Invoke transmit callback
        if (transmitCallback)
        {
            transmitCallback(true);
        }

        ESP_LOGI(TAG, "Now in RX mode (state=%d)", (int)state);
        return; // Return to allow next iteration to check for any pending RX
    }

    // Check for received packets
    if (state != STATE_PACKET_RECEIVED)
    {
        return;
    }

    rxProcessedCount++;

    ESP_LOGI(TAG, "RX packet detected, processing");

    // Immediately set to processing to avoid race condition
    state = STATE_IDLE;

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

    // Note: No need to restart receive mode here - radio remains in RX mode after readData()
    // If the callback starts a transmission, startTransmit() will handle the mode switch
    ESP_LOGI(TAG, "RX packet processing complete (radio still in RX mode)");
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
        instance->state = STATE_PACKET_RECEIVED;
    }
}

void IRAM_ATTR LoRaManager::onTransmitISR()
{
    if (instance)
    {
        // Set state - do NOT perform cleanup in ISR
        // Cleanup happens in process() called from main loop
        instance->state = STATE_PACKET_SENT;
    }
}
