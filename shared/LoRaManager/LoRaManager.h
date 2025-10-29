#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include <SPI.h>
#include <RadioLib.h>
#include "lora_config.h"

// Struct for LoRa packets with metadata
struct LoRaPacket
{
    uint8_t buffer[256];
    int len;
    int rssi;
    float snr;
};

class LoRaManager
{
public:
    LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, long frequency)
        : sckPin(sck), misoPin(miso), mosiPin(mosi), ssPin(ss), rstPin(rst), dio0Pin(dio0),
          frequency(frequency), module(nullptr), radio(nullptr) {}

    /**
     * @brief Initializes the LoRa module (RadioLib).
     * @return True if the LoRa module was initialized successfully, false otherwise.
     */
    bool setup()
    {
        // initialize SPI with specified pins
        SPI.begin(sckPin, misoPin, mosiPin, ssPin);

        // create Module and SX1278 instances
        // Module arguments: cs, dio0, reset (adjust if your RadioLib version expects a different order)
        module = new Module(ssPin, dio0Pin, rstPin);
        radio = new SX1278(module);

        int state = radio->begin(frequency, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODING_RATE, 0x12, LORA_TX_POWER, 20, 1);
        if (state != RADIOLIB_ERR_NONE)
        {
            Serial.print(F("LoRa (RadioLib) init failed, code: "));
            Serial.println(state);
            return false;
        }

        radio->setCRC(false); // disable CRC for compatibility

        Serial.println("LoRa (RadioLib) initialized successfully.");
        return true;
    }

    /**
     * @brief Sends a packet with the given byte buffer.
     * @param buffer The byte buffer to send.
     * @param length The number of bytes to send from the buffer.
     * @return True if the packet was sent successfully, false otherwise.
     */
    bool sendPacket(const byte *buffer, size_t length)
    {
        if (!radio)
        {
            Serial.println("Radio not initialized.");
            return false;
        }

        int state = radio->startTransmit(buffer, length);
        if (state == RADIOLIB_ERR_NONE)
        {
            Serial.println("Packet sent successfully!");
            return true;
        }
        else
        {
            Serial.print("Failed to send packet, code: ");
            Serial.println(state);
            return false;
        }
    }

    /**
     * @brief Starts continuous receive mode.
     *
     * This function puts the LoRa module in receive mode, listening for incoming packets.
     */
    void startReceiveMode()
    {
        // TODO: this where it fails https://github.com/jgromes/RadioLib/blob/master/examples/SX127x/SX127x_PingPong/SX127x_PingPong.ino
        if (!radio)
            return;
        // startReceive can be used for continuous receive in RadioLib
        int16_t state = radio->startReceive();
        if (state == RADIOLIB_ERR_NONE)
        {
            Serial.println(F("success!"));
        }
        else
        {
            Serial.print(F("failed, code "));
            Serial.println(state);
            while (true)
            {
                delay(10);
            }
        }
    }

    void onReceive(void (*callback)(void))
    {
        if (!radio)
            return;
        // set the packet received action callback
        radio->setPacketReceivedAction(callback);
    }

    LoRaPacket getPacketData()
    {
        if (!radio)
            return LoRaPacket{0};
        LoRaPacket packet;
        packet.len = radio->readData(packet.buffer, sizeof(packet.buffer));
        packet.rssi = radio->getRSSI();
        packet.snr = radio->getSNR();
        return packet;
    }

    /**
     * @brief Returns a string with the current LoRa configuration.
     * @return Configuration string.
     */
    String getConfigurationString() const
    {
        String config = "LoRa (RadioLib) Configuration:\n";
        config += "  Frequency: " + String(frequency / 1000000.0, 2) + " MHz\n";
        config += "  Bandwidth: " + String(LORA_BANDWIDTH / 1000.0, 1) + " kHz\n";
        config += "  Spreading Factor: " + String(LORA_SPREADING_FACTOR) + "\n";
        config += "  Coding Rate: 4/" + String(LORA_CODING_RATE) + "\n";
        config += "  TX Power: " + String(LORA_TX_POWER) + " dBm\n";
        return config;
    }

private:
    int sckPin;
    int misoPin;
    int mosiPin;
    int ssPin;
    int rstPin;
    int dio0Pin;
    long frequency;

    Module *module;
    SX1278 *radio;
};

#endif // LORA_MANAGER_H