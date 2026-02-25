#include <common/LoRaManager.h>
#include "common/Logging.h"
#include "common/FirmwareConfig.h"
#include <Arduino.h>
#include <SPI.h>

static const char *TAG = "LoRa";

// Static instance for ISR access
LoRaManager *LoRaManager::instance = nullptr;

LoRaManager::LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, int busy)
    : pinSCK(sck),
      pinMISO(miso),
      pinMOSI(mosi),
      pinSS(ss),
      pinRST(rst),
      pinDIO0(dio0),
      pinBusy(busy),
      module(nullptr),
      radio(nullptr),
      state(STATE_UNINITIALIZED),
      receiveCallback(nullptr),
      transmitCallback(nullptr),
      currentTxEntry_(nullptr),
      csmaBackoffCount_(0)
{
    instance = this;

    module = new Module(pinSS, pinDIO0, pinRST, pinBusy);

#if defined(RADIO_SX1262)
    radio = new SX1262(module);
#elif defined(RADIO_SX1268)
    radio = new SX1268(module);
#else
#error "No supported RADIO defined! Please define RADIO_SX1262, or RADIO_SX1268"
#endif
}

void LoRaManager::initSPI()
{
#if defined(ARDUINO_ARCH_ESP32)
    SPI.begin(pinSCK, pinMISO, pinMOSI, pinSS);
#elif defined(ARDUINO_ARCH_NRF52)
    SPI.begin();
#endif
}

bool LoRaManager::begin()
{
    LOG_I(TAG, "Initializing LoRa radio");

    initSPI();

    for (int attempt = 1; attempt <= LoRaConstants::INIT_RETRY_COUNT; attempt++)
    {
        LOG_I(TAG, "Setup attempt %d/%d", attempt, LoRaConstants::INIT_RETRY_COUNT);

        int initResult;
        initResult = radio->begin(
            LoRaConstants::FREQUENCY,
            LoRaConstants::BANDWIDTH,
            LoRaConstants::SPREADING_FACTOR,
            LoRaConstants::CODING_RATE,
            LoRaConstants::SYNC_WORD,
            LORA_TX_POWER,
            LoRaConstants::PREAMBLE_LENGTH);

        this->state = STATE_IDLE;
        LOG_I(TAG, "LoRa setup successful");
        LOG_I(TAG, "  Frequency: %.2f MHz", LoRaConstants::FREQUENCY);
        LOG_I(TAG, "  Bandwidth: %.1f kHz", LoRaConstants::BANDWIDTH);
        LOG_I(TAG, "  Spreading Factor: %d", LoRaConstants::SPREADING_FACTOR);
        LOG_I(TAG, "  Coding Rate: 4/%d", LoRaConstants::CODING_RATE);
        LOG_I(TAG, "  TX Power: %d dBm", LORA_TX_POWER);
        LOG_I(TAG, "  Preamble Length: %d symbols", LoRaConstants::PREAMBLE_LENGTH);

        if (initResult == RADIOLIB_ERR_NONE)
        {
            int res = radio->setCRC(true);
            if (res != RADIOLIB_ERR_NONE)
            {
                LOG_E(TAG, "Failed to configure CRC, code %d", res);
                return false;
            }
            else
            {
                LOG_I(TAG, "CRC enabled");
            }

#if defined(RADIO_SX1262) || defined(RADIO_SX1268)
            res = radio->setTCXO(LoRaConstants::TCXO_VOLTAGE);
            if (res != RADIOLIB_ERR_NONE)
            {
                LOG_E(TAG, "Failed to configure TCXO, code %d", res);
            }
            else
            {
                LOG_I(TAG, "TCXO configured at %.1fV via DIO3", LoRaConstants::TCXO_VOLTAGE);
            }

            // OPTIONAL: Enable DC-DC switching regulator for lower idle current.
            // Non-fatal: falls back to LDO mode if unsupported or unavailable.
            res = radio->setRegulatorDCDC();
            if (res != RADIOLIB_ERR_NONE)
            {
                LOG_E(TAG, "Failed to set DC-DC regulator, code %d", res);
            }
            else
            {
                LOG_I(TAG, "DC-DC regulator enabled");
            }

            // OPTIONAL: Enable boosted RX gain mode (+2–3 dBm sensitivity).
            // Non-fatal: standard gain mode is used if unsupported.
            res = radio->setRxBoostedGainMode(true);
            if (res != RADIOLIB_ERR_NONE)
            {
                LOG_E(TAG, "Failed to enable boosted RX gain, code %d", res);
            }
            else
            {
                LOG_I(TAG, "Boosted RX gain mode enabled");
            }

            radio->setDio2AsRfSwitch(true);
            LOG_I(TAG, "DIO2 configured as RF switch");

#if defined(LORA_MAX_CURRENT)
            res = radio->setCurrentLimit(LORA_MAX_CURRENT);
            if (res != RADIOLIB_ERR_NONE)
            {
                LOG_E(TAG, "Failed to set current limit, code %d", res);
                return false;
            }
            else
            {
                LOG_I(TAG, "PA current limit set to %d mA", LORA_MAX_CURRENT);
            }
#endif

#endif

            return true;
        }

        LOG_E(TAG, "Setup failed, code %d", initResult);

        if (attempt < LoRaConstants::INIT_RETRY_COUNT)
        {
            LOG_I(TAG, "Retrying in %d ms...", LoRaConstants::INIT_RETRY_DELAY_MS);
            delay(LoRaConstants::INIT_RETRY_DELAY_MS);
        }
    }

    LOG_I(TAG, "Setup failed permanently");
    return false;
}

