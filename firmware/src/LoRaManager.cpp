#include "LoRaManager.h"
#include <Arduino.h>
#include <SPI.h>

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
#if defined(ARDUINO_ARCH_NRF52)
      txCompleteTime(0),
#endif
      rxInterruptCount(0),
      txInterruptCount(0),
      rxProcessedCount(0),
      txProcessedCount(0),
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

bool LoRaManager::begin(const LoRaConfig &config)
{
    Serial.println("Initializing LoRa radio");

#if defined(ARDUINO_ARCH_ESP32)
    // ESP32: Initialize SPI with custom pins
    SPI.begin(pinSCK, pinMISO, pinMOSI, pinSS);
#endif
    // nRF52: SPI initialization happens elsewhere (in Arduino core)

    // Attempt initialization with retries
    // Note: LoRaConstants will be defined in FirmwareConfig (user will fix)
    const int INIT_RETRY_COUNT = 3;
    const int INIT_RETRY_DELAY_MS = 1000;

    for (int attempt = 1; attempt <= INIT_RETRY_COUNT; attempt++)
    {
        Serial.print("Setup attempt ");
        Serial.print(attempt);
        Serial.print("/");
        Serial.println(INIT_RETRY_COUNT);

        // Initialize radio based on chip type
        int state;
#if defined(RADIO_SX1278)
        // SX1278: Basic initialization (no TCXO)
        const uint8_t SYNC_WORD = 0x12;
        const uint8_t PREAMBLE_LENGTH = 8;
        state = radio->begin(
            config.frequency,
            config.bandwidth,
            config.spreadingFactor,
            config.codingRate,
            SYNC_WORD,
            config.txPower,
            PREAMBLE_LENGTH);
#elif defined(RADIO_SX1262) || defined(RADIO_SX1268)
        // SX1262/SX1268: Extended initialization with TCXO
        const uint8_t SYNC_WORD = 0x12;
        const uint8_t PREAMBLE_LENGTH = 8;
        const float TCXO_VOLTAGE = 1.8;
        const bool USE_DIO2_AS_RF_SWITCH = false;
        state = radio->begin(
            config.frequency,
            config.bandwidth,
            config.spreadingFactor,
            config.codingRate,
            SYNC_WORD,
            config.txPower,
            PREAMBLE_LENGTH,
            TCXO_VOLTAGE,
            USE_DIO2_AS_RF_SWITCH);
#endif

        if (state == RADIOLIB_ERR_NONE)
        {
#if defined(RADIO_SX1262) || defined(RADIO_SX1268)
            // Set current limit for PA (important for SX126x family)
            int res = radio->setCurrentLimit(140);
            if (res != RADIOLIB_ERR_NONE)
            {
                Serial.print("Failed to set current limit, code ");
                Serial.println(res);
                return false;
            }
#endif

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
            return true;
        }

        Serial.print("Setup failed, code ");
        Serial.println(state);

        if (attempt < INIT_RETRY_COUNT)
        {
            Serial.println("Retrying in 1 second...");
            delay(INIT_RETRY_DELAY_MS);
        }
    }

    Serial.println("Setup failed permanently");
    return false;
}

bool LoRaManager::startReceive(bool dutyCycle)
{
    if (state == STATE_UNINITIALIZED)
    {
        Serial.println("Cannot start receive - not initialized");
        return false;
    }

    // Set receive interrupt handler
    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);

