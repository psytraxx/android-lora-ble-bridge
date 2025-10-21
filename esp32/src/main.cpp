//! ESP32 Firmware for LoRa-BLE Bridge
//!
//! This firmware implements a BLE peripheral that communicates with Android devices
//! and bridges BLE messages to LoRa transmission and reception.
//!
//! Features:
//! - BLE GATT server with TX/RX characteristics for message exchange
//! - LoRa radio for long-range communication (5-10 km typical)
//! - Message queue for inter-task communication
//! - Advertising as "ESP32-LoRa"

#include <Arduino.h>
#include "lora_config.h"
#include "LoRaManager.h"
#include "BLEManager.h"
#include "Protocol.h"

// --- Pin Definitions for LoRa Module ---
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_SS 5
#define LORA_RST 12
#define LORA_DIO0 32

// Manager objects
LoRaManager loraManager(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS, LORA_RST, LORA_DIO0, LORA_FREQUENCY);
BLEManager bleManager;

// Message queues (simple implementation using arrays)
const int MAX_BLE_TO_LORA_QUEUE = 5;
const int MAX_LORA_TO_BLE_QUEUE = 10;

Message bleToLoraQueue[MAX_BLE_TO_LORA_QUEUE];
int bleToLoraQueueSize = 0;

Message loraTobleQueue[MAX_LORA_TO_BLE_QUEUE];
int loraTobleQueueSize = 0;

/**
 * @brief Setup routine for ESP32 LoRa-BLE Bridge
 */
void setup()
{
    Serial.begin(115200);
    delay(2000);

    Serial.println("===================================");
    Serial.println("ESP32 LoRa-BLE Bridge starting...");
    Serial.println("===================================");

    // Initialize BLE
    if (!bleManager.setup("ESP32-LoRa"))
    {
        Serial.println("BLE setup failed. Halting execution.");
        while (1)
        {
            delay(1000);
        }
    }
    bleManager.startAdvertising();

    // Initialize LoRa
    Serial.println("\nInitializing LoRa radio...");
    Serial.println(loraManager.getConfigurationString());

    if (!loraManager.setup())
    {
        Serial.println("LoRa setup failed. Halting execution.");
        while (1)
        {
            delay(1000);
        }
    }

    // Start continuous receive mode
    loraManager.startReceiveMode();

    Serial.println("\n===================================");
    Serial.println("All systems initialized successfully");
    Serial.println("System running - waiting for connections...");
    Serial.println("===================================\n");
}

/**
 * @brief Main loop - handles BLE<->LoRa message bridging
 */
void loop()
{
    // Process BLE events
    bleManager.process();

    // Check for messages from BLE to send via LoRa
    if (bleManager.hasMessage())
    {
        Message msg = bleManager.getMessage();

        Serial.print("Received message from BLE: type=");
        Serial.println((int)msg.type);

        // Serialize and send via LoRa
        uint8_t buf[64];
        int len = msg.serialize(buf, sizeof(buf));

        if (len > 0)
        {
            Serial.print("Transmitting ");
            Serial.print(len);
            Serial.println(" bytes via LoRa");
            loraManager.sendPacket(buf, len);
            Serial.println("LoRa TX successful");

            // Return to RX mode after transmission
            loraManager.startReceiveMode();
        }
        else
        {
            Serial.println("Failed to serialize message for LoRa TX");
        }
    }

    // Check for messages from LoRa
    uint8_t rxBuffer[64];
    int packetSize = loraManager.receivePacket(rxBuffer, sizeof(rxBuffer));

    if (packetSize > 0)
    {
        int rssi = loraManager.getRssi();
        float snr = loraManager.getSnr();

        Serial.print("LoRa RX: received ");
        Serial.print(packetSize);
        Serial.print(" bytes, RSSI: ");
        Serial.print(rssi);
        Serial.print(" dBm, SNR: ");
        Serial.print(snr);
        Serial.println(" dB");

        // Deserialize message
        Message msg;
        if (msg.deserialize(rxBuffer, packetSize))
        {
            Serial.print("LoRa message deserialized: type=");
            Serial.println((int)msg.type);

            // Handle different message types
            switch (msg.type)
            {
            case MessageType::Text:
            {
                Serial.print("Text message - seq: ");
                Serial.print(msg.textData.seq);
                Serial.print(", text: \"");
                Serial.print(msg.textData.text);
                Serial.println("\"");

                // Send ACK
                Message ack = Message::createAck(msg.textData.seq);
                uint8_t ackBuf[64];
                int ackLen = ack.serialize(ackBuf, sizeof(ackBuf));

                if (ackLen > 0)
                {
                    Serial.print("Sending ACK for seq: ");
                    Serial.println(msg.textData.seq);
                    loraManager.sendPacket(ackBuf, ackLen);
                    loraManager.startReceiveMode();
                    Serial.println("ACK sent successfully");
                }

                // Forward to BLE if connected
                if (bleManager.isConnected())
                {
                    if (bleManager.sendMessage(msg))
                    {
                        Serial.println("Text message forwarded from LoRa to BLE");
                    }
                }
                else
                {
                    Serial.println("BLE not connected - message buffered (not implemented in simple version)");
                }
                break;
            }

            case MessageType::Gps:
            {
                Serial.print("GPS message - seq: ");
                Serial.print(msg.gpsData.seq);
                Serial.print(", lat: ");
                Serial.print(msg.gpsData.lat / 1000000.0, 6);
                Serial.print("°, lon: ");
                Serial.print(msg.gpsData.lon / 1000000.0, 6);
                Serial.println("°");

                // Send ACK
                Message ack = Message::createAck(msg.gpsData.seq);
                uint8_t ackBuf[64];
                int ackLen = ack.serialize(ackBuf, sizeof(ackBuf));

                if (ackLen > 0)
                {
                    Serial.print("Sending ACK for GPS seq: ");
                    Serial.println(msg.gpsData.seq);
                    loraManager.sendPacket(ackBuf, ackLen);
                    loraManager.startReceiveMode();
                    Serial.println("ACK sent successfully");
                }

                // Forward to BLE if connected
                if (bleManager.isConnected())
                {
                    if (bleManager.sendMessage(msg))
                    {
                        Serial.println("GPS message forwarded from LoRa to BLE");
                    }
                }
                else
                {
                    Serial.println("BLE not connected - GPS message buffered (not implemented in simple version)");
                }
                break;
            }

            case MessageType::Ack:
            {
                Serial.print("Received ACK for seq: ");
                Serial.println(msg.ackData.seq);

                // Forward to BLE if connected
                if (bleManager.isConnected())
                {
                    if (bleManager.sendMessage(msg))
                    {
                        Serial.println("ACK forwarded to BLE");
                    }
                }
                break;
            }
            }
        }
        else
        {
            Serial.println("Failed to deserialize LoRa message");
        }
    }

    // Small delay to prevent watchdog issues
    delay(10);
}