bool LoRaManager::startReceive(bool dutyCycle)
{
    if (state == STATE_UNINITIALIZED)
    {
        LOG_I(TAG, "Cannot start receive - not initialized");
        return false;
    }

    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);

#if defined(RADIO_SX1262) || defined(RADIO_SX1268)
    if (dutyCycle)
    {
        LOG_I(TAG, "Starting duty cycle RX mode");
        int rxState = radio->startReceiveDutyCycleAuto(
            LoRaConstants::PREAMBLE_LENGTH,
            0);
        if (rxState != RADIOLIB_ERR_NONE)
        {
            LOG_E(TAG, "Failed to start duty cycle RX mode, code %d", rxState);
            return false;
        }
        LOG_I(TAG, "Duty cycle receive mode started");
    }
    else
    {
#endif
        int rxState = radio->startReceive();
        if (rxState != RADIOLIB_ERR_NONE)
        {
            LOG_E(TAG, "Failed to start continuous receive mode, code %d", rxState);
            return false;
        }
        LOG_I(TAG, "Continuous receive mode started");
#if defined(RADIO_SX1262) || defined(RADIO_SX1268)
    }
#endif

    state = STATE_IDLE;
    return true;
}

bool LoRaManager::startTransmit(const uint8_t *data, size_t len)
{
    if (state == STATE_UNINITIALIZED)
    {
        LOG_I(TAG, "Cannot transmit - not initialized");
        return false;
    }

    if (state == STATE_TRANSMITTING)
    {
        LOG_I(TAG, "Transmission already in progress");
        return false;
    }

    LOG_I(TAG, "Starting transmission of %d bytes", len);

    radio->clearPacketReceivedAction();
    radio->setPacketSentAction(LoRaManager::onTransmitISR);

    state = STATE_TRANSMITTING;
    int txState = radio->startTransmit(const_cast<uint8_t *>(data), len);

    if (txState != RADIOLIB_ERR_NONE)
    {
        LOG_E(TAG, "Failed to start transmission, code %d", txState);
        startReceive();
        state = STATE_IDLE;
        return false;
    }

    LOG_I(TAG, "Transmission started (non-blocking)");
    return true;
}

// ============================================================================
// Queue-based TX API
// ============================================================================

