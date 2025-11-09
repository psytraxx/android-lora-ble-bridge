#include "LoRaManager.h"
#include "FirmwareConfig.h"
#include "esp_log.h"
#include "Esp32S3Hal.h"
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
      rxInterruptCount(0),
      txInterruptCount(0),
      rxProcessedCount(0),
      txProcessedCount(0),
      receiveCallback(nullptr),
      transmitCallback(nullptr)
{
    // Set singleton instance for ISR access
    instance = this;

    EspS3Hal *hal = new EspS3Hal(pinSCK, pinMISO, pinMOSI);

    // Create RadioLib module instance (stored as base class pointer for polymorphism)
#if defined(RADIO_SX1278)
    radio = new SX1278(new Module(hal, pinSS, pinDIO0, pinRST));
#elif defined(RADIO_SX1262)
    radio = new SX1262(new Module(hal, pinSS, pinDIO0, pinRST, pinBusy));
#else
#error "No supported RADIO defined! Please define RADIO_SX1278 or RADIO_SX1262 in platformio.ini"
#endif
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

        if (state == RADIOLIB_ERR_NONE)
        {
            // Set preamble length for wake-on-radio (Option 3: Power Optimized)
            int preambleErr = radio->setPreambleLength(LoRaConstants::PREAMBLE_LENGTH);
            if (preambleErr != RADIOLIB_ERR_NONE)
            {
                ESP_LOGW(TAG, "Failed to set preamble length: %d", preambleErr);
            }
            else
            {
                // Calculate preamble duration: symbols × symbol_time_ms
                // Symbol time @ SF11/BW250: (2^11) / 250000 = 8.192 ms
                uint32_t preambleDurationMs = (LoRaConstants::PREAMBLE_LENGTH * 8192) / 1000;
                ESP_LOGI(TAG, "  Preamble: %d symbols (~%d ms)",
                         LoRaConstants::PREAMBLE_LENGTH, preambleDurationMs);
            }

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

    // Clear any previous interrupt handlers
    radio->clearPacketSentAction();

    // Set receive interrupt handler
    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);

    // Start continuous RX mode for reliable packet reception
    ESP_LOGI(TAG, "Starting continuous RX mode (~12mA)");
    int rxState = radio->startReceive();
    if (rxState != RADIOLIB_ERR_NONE)
    {
        ESP_LOGE(TAG, "Failed to start continuous receive mode, code %d", rxState);
        return false;
    }

    state = STATE_IDLE;
    return true;
}

bool LoRaManager::configureForDeepSleepWake()
{
    if (state == STATE_UNINITIALIZED)
    {
        ESP_LOGE(TAG, "Cannot configure deep sleep - not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Configuring radio for deep sleep wake-on-radio");

    // Clear any previous interrupt handlers
    radio->clearPacketSentAction();

#if defined(RADIO_SX1262)
    // SX1262: Configure for preamble detection wake
    ESP_LOGI(TAG, "Enabling preamble detection IRQ for early wake");

    // Start continuous RX mode (autonomous duty cycle won't work during deep sleep)
    int rxState = radio->startReceive();
    if (rxState != RADIOLIB_ERR_NONE)
    {
        ESP_LOGE(TAG, "Failed to start continuous RX for deep sleep, code %d", rxState);
        return false;
    }

    // Configure DIO1 to trigger on both preamble detection AND RX done
    // Preamble detection wakes ESP32 early, RX done ensures we catch the packet
    int irqState = radio->setIrqFlags(
        RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED |
        RADIOLIB_SX126X_IRQ_RX_DONE
    );

    if (irqState != RADIOLIB_ERR_NONE)
    {
        ESP_LOGW(TAG, "Failed to set preamble detection IRQ (code %d), using standard RX", irqState);
    }
    else
    {
        ESP_LOGI(TAG, "Preamble detection IRQ configured successfully");
        ESP_LOGI(TAG, "Radio will trigger DIO1 during preamble (~1.5s before packet ends)");
    }

    // Set receive interrupt handler (will be called on preamble OR rx done)
    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);

    ESP_LOGI(TAG, "Radio ready for deep sleep wake (continuous RX mode, ~12mA)");
#else
    // SX1278: Standard continuous RX (no preamble detection support)
    ESP_LOGI(TAG, "SX1278: Using standard continuous RX for deep sleep");

    
    int rxState = radio->startReceive();
    if (rxState != RADIOLIB_ERR_NONE)
    {
        ESP_LOGE(TAG, "Failed to start continuous RX for deep sleep, code %d", rxState);
        return false;
    }

    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);
    ESP_LOGI(TAG, "Radio ready for deep sleep wake (continuous RX mode, ~12mA)");
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

    // Send message with extended preamble (no WakeUp message needed - Protocol v3.2)
    ESP_LOGI(TAG, "Starting transmission of %d bytes (with %d-symbol preamble)",
             len, LoRaConstants::PREAMBLE_LENGTH);

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
        txProcessedCount++;
        ESP_LOGI(TAG, "TX complete (ISR:%lu/Proc:%lu), restoring RX mode",
                 (unsigned long)txInterruptCount, (unsigned long)txProcessedCount);

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

    // Check if we're missing packets
    if (rxInterruptCount > rxProcessedCount)
    {
        ESP_LOGW(TAG, "RX interrupt/process mismatch! ISR:%lu, Proc:%lu (missed %lu)",
                 (unsigned long)rxInterruptCount, (unsigned long)rxProcessedCount,
                 (unsigned long)(rxInterruptCount - rxProcessedCount));
    }

    ESP_LOGI(TAG, "RX packet detected (ISR:%lu/Proc:%lu)",
             (unsigned long)rxInterruptCount, (unsigned long)rxProcessedCount);

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
        // Increment interrupt counter atomically
        uint32_t count = instance->rxInterruptCount;
        instance->rxInterruptCount = count + 1;

        // Set state - do NOT read data in ISR
        // Data reading happens in process() called from main loop
        // If state is already PACKET_RECEIVED, we're missing packets!
        if (instance->state == STATE_PACKET_RECEIVED)
        {
            // Packet not yet processed - this is a problem
            // But we can't log here, so just note it happened
        }
        instance->state = STATE_PACKET_RECEIVED;
    }
}

void IRAM_ATTR LoRaManager::onTransmitISR()
{
    if (instance)
    {
        // Increment interrupt counter atomically
        uint32_t count = instance->txInterruptCount;
        instance->txInterruptCount = count + 1;

        // Set state - do NOT perform cleanup in ISR
        // Cleanup happens in process() called from main loop
        instance->state = STATE_PACKET_SENT;
    }
}
