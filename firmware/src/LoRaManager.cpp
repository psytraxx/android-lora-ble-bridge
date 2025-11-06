#include "LoRaManager.h"
#include "FirmwareConfig.h"
#include <Arduino.h>

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
    SPI.begin(pinSCK, pinMISO, pinMOSI, pinSS);

    Serial.println("Initializing radio");
    // Attempt initialization with retries
    for (int attempt = 1; attempt <= LoRaConstants::INIT_RETRY_COUNT; attempt++)
    {

        Serial.printf("Setup attempt %d/%d\n", attempt, LoRaConstants::INIT_RETRY_COUNT);

        int state = radio->begin(
            config.frequency,
            config.bandwidth,
            config.spreadingFactor,
            config.codingRate,
            config.syncWord,
            config.txPower);

        if (state == RADIOLIB_ERR_NONE)
        {
            // Using RadioLib default preamble (8 symbols)
            // WakeUp messages are now used to wake duty-cycled receivers
            this->state = STATE_IDLE;
            Serial.println("Setup successful");
            Serial.printf("  Frequency: %.2f MHz\n", config.frequency);
            Serial.printf("  Bandwidth: %.2f kHz\n", config.bandwidth);
            Serial.printf("  Spreading Factor: %d\n", config.spreadingFactor);
            Serial.printf("  Coding Rate: 4/%d\n", config.codingRate);
            Serial.printf("  TX Power: %d dBm\n", config.txPower);
            return true;
        }

        Serial.printf("Setup failed, code %d\n", state);

        if (attempt < LoRaConstants::INIT_RETRY_COUNT)
        {
            Serial.println("Retrying in 1 second...");
            delay(LoRaConstants::INIT_RETRY_DELAY_MS);
        }
    }

    Serial.println("Setup failed permanently");
    return false;
}

bool LoRaManager::startReceive()
{
    if (state == STATE_UNINITIALIZED)
    {
        Serial.println("Cannot start receive - not initialized");
        return false;
    }
    
    // SX1278 or duty cycle disabled: Standard continuous receive mode
    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);
    int rxState = radio->startReceive();
    if (rxState != RADIOLIB_ERR_NONE)
    {
        Serial.printf("Failed to start continuous receive mode, code %d", rxState);
        return false;
    }
    Serial.printf("Continuous receive mode started");
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
            Serial.printf("WakeUp transmission failed, code %d - continuing anyway\n", wakeUpState);
        }
        else
        {
            Serial.println("WakeUp sent successfully");
        }

        // Wait for receiver to wake up and switch to continuous RX
        vTaskDelay(pdMS_TO_TICKS(LoRaConstants::WAKEUP_TO_MESSAGE_DELAY_MS));
    }
    else
    {
        Serial.println("Failed to serialize WakeUp message");
    }

    // Step 2: Send actual message (non-blocking)
    Serial.printf("Starting transmission of %d bytes\n", len);

    // Switch to transmit mode with interrupt
    radio->clearPacketReceivedAction();
    radio->setPacketSentAction(LoRaManager::onTransmitISR);

    // Start non-blocking transmission
    state = STATE_TRANSMITTING;
    int txState = radio->startTransmit(const_cast<uint8_t *>(data), len);

    if (txState != RADIOLIB_ERR_NONE)
    {
        Serial.printf("Failed to start transmission, code %d\n", txState);
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
    // Check for completed transmission
    if (state == STATE_PACKET_SENT)
    {
        txProcessedCount++;
        Serial.printf("TX complete (ISR:%lu/Proc:%lu), restoring RX mode\n",
                      (unsigned long)txInterruptCount, (unsigned long)txProcessedCount);

        // Return to receive mode
        startReceive();
        state = STATE_IDLE;

        // Invoke transmit callback
        if (transmitCallback)
        {
            transmitCallback(true);
        }

        Serial.printf("Now in RX mode (state=%d)\n", (int)state);
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
        Serial.printf("RX interrupt/process mismatch! ISR:%lu, Proc:%lu (missed %lu)\n",
                      (unsigned long)rxInterruptCount, (unsigned long)rxProcessedCount,
                      (unsigned long)(rxInterruptCount - rxProcessedCount));
    }

    Serial.printf("RX packet detected (ISR:%lu/Proc:%lu)\n",
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

        Serial.printf("Packet received (%d bytes, RSSI: %d dBm, SNR: %.2f dB)\n",
                      packet.len, packet.rssi, packet.snr);

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
        Serial.printf("Read failed, code %d\n", rxState);
    }

    // Note: No need to restart receive mode here - radio remains in RX mode after readData()
    // If the callback starts a transmission, startTransmit() will handle the mode switch
    Serial.println("RX packet processing complete (radio still in RX mode)");
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