bool LoRaManager::queueTransmit(const uint8_t *data, size_t len, TxPriority priority,
                                uint32_t fromNode, uint32_t packetId,
                                bool isRelay, uint8_t maxRetries, uint32_t delayMs)
{
    return txQueue_.push(data, len, priority, fromNode, packetId,
                         isRelay, maxRetries, delayMs);
}

int LoRaManager::cancelQueued(uint32_t fromNode, uint32_t packetId)
{
    return txQueue_.cancel(fromNode, packetId);
}

int LoRaManager::cancelQueuedRelays(uint32_t fromNode, uint32_t packetId)
{
    return txQueue_.cancelRelays(fromNode, packetId);
}

void LoRaManager::extendPendingTimers(uint32_t ms)
{
    txQueue_.extendTimers(ms);
}

// ============================================================================
// CSMA/CA Channel Activity Detection
// ============================================================================

bool LoRaManager::isChannelClear()
{
    int result = radio->scanChannel();

    if (result == RADIOLIB_CHANNEL_FREE)
    {
        return true;
    }
    else if (result == RADIOLIB_PREAMBLE_DETECTED)
    {
        LOG_D(TAG, "CSMA: channel busy (preamble detected)");
        return false;
    }
    else
    {
        // scanChannel error — treat as clear to avoid permanent stall
        LOG_W(TAG, "CSMA: scanChannel error %d, treating as clear", result);
        return true;
    }
}

// ============================================================================
// Callbacks
// ============================================================================

void LoRaManager::setReceiveCallback(LoRaReceiveCallback callback)
{
    receiveCallback = callback;
}

void LoRaManager::setTransmitCallback(LoRaTransmitCallback callback)
{
    transmitCallback = callback;
}

// ============================================================================
// Main Process Loop
// ============================================================================

void LoRaManager::process()
{
    // Handle received packets
    if (state == STATE_PACKET_RECEIVED)
    {
        LOG_D(TAG, "RX packet detected, processing");

        LoRaPacket packet;
        packet.len = radio->getPacketLength();
        int rxState = radio->readData(packet.buffer, packet.len);

        if (rxState == RADIOLIB_ERR_NONE)
        {
            packet.rssi = radio->getRSSI();
            packet.snr = radio->getSNR();

            LOG_D(TAG, "Packet received (%d bytes, RSSI: %d dBm, SNR: %.1f dB)",
                  packet.len, packet.rssi, packet.snr);

            if (receiveCallback)
            {
                receiveCallback(packet);
            }
        }
        else if (rxState == RADIOLIB_ERR_CRC_MISMATCH)
        {
            LOG_I(TAG, "CRC error");
        }
        else
        {
            LOG_E(TAG, "Read failed, code %d", rxState);
        }

        // Restart RX mode if callback didn't start a transmission
        if (state == STATE_PACKET_RECEIVED)
        {
            startReceive(true);
            state = STATE_IDLE;
            LOG_D(TAG, "RX packet processing complete, receive mode restarted");
        }
        else if (state == STATE_TRANSMITTING)
        {
            LOG_I(TAG, "RX packet processing complete, transmission queued");
        }
        return;
    }

    // Handle completed transmission
    if (state == STATE_PACKET_SENT)
    {
        LOG_I(TAG, "TX complete");

        if (transmitCallback)
        {
            transmitCallback(true);
        }

        // Check if current entry needs retry
        if (currentTxEntry_ && currentTxEntry_->active)
        {
            if (currentTxEntry_->maxRetries > 0 &&
                txQueue_.requeueForRetry(currentTxEntry_, RetryConstants::BASE_RETRY_INTERVAL_MS))
            {
                // Entry stays in queue for retry
                LOG_D(TAG, "Entry requeued for retry");
            }
            else
            {
                // Done with this entry (no retries or max reached)
                txQueue_.pop(currentTxEntry_);
            }
        }
        currentTxEntry_ = nullptr;

        delay(LoRaConstants::RX_SETTLE_TIME_MS);
        startReceive(true);
        state = STATE_IDLE;
        LOG_I(TAG, "Now in RX mode");
        return;
    }

    // Dequeue next TX packet when idle
    if (state == STATE_IDLE)
    {
        uint32_t now = millis();
        TxQueueEntry *entry = txQueue_.peekReady(now);

        if (entry != nullptr)
        {
            // CSMA/CA: check channel before transmitting
            if (!isChannelClear())
            {
                // Exponential backoff
                uint32_t maxSlots = 1u << min(csmaBackoffCount_, CSMAConstants::MAX_BACKOFF_ATTEMPTS);
                uint32_t backoffMs = random(0, maxSlots) * CSMAConstants::SLOT_TIME_MS;
                backoffMs = min(backoffMs, CSMAConstants::MAX_BACKOFF_MS);

                entry->earliestTxTime = now + backoffMs;
                csmaBackoffCount_++;

                LOG_D(TAG, "CSMA backoff #%u: %lums",
                      csmaBackoffCount_, (unsigned long)backoffMs);

                // Restart RX since scanChannel() leaves radio in standby
                startReceive(true);
                return;
            }

            // Channel clear — transmit
            csmaBackoffCount_ = 0;
            currentTxEntry_ = entry;

            if (!startTransmit(entry->packet, entry->length))
            {
                LOG_W(TAG, "TX dequeue failed, discarding entry");
                txQueue_.pop(entry);
                currentTxEntry_ = nullptr;
            }
        }
    }
}

