#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include <SPI.h>
#include <LoRa.h>
#include "lora_config.h"

class LoRaManager
{
public:
    LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, long frequency)
        : sckPin(sck), misoPin(miso), mosiPin(mosi), ssPin(ss), rstPin(rst), dio0Pin(dio0), frequency(frequency) {}

    /**
     * @brief Initializes the LoRa module.
     * @return True if the LoRa module was initialized successfully, false otherwise.
     */
    bool setup()
    {
        SPI.begin(sckPin, misoPin, mosiPin, ssPin);
        LoRa.setPins(ssPin, rstPin, dio0Pin);

        // Configure LoRa parameters from lora_config.h
        LoRa.setSignalBandwidth(LORA_BANDWIDTH);
        LoRa.setCodingRate4(LORA_CODING_RATE);
        LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
        LoRa.setTxPower(LORA_TX_POWER);
        LoRa.enableCrc();

        if (!LoRa.begin(frequency))
        {
            Serial.println("LoRa initialization failed!");
            return false;
        }
        Serial.println("LoRa initialized successfully.");
        return true;
    }

    /**
     * @brief Sends a packet with the given byte buffer.
     * @param buffer The byte buffer to send.
     * @param length The number of bytes to send from the buffer.
     */
    void sendPacket(const byte *buffer, size_t length)
    {
        LoRa.beginPacket();
        LoRa.write(buffer, length);         // Use LoRa.write for byte arrays
        int success = LoRa.endPacket(true); // true for synchronous mode
        if (success)
        {
            Serial.println("Packet sent successfully!");
        }
        else
        {
            Serial.println("Failed to send packet.");
        }
    }

    /**
     * @brief Starts continuous receive mode.
     *
     * This function puts the LoRa module in receive mode, listening for incoming packets.
     */
    void startReceiveMode()
    {
        LoRa.receive();
    }

    /**
     * @brief Checks for and reads a packet into a byte buffer.
     * @param buffer The buffer to store the received packet data.
     * @param maxLen The maximum number of bytes to read into the buffer.
     * @return The number of bytes received (packet size), or 0 if no packet was received or buffer is too small.
     */
    int receivePacket(byte *buffer, size_t maxLen)
    {
        int packetSize = LoRa.parsePacket();
        if (packetSize == 0 || packetSize > maxLen)
        {
            // No packet or buffer too small
            // Clear any remaining data if packetSize > maxLen? LoRa lib might handle this.
            // For now, just return 0. If packetSize > maxLen, we might lose data.
            if (packetSize > maxLen)
            {
                Serial.printf("Received packet size (%d) exceeds buffer size (%d). Packet discarded.\n", packetSize, maxLen);
                // Read and discard the packet data to clear the LoRa buffer
                while (LoRa.available())
                {
                    LoRa.read();
                }
            }
            return 0;
        }

        // Read packet data into the buffer
        int bytesRead = 0;
        while (LoRa.available() && bytesRead < packetSize) // Ensure we don't read more than packetSize
        {
            buffer[bytesRead++] = (byte)LoRa.read();
        }

        // Check if we read the expected number of bytes
        if (bytesRead != packetSize)
        {
            Serial.printf("Error reading packet: expected %d bytes, read %d bytes.\n", packetSize, bytesRead);
            // Handle error, maybe return -1 or 0? For now, return bytesRead but log error.
        }

        return bytesRead; // Return the actual number of bytes read
    }

    /**
     * @brief Gets the received signal strength indicator (RSSI) of the last packet.
     * @return The RSSI value.
     */
    int getPacketRssi()
    {
        return LoRa.packetRssi();
    }

    /**
     * @brief Gets the signal-to-noise ratio (SNR) of the last packet.
     * @return The SNR value.
     */
    float getPacketSnr()
    {
        return LoRa.packetSnr();
    }

    /**
     * @brief Gets the RSSI (Received Signal Strength Indicator) of the last received packet.
     * @return RSSI value in dBm.
     */
    int getRssi()
    {
        return LoRa.packetRssi();
    }

    /**
     * @brief Gets the SNR (Signal-to-Noise Ratio) of the last received packet.
     * @return SNR value in dB.
     */
    float getSnr()
    {
        return LoRa.packetSnr();
    }

    /**
     * @brief Returns a string with the current LoRa configuration.
     * @return Configuration string.
     */
    String getConfigurationString() const
    {
        String config = "LoRa Configuration:\n";
        config += "  Frequency: " + String(frequency / 1000000.0, 2) + " MHz\n";
        config += "  Bandwidth: " + String(LORA_BANDWIDTH / 1000.0, 1) + " kHz\n";
        config += "  Spreading Factor: " + String(LORA_SPREADING_FACTOR) + "\n";
        config += "  Coding Rate: 4/" + String(LORA_CODING_RATE) + "\n";
        config += "  TX Power: " + String(LORA_TX_POWER) + " dBm\n";
        config += "  CRC: Enabled\n";
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
};

#endif // LORA_MANAGER_H