#include "nrf52/LoRaManager.h"
#include "nrf52/FirmwareConfig.h"
#include <Arduino.h>
#include <SPI.h>

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
      rxInterruptCount(0),
      txInterruptCount(0),
      rxProcessedCount(0),
      txProcessedCount(0),
      receiveCallback(nullptr),
      transmitCallback(nullptr)
{
    // Set singleton instance for ISR access
    instance = this;

    // Create RadioLib module instance for SX1262
    radio = new SX1262(new Module(pinSS, pinDIO0, pinRST, pinBusy));
}

bool LoRaManager::begin(const LoRaConfig &config)
{
    Serial.println("Initializing LoRa radio");

    // Configure SPI pins
    SPI.begin();

    // Attempt initialization with retries
    for (int attempt = 1; attempt <= LoRaConstants::INIT_RETRY_COUNT; attempt++)
    {
        Serial.print("Setup attempt ");
        Serial.print(attempt);
        Serial.print("/");
        Serial.println(LoRaConstants::INIT_RETRY_COUNT);

        // Initialize SX1262 with configuration
        int state = radio->begin(
            config.frequency,
            config.bandwidth,
            config.spreadingFactor,
            config.codingRate,
            LoRaConstants::SYNC_WORD,
            config.txPower,
            LoRaConstants::PREAMBLE_LENGTH,
            LoRaConstants::TCXO_VOLTAGE,
            LoRaConstants::USE_DIO2_AS_RF_SWITCH);

        if (state == RADIOLIB_ERR_NONE)
        {
            // Set current limit for PA (important for SX1262)
            int res = radio->setCurrentLimit(140);
            if (res != RADIOLIB_ERR_NONE)
            {
                Serial.print("Failed to set current limit, code ");
                Serial.println(res);
                return false;
            }

            this->state = STATE_IDLE;
            Serial.println("LoRa setup successful");
            Serial.print("  Frequency: ");
            Serial.print(config.frequency);
            Serial.println(" MHz");
            Serial.print("  Bandwidth: ");
            Serial.print(config.bandwidth);
            Serial.println(" kHz");
            Serial.print("  Spreading Factor: ");
            Serial.println(config.spreadingFactor);
            Serial.print("  Coding Rate: 4/");
            Serial.println(config.codingRate);
            Serial.print("  TX Power: ");
            Serial.print(config.txPower);
            Serial.println(" dBm");
            Serial.print("  Preamble: ");
            Serial.print(LoRaConstants::PREAMBLE_LENGTH);
            Serial.println(" symbols");
            return true;
        }

        Serial.print("Setup failed, code ");
        Serial.println(state);

        if (attempt < LoRaConstants::INIT_RETRY_COUNT)
        {
            Serial.println("Retrying in 1 second...");
            delay(LoRaConstants::INIT_RETRY_DELAY_MS);
        }
    }

    Serial.println("LoRa setup failed permanently");
    return false;
}

bool LoRaManager::startReceive(bool dutyCycle)
{
    if (state == STATE_UNINITIALIZED)
    {
        Serial.println("Cannot start receive - not initialized");
        return false;
    }

    // Set interrupt callback
    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);

    int rxState;
    if (dutyCycle)
    {
        Serial.println("Starting duty cycle RX mode");
        rxState = radio->startReceiveDutyCycleAuto(
            LoRaConstants::PREAMBLE_LENGTH,
            8,
            (RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1 << RADIOLIB_IRQ_PREAMBLE_DETECTED)));
    }
    else
    {
        Serial.println("Starting continuous RX mode");
        rxState = radio->startReceive();
    }

    if (rxState != RADIOLIB_ERR_NONE)
    {
        Serial.print("Failed to start receive mode, code ");
        Serial.println(rxState);
        return false;
    }

    Serial.println("Receive mode started");
    return true;
}

bool LoRaManager::startTransmit(const uint8_t *data, size_t len)
{
    if (state == STATE_UNINITIALIZED)
    {
        Serial.println("Cannot transmit - not initialized");
        return false;
    }

    if (state == STATE_TRANSMITTING)
    {
        Serial.println("Already transmitting");
        return false;
    }

    Serial.print("Starting transmission of ");
    Serial.print(len);
    Serial.println(" bytes");

    // Set interrupt callback for transmission complete
    radio->setPacketSentAction(LoRaManager::onTransmitISR);

    // Start non-blocking transmission
    int txState = radio->startTransmit(const_cast<uint8_t *>(data), len);

    if (txState == RADIOLIB_ERR_NONE)
    {
        state = STATE_TRANSMITTING;
        return true;
    }
    else
    {
        Serial.print("Failed to start transmission, code ");
        Serial.println(txState);
        return false;
    }
}

void LoRaManager::process()
{
    // Process received packets
    if (rxInterruptCount > rxProcessedCount)
    {
        Serial.println("Processing received packet");

        // Read packet
        LoRaPacket packet;
        packet.len = radio->getPacketLength();

        int readState = radio->readData(packet.buffer, packet.len);

        if (readState == RADIOLIB_ERR_NONE)
        {
            packet.rssi = radio->getRSSI();
            packet.snr = radio->getSNR();

            Serial.print("Packet received: ");
            Serial.print(packet.len);
            Serial.print(" bytes, RSSI: ");
            Serial.print(packet.rssi);
            Serial.print(" dBm, SNR: ");
            Serial.print(packet.snr);
            Serial.println(" dB");

            if (receiveCallback)
            {
                receiveCallback(packet);
            }

            // Restart receive mode
            radio->startReceive();
        }
        else
        {
            Serial.print("Failed to read packet, code ");
            Serial.println(readState);
        }

        rxProcessedCount = rxInterruptCount;
        state = STATE_IDLE;
    }

    // Process transmission completion
    if (txInterruptCount > txProcessedCount)
    {
        Serial.println("Transmission completed");

        bool success = true; // RadioLib ISR indicates success if called

        if (transmitCallback)
        {
            transmitCallback(success);
        }

        txProcessedCount = txInterruptCount;
        state = STATE_IDLE;

        // Restart receive mode after transmission
        delay(LoRaConstants::RX_SETTLE_TIME_MS);
        radio->startReceive();
    }
}

void LoRaManager::setReceiveCallback(LoRaReceiveCallback callback)
{
    receiveCallback = callback;
}

void LoRaManager::setTransmitCallback(LoRaTransmitCallback callback)
{
    transmitCallback = callback;
}

int LoRaManager::getRSSI() const
{
    if (radio)
    {
        return radio->getRSSI();
    }
    return 0;
}

float LoRaManager::getSNR() const
{
    if (radio)
    {
        return radio->getSNR();
    }
    return 0.0;
}

// ISR handlers
void LoRaManager::onReceiveISR()
{
    if (instance)
    {
        instance->rxInterruptCount++;
        instance->state = STATE_PACKET_RECEIVED;
    }
}

void LoRaManager::onTransmitISR()
{
    if (instance)
    {
        instance->txInterruptCount++;
        instance->state = STATE_PACKET_SENT;
    }
}