int LoRaManager::getRSSI() const
{
    if (state == STATE_UNINITIALIZED || !radio)
        return 0;
    return radio->getRSSI();
}

float LoRaManager::getSNR() const
{
    if (state == STATE_UNINITIALIZED || !radio)
        return 0.0f;
    return radio->getSNR();
}

bool LoRaManager::handleSleepWakeup()
{
    LOG_I(TAG, "Resuming from deep sleep (LoRa wakeup)");

    initSPI();

    module->init();

    module->spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_ADDR] = Module::BITS_16;
    module->spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = Module::BITS_8;
    module->spiConfig.statusPos = 1;
    module->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_READ] = RADIOLIB_SX126X_CMD_READ_REGISTER;
    module->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_WRITE] = RADIOLIB_SX126X_CMD_WRITE_REGISTER;
    module->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_NOP] = RADIOLIB_SX126X_CMD_NOP;
    module->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_STATUS] = RADIOLIB_SX126X_CMD_GET_STATUS;
    module->spiConfig.stream = true;

    pinMode(pinDIO0, INPUT);

    LoRaPacket packet;
    size_t len = radio->getPacketLength();
    packet.len = len;

    if (packet.len > 0 && packet.len <= 255)
    {
        LOG_I(TAG, "Detected pending packet: %d bytes", packet.len);

        int readState = radio->readData(packet.buffer, packet.len);

        if (readState == RADIOLIB_ERR_NONE)
        {
            packet.rssi = radio->getRSSI();
            packet.snr = radio->getSNR();

            LOG_I(TAG, "Wakeup packet received (%d bytes, RSSI: %d dBm, SNR: %.1f dB)",
                  packet.len, packet.rssi, packet.snr);

            begin();

            if (receiveCallback)
            {
                receiveCallback(packet);
            }
            return true;
        }
        else
        {
            LOG_E(TAG, "Failed to read wakeup packet, code %d", readState);
        }
    }
    else if (packet.len > 255)
    {
        LOG_W(TAG, "Invalid packet length detected: %d (likely uninitialized state)", packet.len);
    }
    else
    {
        LOG_W(TAG, "Wakeup triggered but no packet length detected");
    }

    return false;
}

// ISR handlers
void LORA_ISR_ATTR LoRaManager::onReceiveISR()
{
    if (instance)
    {
        instance->state = STATE_PACKET_RECEIVED;
    }
}

void LORA_ISR_ATTR LoRaManager::onTransmitISR()
{
    if (instance)
    {
        instance->state = STATE_PACKET_SENT;
    }
}
