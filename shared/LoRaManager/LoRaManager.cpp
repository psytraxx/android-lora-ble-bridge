#include "LoRaManager.h"
#include <SPI.h>
#include <RadioLib.h>
#include <esp_task_wdt.h>

// Define the static instance pointer declared in the header.
LoRaManager *LoRaManager::s_instance = nullptr;

LoRaManager::LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, float freq) noexcept
    : sckPin(sck), misoPin(miso), mosiPin(mosi), ssPin(ss), rstPin(rst), dio0Pin(dio0), frequency(freq), module(nullptr), radio(nullptr)
{
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


bool LoRaManager::sendPacketBlocking(const uint8_t *buffer, size_t length)
{
    if (!radio)
    {
        Serial.println(F("LoRa: sendPacketBlocking called before initialization"));
        return false;
    }

    // Stop any ongoing receive operation before transmitting
    // This is critical - RadioLib needs the radio to be in standby before TX
    Serial.println(F("LoRa: stopping RX mode before TX"));
    radio->standby();

    // Disable watchdog for this task during long blocking transmit
    // At SF11+BW31.25kHz, transmission can take 3-4 seconds, exceeding watchdog timeout
    esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

    // Use RadioLib's blocking transmit method (no interrupts needed)
    Serial.println(F("LoRa: starting blocking transmit"));
    int16_t state = radio->transmit(const_cast<uint8_t*>(buffer), length);

    // Re-enable watchdog after transmission
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());

    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println(F("LoRa: blocking transmit successful"));
        return true;
    }
    else
    {
        Serial.print(F("LoRa: blocking transmit failed, code: "));
        Serial.println(state);
        return false;
    }
}

void LoRaManager::startReceiveMode() noexcept
{
    if (!radio)
        return;

    // DO NOT clear rxFlag here - a packet might have arrived!
    // Let consumeRxFlag() handle clearing it when the packet is processed

    // Register instance for ISR routing
    s_instance = this;

    // Use setPacketReceivedAction as shown in RadioLib examples
    // This triggers when a complete packet is received (not just DIO0)
    radio->setPacketReceivedAction(LoRaManager::onRxStatic);

    int16_t st = radio->startReceive();
    if (st == RADIOLIB_ERR_NONE)
    {
        Serial.println(F("LoRa: receive mode started successfully"));
    }
    else
    {
        Serial.print(F("LoRa: startReceive FAILED, code: "));
        Serial.println(st);
    }
}

LoRaPacket LoRaManager::getPacketData() noexcept
{
    LoRaPacket p{};
    if (!radio)
    {
        Serial.println(F("LoRa: getPacketData called but radio is null"));
        return p;
    }

    // Get packet length first
    size_t availableLength = radio->getPacketLength();
    Serial.printf("LoRa: getPacketLength() returned %d\n", availableLength);

    if (availableLength <= 0 || availableLength > sizeof(p.buffer))
    {
        Serial.printf("LoRa: Invalid packet length: %d\n", availableLength);
        return p;
    }

    // Use RadioLib's readData(uint8_t*, size_t) - this is the proper binary data method
    // Pass the exact length we expect to read
    int16_t state = radio->readData(p.buffer, availableLength);

    // Get RSSI and SNR
    p.rssi = radio->getRSSI();
    p.snr = radio->getSNR();

    Serial.printf("LoRa: readData(buffer, %d) returned code=%d, rssi=%d, snr=%.2f\n",
                  availableLength, state, p.rssi, p.snr);

    // Check return code
    if (state == RADIOLIB_ERR_NONE)
    {
        // Success - set the length
        p.len = availableLength;

        // Print raw buffer
        Serial.print("LoRa: Raw data: ");
        for (size_t i = 0; i < p.len; i++)
        {
            Serial.printf("%02X ", p.buffer[i]);
        }
        Serial.println();
    }
    else
    {
        Serial.printf("LoRa: readData error code: %d\n", state);
        p.len = 0;
    }

    return p;
}

String LoRaManager::getConfigurationString() const noexcept
{
    String cfg = "LoRa (RadioLib) Configuration:\n";
    cfg += "  Frequency: " + String(frequency) + " MHz\n";
    cfg += "  Bandwidth: " + String(LORA_BANDWIDTH) + " kHz\n";
    cfg += "  Spreading Factor: " + String(LORA_SPREADING_FACTOR) + "\n";
    cfg += "  Coding Rate: 4/" + String(LORA_CODING_RATE) + "\n";
    cfg += "  TX Power: " + String(LORA_TX_POWER) + " dBm\n";
    return cfg;
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
