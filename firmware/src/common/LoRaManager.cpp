#include <common/LoRaManager.h>
#include "common/Logging.h"
#include "common/FirmwareConfig.h"
#include <Arduino.h>
#include <SPI.h>

static const char *TAG = "LoRa";

// Platform-specific includes will be handled via FirmwareConfig later
// For now, we'll use conditional compilation for constants

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
      transmitCallback(nullptr)
{
    // Set singleton instance for ISR access
    instance = this;

    // Create RadioLib module instance - keep reference for direct access during wakeup
    module = new Module(pinSS, pinDIO0, pinRST, pinBusy);

    // Create RadioLib radio instance (type depends on radio chip)
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
    // ESP32: Initialize SPI with custom pins
    SPI.begin(pinSCK, pinMISO, pinMOSI, pinSS);
#elif defined(ARDUINO_ARCH_NRF52)
    // nRF52: SPI needs to be reinitialized after System OFF wakeup
    // The Arduino core handles pin configuration, we just need to start SPI
    SPI.begin();
#endif
}

bool LoRaManager::begin()
{
    LOG_I(TAG, "Initializing LoRa radio");

    initSPI();

    // nRF52: SPI initialization happens elsewhere (in Arduino core)
    for (int attempt = 1; attempt <= LoRaConstants::INIT_RETRY_COUNT; attempt++)
    {
        LOG_I(TAG, "Setup attempt %d/%d", attempt, LoRaConstants::INIT_RETRY_COUNT);

        // Initialize radio based on chip type
        int initResult;
        // SX12XX: Basic initialization
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
            // OPTIONAL: Explicitly set TCXO control via DIO3.
            // Non-fatal: hardware may not have an external TCXO; begin() may already
            // configure this. Failure is logged but does not abort initialization.
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
            // REQUIRED: PA current limit protects the SX126x PA from over-current.
            // Fatal: without this limit the PA may operate outside safe ratings.
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

    // Set receive interrupt handler
    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);

#if defined(RADIO_SX1262) || defined(RADIO_SX1268)
    if (dutyCycle)
    {
        // SX1262/SX1268: Hardware-based duty cycle mode
        LOG_I(TAG, "Starting duty cycle RX mode");
        // Use configured preamble length to match sender
        // Pass 0 for minSymbols to let RadioLib calculate it based on Spreading Factor
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
        // Standard continuous receive mode (all radios)
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

    // Switch to transmit mode with interrupt
    radio->clearPacketReceivedAction();
    radio->setPacketSentAction(LoRaManager::onTransmitISR);

    // Start non-blocking transmission
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

void LoRaManager::setReceiveCallback(LoRaReceiveCallback callback)
{
    receiveCallback = callback;
}

void LoRaManager::setTransmitCallback(LoRaTransmitCallback callback)
{
    transmitCallback = callback;
}

bool LoRaManager::queueTransmit(const uint8_t *data, size_t len)
{
    if (len == 0 || len > sizeof(TxPacket::data))
    {
        LOG_E(TAG, "TX queue: invalid packet length %d", len);
        return false;
    }

    if (txQueueCount >= TX_QUEUE_SIZE)
    {
        LOG_W(TAG, "TX queue full, dropping packet");
        return false;
    }

    TxPacket &pkt = txQueue[txQueueTail];
    memcpy(pkt.data, data, len);
    pkt.len = len;
    txQueueTail = (txQueueTail + 1) % TX_QUEUE_SIZE;
    txQueueCount++;

    LOG_I(TAG, "TX queued (%d bytes, queue depth: %d)", len, txQueueCount);
    return true;
}

bool LoRaManager::processTxQueue()
{
    if (txQueueCount == 0 || state != STATE_IDLE)
        return false;

    int16_t result = radio->scanChannel();

    if (result == RADIOLIB_CHANNEL_FREE)
    {
        LOG_I(TAG, "CAD: channel free, transmitting");
        TxPacket &pkt = txQueue[txQueueHead];
        bool ok = startTransmit(pkt.data, pkt.len);
        txQueueHead = (txQueueHead + 1) % TX_QUEUE_SIZE;
        txQueueCount--;
        cadRetries = 0;
        return ok;
    }
    else if (result == RADIOLIB_LORA_DETECTED)
    {
        cadRetries++;
        LOG_I(TAG, "CAD: channel busy (retry %d/%d)", cadRetries, LoRaConstants::CAD_MAX_RETRIES);

        if (cadRetries >= LoRaConstants::CAD_MAX_RETRIES)
        {
            LOG_W(TAG, "CAD: max retries reached, force transmitting");
            TxPacket &pkt = txQueue[txQueueHead];
            bool ok = startTransmit(pkt.data, pkt.len);
            txQueueHead = (txQueueHead + 1) % TX_QUEUE_SIZE;
            txQueueCount--;
            cadRetries = 0;
            return ok;
        }

        // Restart RX since scanChannel() left radio in standby
        startReceive(true);
        return false;
    }
    else
    {
        LOG_E(TAG, "CAD: scan failed with code %d", result);
        // Restart RX since scanChannel() left radio in standby
        startReceive(true);
        return false;
    }
}

void LoRaManager::process()
{
    // Check for received packets
    if (state == STATE_PACKET_RECEIVED)
    {
        LOG_D(TAG, "RX packet detected, processing");

        // Read packet data
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
            // Use duty cycle mode by default for power savings (SX126x)
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

    // Check for completed transmission
    if (state == STATE_PACKET_SENT)
    {
        LOG_I(TAG, "TX complete");

        // Invoke transmit callback
        if (transmitCallback)
        {
            transmitCallback(true);
        }

        // Begin non-blocking settle period before switching to RX mode.
        // Avoids delay() in the main-loop process() call while still providing
        // the hardware settle time required for reliable TX->RX transitions.
        state = STATE_TX_SETTLING;
        txSettleDeadline = millis() + LoRaConstants::RX_SETTLE_TIME_MS;
        return;
    }

    // Wait out the post-TX hardware settle period (non-blocking)
    if (state == STATE_TX_SETTLING)
    {
        if ((uint32_t)millis() >= txSettleDeadline)
        {
            startReceive(true);
            state = STATE_IDLE;
            LOG_I(TAG, "Now in RX mode");
        }
        return;
    }

    // Drain TX queue when idle
    if (state == STATE_IDLE && txQueueCount > 0)
    {
        processTxQueue();
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

    // 1. Initialize SPI
    initSPI();

    // 2. Initialize RadioLib Module (HAL and pins) - this is critical!
    // The Module must be properly initialized before any SPI communication.
    module->init();

    // 3. Configure SPI settings for SX126x protocol
    // Without this, RadioLib sends malformed SPI commands and reads garbage.
    module->spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_ADDR] = Module::BITS_16;
    module->spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = Module::BITS_8;
    module->spiConfig.statusPos = 1;
    module->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_READ] = RADIOLIB_SX126X_CMD_READ_REGISTER;
    module->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_WRITE] = RADIOLIB_SX126X_CMD_WRITE_REGISTER;
    module->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_NOP] = RADIOLIB_SX126X_CMD_NOP;
    module->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_STATUS] = RADIOLIB_SX126X_CMD_GET_STATUS;
    module->spiConfig.stream = true;

    // 4. Set DIO0 as input for interrupt
    pinMode(pinDIO0, INPUT);

    // 5. Read packet from the SX1262's buffer
    // The chip retains its configuration and buffer during duty cycle mode.
    LoRaPacket packet;
    size_t len = radio->getPacketLength();
    packet.len = len;

    if (packet.len > 0 && packet.len <= 255)
    {
        LOG_I(TAG, "Detected pending packet: %d bytes", packet.len);

        // readData reads from the buffer and checks CRC from hardware flags
        int readState = radio->readData(packet.buffer, packet.len);

        if (readState == RADIOLIB_ERR_NONE)
        {
            packet.rssi = radio->getRSSI();
            packet.snr = radio->getSNR();

            LOG_I(TAG, "Wakeup packet received (%d bytes, RSSI: %d dBm, SNR: %.1f dB)",
                  packet.len, packet.rssi, packet.snr);

            // Re-initialize radio to ensure consistent state for ACK/future RX
            // This resets the radio, but we already have the packet.
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
