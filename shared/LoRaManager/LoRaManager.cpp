#include "LoRaManager.h"
#include <SPI.h>
#include <RadioLib.h>

// Define the static instance pointer declared in the header.
LoRaManager *LoRaManager::s_instance = nullptr;

LoRaManager::LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, long freq) noexcept
    : sckPin(sck), misoPin(miso), mosiPin(mosi), ssPin(ss), rstPin(rst), dio0Pin(dio0), frequency(freq), module(nullptr), radio(nullptr)
{
}

int LoRaManager::getTransmissionState() const noexcept
{
    return transmissionState;
}

bool LoRaManager::isRxPending() const noexcept
{
    return rxFlag;
}

LoRaManager::~LoRaManager()
{
    shutdown();
}

void LoRaManager::shutdown() noexcept
{
    // Make sure ISR routing is cleared
    if (radio)
    {
        radio->setPacketReceivedAction(nullptr);
        radio->setPacketSentAction(nullptr);
    }

    // Free objects if allocated
    if (radio)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
        delete radio;
#pragma GCC diagnostic pop
        radio = nullptr;
    }
    if (module)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
        delete module;
#pragma GCC diagnostic pop
        module = nullptr;
    }

    // Clear static instance pointer if it pointed to this
    if (s_instance == this)
    {
        s_instance = nullptr;
    }
}

bool LoRaManager::setup()
{
    // Initialize SPI and RadioLib module
    SPI.begin(sckPin, misoPin, mosiPin, ssPin);

    // Allocate Module and SX1278 objects
    module = new Module(ssPin, dio0Pin, rstPin);
    radio = new SX1278(module);

    int state = radio->begin(frequency, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODING_RATE, 0x12, LORA_TX_POWER, 20, 1);
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.print(F("LoRa (RadioLib) init failed, code: "));
        Serial.println(state);
        return false;
    }

    // Keep CRC disabled to match existing behavior in the project
    radio->setCRC(false);

    Serial.println(F("LoRa (RadioLib) initialized successfully."));
    return true;
}

int LoRaManager::startTransmitNonBlocking(const uint8_t *buffer, size_t length)
{
    if (!radio)
    {
        Serial.println(F("LoRa: startTransmitNonBlocking called before initialization"));
        return RADIOLIB_ERR_UNKNOWN;
    }

    // Register instance and ISR-safe callback
    s_instance = this;
    radio->setPacketSentAction(LoRaManager::onTxDoneStatic);

    int state = radio->startTransmit(buffer, length);
    transmissionState = state;
    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println(F("LoRa: started non-blocking transmit"));
    }
    else
    {
        Serial.print(F("LoRa: startTransmit failed, code: "));
        Serial.println(state);
    }
    return state;
}

void LoRaManager::finishTransmit() noexcept
{
    if (!radio)
        return;
    // finishTransmit may perform SPI; must be called outside ISR
    radio->finishTransmit();
    Serial.println(F("LoRa: finishTransmit called"));
}

void LoRaManager::startReceiveMode() noexcept
{
    if (!radio)
        return;
    // Register internal minimal ISR to set rxFlag when a packet arrives
    radio->setPacketReceivedAction(LoRaManager::onRxStatic);
    int16_t st = radio->startReceive();
    if (st == RADIOLIB_ERR_NONE)
    {
        Serial.println(F("LoRa: receive mode started"));
    }
    else
    {
        Serial.print(F("LoRa: startReceive failed, code: "));
        Serial.println(st);
    }
}

void LoRaManager::onReceive(void (*callback)(void)) noexcept
{
    if (!radio)
        return;
    radio->setPacketReceivedAction(callback);
}

void LoRaManager::setPacketSentAction(void (*callback)(void)) noexcept
{
    if (!radio)
        return;
    radio->setPacketSentAction(callback);
}

bool LoRaManager::consumeTxDoneFlag() noexcept
{
    if (txDoneFlag)
    {
        txDoneFlag = false;
        Serial.println(F("LoRa: TX done flag consumed"));
        return true;
    }
    return false;
}

LoRaPacket LoRaManager::getPacketData() noexcept
{
    LoRaPacket p{};
    if (!radio)
        return p;

    p.len = radio->readData(p.buffer, sizeof(p.buffer));
    p.rssi = radio->getRSSI();
    p.snr = radio->getSNR();
    return p;
}

String LoRaManager::getConfigurationString() const noexcept
{
    String cfg = "LoRa (RadioLib) Configuration:\n";
    cfg += "  Frequency: " + String(frequency / 1000000.0, 2) + " MHz\n";
    cfg += "  Bandwidth: " + String(LORA_BANDWIDTH / 1000.0, 1) + " kHz\n";
    cfg += "  Spreading Factor: " + String(LORA_SPREADING_FACTOR) + "\n";
    cfg += "  Coding Rate: 4/" + String(LORA_CODING_RATE) + "\n";
    cfg += "  TX Power: " + String(LORA_TX_POWER) + " dBm\n";
    return cfg;
}

// RadioLib ISR-friendly static callback
#if defined(ESP8266) || defined(ESP32)
IRAM_ATTR
#endif
void LoRaManager::onTxDoneStatic() noexcept
{
    if (s_instance)
    {
        s_instance->txDoneFlag = true;
    }
}

#if defined(ESP8266) || defined(ESP32)
IRAM_ATTR
#endif
void LoRaManager::onRxStatic() noexcept
{
    if (s_instance)
    {
        s_instance->rxFlag = true;
    }
}

bool LoRaManager::consumeRxFlag() noexcept
{
    if (rxFlag)
    {
        rxFlag = false;
        return true;
    }
    return false;
}
