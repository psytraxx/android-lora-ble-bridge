#include "LoRaManager.h"
#include <SPI.h>

// Static instance for ISR access
LoRaManager *LoRaManager::instance = nullptr;

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

    // Create RadioLib module instance
    radio = new SX1278(new Module(pinSS, pinDIO0, pinRST, RADIOLIB_NC));
}

bool LoRaManager::begin(const LoRaConfig &config, int retryCount)
{
    Serial.println("LoRaManager: Initializing radio");

    // Initialize SPI bus
    SPI.begin(pinSCK, pinMISO, pinMOSI, pinSS);

    // Attempt initialization with retries
    for (int attempt = 1; attempt <= retryCount; attempt++)
    {
        Serial.print("LoRaManager: Setup attempt ");
        Serial.print(attempt);
        Serial.print("/");
        Serial.println(retryCount);

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
            Serial.println("LoRaManager: Setup successful");
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

        Serial.print("LoRaManager: Setup failed, code ");
        Serial.println(state);

        if (attempt < retryCount)
        {
            Serial.println("Retrying in 1 second...");
            delay(1000);
        }
    }

    Serial.println("LoRaManager: Setup failed permanently");
    return false;
}

bool LoRaManager::startReceive()
{
    if (!initialized)
    {
        Serial.println("LoRaManager: Cannot start receive - not initialized");
        return false;
    }

    // Set up interrupt-driven receive
    radio->setPacketReceivedAction(LoRaManager::onReceiveISR);

    int state = radio->startReceive();
    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println("LoRaManager: Continuous receive mode started");
        return true;
    }

    Serial.print("LoRaManager: Failed to start receive mode, code ");
    Serial.println(state);
    return false;
}

bool LoRaManager::transmit(const uint8_t *data, size_t len)
{
    if (!initialized)
    {
        Serial.println("LoRaManager: Cannot transmit - not initialized");
        return false;
    }

    Serial.print("LoRaManager: Transmitting ");
    Serial.print(len);
    Serial.println(" bytes");

    // Clear interrupt handler to allow DIO0 to signal TX completion
    radio->clearPacketReceivedAction();

    // Transmit the data
    int state = radio->transmit(const_cast<uint8_t *>(data), len);

    bool success = (state == RADIOLIB_ERR_NONE);

    if (success)
    {
        Serial.println("LoRaManager: Transmission successful");
    }
    else
    {
        Serial.print("LoRaManager: Transmission failed, code ");
        Serial.println(state);
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

        Serial.print("LoRaManager: Packet received (");
        Serial.print(packet.len);
        Serial.print(" bytes, RSSI: ");
        Serial.print(packet.rssi);
        Serial.print(" dBm, SNR: ");
        Serial.print(packet.snr);
        Serial.println(" dB)");

        // Invoke callback if set
        if (receiveCallback)
        {
            receiveCallback(packet);
        }
    }
    else if (state == RADIOLIB_ERR_CRC_MISMATCH)
    {
        Serial.println("LoRaManager: CRC error");
    }
    else
    {
        Serial.print("LoRaManager: Read failed, code ");
        Serial.println(state);
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

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void LoRaManager::onReceiveISR()
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
    delay(delayMs);
}