#if defined(RADIO_SX1262) || defined(RADIO_SX1268)
    if (dutyCycle)
    {
        // SX1262/SX1268: Hardware-based duty cycle mode
        Serial.println("Starting duty cycle RX mode");
        const uint8_t PREAMBLE_LENGTH = 8;
        int rxState = radio->startReceiveDutyCycleAuto(
            PREAMBLE_LENGTH,
            8,
            (RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1 << RADIOLIB_IRQ_PREAMBLE_DETECTED)));
        if (rxState != RADIOLIB_ERR_NONE)
        {
            Serial.print("Failed to start duty cycle RX mode, code ");
            Serial.println(rxState);
            return false;
        }
        Serial.println("Duty cycle receive mode started");
    }
    else
    {
#endif
        // Standard continuous receive mode (all radios)
        int rxState = radio->startReceive();
        if (rxState != RADIOLIB_ERR_NONE)
        {
            Serial.print("Failed to start continuous receive mode, code ");
            Serial.println(rxState);
            return false;
        }
        Serial.println("Continuous receive mode started");
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
        Serial.println("Cannot transmit - not initialized");
        return false;
    }

    if (state == STATE_TRANSMITTING)
    {
        Serial.println("Transmission already in progress");
        return false;
    }

    // Step 1: Send WakeUp message (blocking) to wake duty-cycled receivers
    Serial.println("Sending WakeUp message...");
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
            Serial.print("WakeUp transmission failed, code ");
            Serial.print(wakeUpState);
            Serial.println(" - continuing anyway");
        }
        else
        {
            Serial.println("WakeUp sent successfully");
        }

        // Wait for receiver to wake up and switch to continuous RX
        const int WAKEUP_TO_MESSAGE_DELAY_MS = 100;
        delay(WAKEUP_TO_MESSAGE_DELAY_MS);
    }
    else
    {
        Serial.println("Failed to serialize WakeUp message");
    }

    // Step 2: Send actual message (non-blocking)
    Serial.print("Starting transmission of ");
    Serial.print(len);
    Serial.println(" bytes");

    // Switch to transmit mode with interrupt
    radio->clearPacketReceivedAction();
    radio->setPacketSentAction(LoRaManager::onTransmitISR);

    // Start non-blocking transmission
    state = STATE_TRANSMITTING;
    int txState = radio->startTransmit(const_cast<uint8_t *>(data), len);

    if (txState != RADIOLIB_ERR_NONE)
    {
        Serial.print("Failed to start transmission, code ");
        Serial.println(txState);
        startReceive();
        state = STATE_IDLE;
        return false;
    }

    Serial.println("Transmission started (non-blocking)");
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
        rxProcessedCount++;
        Serial.println("RX packet detected, processing");

        // Read packet data
        LoRaPacket packet;
        packet.len = radio->getPacketLength();
        int rxState = radio->readData(packet.buffer, packet.len);

        if (rxState == RADIOLIB_ERR_NONE)
        {
            packet.rssi = radio->getRSSI();
            packet.snr = radio->getSNR();

            Serial.print("Packet received (");
            Serial.print(packet.len);
            Serial.print(" bytes, RSSI: ");
            Serial.print(packet.rssi);
            Serial.print(" dBm, SNR: ");
            Serial.print(packet.snr);
            Serial.println(" dB)");

            if (receiveCallback)
            {
                receiveCallback(packet);
            }
        }
        else if (rxState == RADIOLIB_ERR_CRC_MISMATCH)
        {
            Serial.println("CRC error");
        }
        else
        {
            Serial.print("Read failed, code ");
            Serial.println(rxState);
        }

#if defined(ARDUINO_ARCH_ESP32)
        // ESP32: Radio remains in RX mode after readData()
        state = STATE_IDLE;
        Serial.println("RX packet processing complete (radio still in RX mode)");
#elif defined(ARDUINO_ARCH_NRF52)
        // nRF52: Manually restart receive mode
        radio->startReceive();
        state = STATE_IDLE;
        Serial.println("RX packet processing complete, receive mode restarted");
#endif
        return;
    }

    // Check for completed transmission
    if (state == STATE_PACKET_SENT)
    {
        Serial.println("TX complete");
        txProcessedCount++;

        // Invoke transmit callback
        if (transmitCallback)
        {
            transmitCallback(true);
        }

#if defined(ARDUINO_ARCH_ESP32)
        // ESP32: Immediately restart RX
        startReceive();
        state = STATE_IDLE;
        Serial.println("Now in RX mode");
#elif defined(ARDUINO_ARCH_NRF52)
        // nRF52: Wait for radio to settle before restarting RX
        state = STATE_WAITING_FOR_RX_SETTLE;
        txCompleteTime = millis();
        Serial.println("Waiting for RX settle");
#endif
        return;
    }

#if defined(ARDUINO_ARCH_NRF52)
    // nRF52 only: Handle RX settle delay
    if (state == STATE_WAITING_FOR_RX_SETTLE)
    {
        const unsigned long RX_SETTLE_TIME_MS = 50;
        if (millis() - txCompleteTime >= RX_SETTLE_TIME_MS)
        {
            Serial.println("RX settle time elapsed, restarting receive mode");
            radio->startReceive();
            state = STATE_IDLE;
        }
    }
#endif
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
        instance->rxInterruptCount++;
        instance->state = STATE_PACKET_RECEIVED;
    }
}

void LORA_ISR_ATTR LoRaManager::onTransmitISR()
{
    if (instance)
    {
        instance->txInterruptCount++;
        instance->state = STATE_PACKET_SENT;
    }
}
