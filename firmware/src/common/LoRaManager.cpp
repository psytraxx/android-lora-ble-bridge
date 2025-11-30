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
      radio(nullptr),
      state(STATE_UNINITIALIZED),
      receiveCallback(nullptr),
      transmitCallback(nullptr)
{
    // Set singleton instance for ISR access
    instance = this;

    // Create RadioLib module instance (type depends on radio chip)
#if defined(RADIO_SX1278)
    radio = new SX1278(new Module(pinSS, pinDIO0, pinRST));
#elif defined(RADIO_SX1262)
    radio = new SX1262(new Module(pinSS, pinDIO0, pinRST, pinBusy));
#elif defined(RADIO_SX1268)
    radio = new SX1268(new Module(pinSS, pinDIO0, pinRST, pinBusy));
#else
#error "No supported RADIO defined! Please define RADIO_SX1278, RADIO_SX1262, or RADIO_SX1268"
#endif
}

bool LoRaManager::begin()
{
    LOG_I(TAG, "Initializing LoRa radio");

#if defined(ARDUINO_ARCH_ESP32)
    // ESP32: Initialize SPI with custom pins
    SPI.begin(pinSCK, pinMISO, pinMOSI, pinSS);
#endif
    // nRF52: SPI initialization happens elsewhere (in Arduino core)
    for (int attempt = 1; attempt <= LoRaConstants::INIT_RETRY_COUNT; attempt++)
    {
        LOG_I(TAG, "Setup attempt %d/%d", attempt, LoRaConstants::INIT_RETRY_COUNT);

        // Initialize radio based on chip type
        int state;
        // SX12XX: Basic initialization
        state = radio->begin(
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

        if (state == RADIOLIB_ERR_NONE)
        {
            int res = radio->setCRC(true);
            if (res != RADIOLIB_ERR_NONE)
            {
                LOG_E(TAG, "Failed to configure CRC, code %d", res);
                return false;
            }

#if defined(RADIO_SX1262) || defined(RADIO_SX1268)
            // Explicitly set TCXO control via DIO3 (redundant if begin() does it, but safe)
            // This sends the SetDio3AsTcxoCtrl command
            res = radio->setTCXO(LoRaConstants::TCXO_VOLTAGE);
            if (res != RADIOLIB_ERR_NONE)
            {
                LOG_E(TAG, "Failed to configure TCXO, code %d", res);
            }
            else
            {
                LOG_I(TAG, "TCXO configured at %.1fV via DIO3", LoRaConstants::TCXO_VOLTAGE);
            }

            radio->setDio2AsRfSwitch(true);
            LOG_I(TAG, "DIO2 configured as RF switch");

#if defined(LORA_MAX_CURRENT)
            // Set current limit for PA (important for SX126x family)
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

        LOG_E(TAG, "Setup failed, code %d", state);

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

bool LoRaManager::startTransmit(const uint8_t *data, size_t len, bool sendWakeUp)
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

    if (sendWakeUp)
    {
        // Send blocking WakeUp message (blocking) to wake duty-cycled receivers
        LOG_I(TAG, "Sending WakeUp message...");
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
                LOG_W(TAG, "WakeUp transmission failed, code %d - continuing anyway", wakeUpState);
            }
            else
            {
                LOG_I(TAG, "WakeUp sent successfully, %d bytes, seq: %d", wakeUpLen, (int)wakeUpMsg.textData.seq);
            }

            // Wait for receiver to wake up and switch to continuous RX
            // This delay accounts for: WakeUp ToA + Deep Sleep Wake Time + RX Settle + Margin
            int wakeupDelay = LoRaManager::calculateToA_ms(
                                  LoRaConstants::SPREADING_FACTOR,
                                  LoRaConstants::BANDWIDTH,
                                  LoRaConstants::CODING_RATE,
                                  LoRaConstants::PREAMBLE_LENGTH,
                                  1 // WakeUp message is only 1 byte
                                  ) +
                              LoRaConstants::DEEP_SLEEP_WAKE_TIME_MS;
            delay(wakeupDelay);
        }
        else
        {
            LOG_W(TAG, "Failed to serialize WakeUp message");
        }
    }
    else
    {
        LOG_I(TAG, "Skipping WakeUp message before transmission");
    }

    // Send actual message (non-blocking)
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

void LoRaManager::process()
{
    // Check for received packets
    if (state == STATE_PACKET_RECEIVED)
    {
        LOG_I(TAG, "RX packet detected, processing");

        // Read packet data
        LoRaPacket packet;
        packet.len = radio->getPacketLength();
        int rxState = radio->readData(packet.buffer, packet.len);

        if (rxState == RADIOLIB_ERR_NONE)
        {
            packet.rssi = radio->getRSSI();
            packet.snr = radio->getSNR();

            LOG_I(TAG, "Packet received (%d bytes, RSSI: %d dBm, SNR: %.1f dB)",
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
            LOG_I(TAG, "RX packet processing complete, receive mode restarted");
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

        // Allow radio hardware to settle before switching to RX mode
        // This prevents timing issues with rapid TX->RX transitions
        delay(LoRaConstants::RX_SETTLE_TIME_MS);

        // Restart RX mode for all platforms (preferring duty cycle where supported)
        startReceive(true);
        state = STATE_IDLE;
        LOG_I(TAG, "Now in RX mode");
        return;
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
