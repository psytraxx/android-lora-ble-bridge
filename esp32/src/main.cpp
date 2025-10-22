//! ESP32 Firmware for LoRa-BLE Bridge
//!
//! This firmware implements a BLE peripheral that communicates with Android devices
//! and bridges BLE messages to LoRa transmission and reception.
//!
//! Features:
//! - BLE GATT server with TX/RX characteristics for message exchange
//! - LoRa radio for long-range communication (5-10 km typical)
//! - Message queue for inter-task communication
//! - Advertising as "ESP32S3-LoRa"

#include <Arduino.h>
#include "lora_config.h"
#include "LoRaManager.h"
#include "BLEManager.h"
#include "Protocol.h"
#include "LEDManager.h"
#ifdef ENABLE_BLE_WAKEUP
#include "SleepManager.h"
#endif
#include <freertos/queue.h>
#include <esp_task_wdt.h>
#include <freertos/task.h>
#include <LoRa.h>

// Manager objects
LoRaManager loraManager(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS, LORA_RST, LORA_DIO0, LORA_FREQUENCY);
#ifdef LED_PIN
LEDManager ledManager(LED_PIN);
#endif
#ifdef ENABLE_BLE_WAKEUP
SleepManager sleepManager(LORA_DIO0); // Initialize sleep manager with LoRa interrupt pin
#endif

// Message queues using FreeRTOS
const int BLE_TO_LORA_QUEUE_SIZE = 10;
const int LORA_TO_BLE_QUEUE_SIZE = 15;

QueueHandle_t bleToLoraQueue;
QueueHandle_t loraToBleQueue;

// Struct for LoRa packets with metadata
struct LoRaPacket
{
    uint8_t buffer[256];
    int len;
    int rssi;
    float snr;
};

QueueHandle_t loRaQueue;

// BLEManager declared after queues
BLEManager *bleManager;

/**
 * @brief Helper function to update sleep activity timer from any context
 */
void updateSleepActivity()
{
#ifdef ENABLE_BLE_WAKEUP
    sleepManager.updateActivity();
#endif
}

/**
 * @brief LoRa receive callback - handles incoming LoRa packets event-driven
 */
void onLoRaReceive(int packetSize)
{
    if (packetSize == 0)
        return;

    LoRaPacket packet;
    packet.len = LoRa.readBytes(packet.buffer, sizeof(packet.buffer));
    packet.rssi = LoRa.packetRssi();
    packet.snr = LoRa.packetSnr();

    if (packet.len > 0)
    {
        xQueueSend(loRaQueue, &packet, 0);
    }
}

/**
 * @brief Setup routine for ESP32 LoRa-BLE Bridge
 */
void setup()
{
    Serial.begin(115200);
    delay(2000);

// Set CPU frequency for power savings (configurable via build flag)
#ifndef CPU_FREQ_MHZ
#define CPU_FREQ_MHZ 160
#endif
    setCpuFrequencyMhz(CPU_FREQ_MHZ);
    Serial.print("CPU Frequency set to: ");
    Serial.print(getCpuFrequencyMhz());
    Serial.println(" MHz");

    // Initialize watchdog for robustness (30 second timeout)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 30000, // 30 seconds
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());

    Serial.println("===================================");
    Serial.println("ESP32 LoRa-BLE Bridge starting...");
    Serial.println("===================================");

    // Create message queues
    bleToLoraQueue = xQueueCreate(BLE_TO_LORA_QUEUE_SIZE, sizeof(Message));
    loraToBleQueue = xQueueCreate(LORA_TO_BLE_QUEUE_SIZE, sizeof(Message));
    loRaQueue = xQueueCreate(15, sizeof(LoRaPacket));

    if (bleToLoraQueue == nullptr || loraToBleQueue == nullptr || loRaQueue == nullptr)
    {
        Serial.println("Failed to create message queues. Halting execution.");
        while (1)
        {
            delay(1000);
        }
    }

    // Initialize BLE with queue
    bleManager = new BLEManager(bleToLoraQueue);

    // Set activity callback for sleep management
    bleManager->setActivityCallback(updateSleepActivity);

    // Initialize BLE with retry logic
    const int BLE_RETRY_COUNT = 3;
    int bleRetries = BLE_RETRY_COUNT;
    bool bleSuccess = false;

    while (bleRetries > 0 && !bleSuccess)
    {
        Serial.print("BLE setup attempt ");
        Serial.print(BLE_RETRY_COUNT - bleRetries + 1);
        Serial.print("/");
        Serial.println(BLE_RETRY_COUNT);

        if (bleManager->setup(DEVICE_NAME))
        {
            bleSuccess = true;
            Serial.println("BLE setup successful");
        }
        else
        {
            Serial.println("BLE setup failed");
            if (bleRetries > 1)
            {
                Serial.println("Retrying in 2 seconds...");
                delay(2000);
            }
            bleRetries--;
        }
    }

    if (!bleSuccess)
    {
        Serial.println("BLE setup failed permanently. Halting execution.");
        while (1)
        {
            delay(1000);
        }
    }

    bleManager->startAdvertising();

    // Initialize LoRa
    Serial.println("\nInitializing LoRa radio...");
    Serial.println(loraManager.getConfigurationString());

    const int LORA_RETRY_COUNT = 3;
    int loraRetries = LORA_RETRY_COUNT;
    bool loraSuccess = false;

    while (loraRetries > 0 && !loraSuccess)
    {
        Serial.print("LoRa setup attempt ");
        Serial.print(LORA_RETRY_COUNT - loraRetries + 1);
        Serial.print("/");
        Serial.println(LORA_RETRY_COUNT);

        if (loraManager.setup())
        {
            loraSuccess = true;
            Serial.println("LoRa setup successful");
        }
        else
        {
            Serial.println("LoRa setup failed");
            if (loraRetries > 1)
            {
                Serial.println("Retrying in 1 second...");
                delay(1000);
            }
            loraRetries--;
        }
    }

    if (!loraSuccess)
    {
        Serial.println("LoRa setup failed permanently. Halting execution.");
        while (1)
        {
            delay(1000);
        }
    }

    // Set up event-driven LoRa reception
    LoRa.onReceive(onLoRaReceive);

    // Start continuous receive mode
    loraManager.startReceiveMode();

    // Initialize LED
#ifdef LED_PIN
    ledManager.setup();
#endif

#ifdef ENABLE_BLE_WAKEUP
    // Initialize Sleep Manager
    sleepManager.setup();

    // Check if we woke from light sleep and deliver any stored messages
    if (sleepManager.wasWokenFromSleep())
    {
        Serial.print("Woke from light sleep - ");
        Serial.print(sleepManager.getStoredMessageCount());
        Serial.println(" messages in RTC memory");
        Serial.println(sleepManager.getWakeupReason());

#ifdef LED_PIN
        // Visual confirmation: 3 LED blinks on wake-up
        ledManager.blink(3, 150, 100);
#endif
    }
#endif

    Serial.println("\n===================================");
    Serial.println("All systems initialized successfully");
    Serial.println("System running - waiting for connections...");
    Serial.println("===================================\n");
}

/**
 * @brief Main loop - handles BLE<->LoRa message bridging and light sleep management
 */
void loop()
{
    // Process BLE events
    bleManager->process();

#ifdef ENABLE_BLE_WAKEUP
    // Deliver stored messages from RTC memory when BLE is connected
    if (bleManager->isConnected() && sleepManager.getStoredMessageCount() > 0)
    {
        Message storedMsg;
        if (sleepManager.retrieveMessage(storedMsg))
        {
            if (bleManager->sendMessage(storedMsg))
            {
                Serial.println("Delivered stored message from RTC memory to BLE");
                sleepManager.updateActivity(); // Activity: message delivered
            }
            else
            {
                // Failed to send, put it back
                sleepManager.storeMessage(storedMsg);
            }
        }
    }
#endif

    // Check for messages from BLE to send via LoRa
    Message bleMsg;
    if (xQueueReceive(bleToLoraQueue, &bleMsg, 0) == pdTRUE)
    {
        Serial.print("Received message from BLE queue: type=");
        Serial.println((int)bleMsg.type);

#ifdef ENABLE_BLE_WAKEUP
        sleepManager.updateActivity(); // Activity: BLE message received
#endif

        // Serialize and send via LoRa
        uint8_t buf[64];
        int len = bleMsg.serialize(buf, sizeof(buf));

        if (len > 0)
        {
            Serial.print("Transmitting ");
            Serial.print(len);
            Serial.println(" bytes via LoRa");

            bool sendSuccess = loraManager.sendPacket(buf, len);

            if (!sendSuccess)
            {
                Serial.println("LoRa TX failed, retrying once...");
                delay(100); // Brief delay before retry
                sendSuccess = loraManager.sendPacket(buf, len);
            }

            if (sendSuccess)
            {
                Serial.println("LoRa TX successful");
                // Blink twice for outgoing message
#ifdef LED_PIN
                ledManager.blink(2);
#endif
#ifdef ENABLE_BLE_WAKEUP
                sleepManager.updateActivity(); // Activity: LoRa transmission
#endif
            }
            else
            {
                Serial.println("LoRa TX failed permanently");
            }

            // Return to RX mode after transmission (always, even on failure)
            loraManager.startReceiveMode();
            delay(50); // Small delay to ensure radio is fully in RX mode
        }
        else
        {
            Serial.println("Failed to serialize message for LoRa TX");
        }
    }

    // Check for messages from LoRa (event-driven via callback)
    LoRaPacket packet;
    if (xQueueReceive(loRaQueue, &packet, 0) == pdTRUE)
    {
        Serial.print("LoRa RX: received ");
        Serial.print(packet.len);
        Serial.print(" bytes, RSSI: ");
        Serial.print(packet.rssi);
        Serial.print(" dBm, SNR: ");
        Serial.print(packet.snr);
        Serial.println(" dB");

#ifdef ENABLE_BLE_WAKEUP
        sleepManager.updateActivity(); // Activity: LoRa packet received
#endif

        // Deserialize message
        Message msg;
        if (msg.deserialize(packet.buffer, packet.len))
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
                Serial.print("\"");

                if (msg.textData.hasGps)
                {
                    Serial.print(", GPS: ");
                    Serial.print(msg.textData.lat / 1000000.0, 6);
                    Serial.print("°, ");
                    Serial.print(msg.textData.lon / 1000000.0, 6);
                    Serial.print("°");
                }
                Serial.println();

                // Send ACK
                Message ack = Message::createAck(msg.textData.seq);
                uint8_t ackBuf[64];
                int ackLen = ack.serialize(ackBuf, sizeof(ackBuf));

                if (ackLen > 0)
                {
                    Serial.print("Sending ACK for seq: ");
                    Serial.println(msg.textData.seq);
                    bool ackSent = loraManager.sendPacket(ackBuf, ackLen);
                    if (ackSent)
                    {
                        Serial.println("ACK sent successfully");
                    }
                    else
                    {
                        Serial.println("ACK send failed");
                    }
                    loraManager.startReceiveMode();
                }

                // Try to send via BLE if connected, otherwise store in RTC memory
                if (bleManager->isConnected())
                {
                    if (xQueueSend(loraToBleQueue, &msg, 0) != pdTRUE)
                    {
                        Serial.println("Warning: LoRa to BLE queue full, storing in RTC memory");
#ifdef ENABLE_BLE_WAKEUP
                        sleepManager.storeMessage(msg);
#endif
                    }
                    else
                    {
#ifdef LED_PIN
                        // Blink once for incoming message (reduced frequency for power savings)
                        ledManager.blink();
#endif
                    }
                }
                else
                {
                    // No BLE connection - store message for later delivery
                    Serial.println("No BLE connection - storing message in RTC memory");
#ifdef ENABLE_BLE_WAKEUP
                    sleepManager.storeMessage(msg);
#endif
#ifdef LED_PIN
                    ledManager.blink();
#endif
                }
                break;
            }

            case MessageType::Ack:
            {
                Serial.print("Received ACK for seq: ");
                Serial.println(msg.ackData.seq);

                // Try to send via BLE if connected, otherwise store in RTC memory
                if (bleManager->isConnected())
                {
                    if (xQueueSend(loraToBleQueue, &msg, 0) != pdTRUE)
                    {
                        Serial.println("Warning: LoRa to BLE queue full, storing in RTC memory");
#ifdef ENABLE_BLE_WAKEUP
                        sleepManager.storeMessage(msg);
#endif
                    }
                    else
                    {
#ifdef LED_PIN
                        // Blink once for incoming message (reduced frequency for power savings)
                        ledManager.blink();
#endif
                    }
                }
                else
                {
                    // No BLE connection - store message for later delivery
                    Serial.println("No BLE connection - storing message in RTC memory");
#ifdef ENABLE_BLE_WAKEUP
                    sleepManager.storeMessage(msg);
#endif
#ifdef LED_PIN
                    ledManager.blink();
#endif
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

    // Forward queued messages from LoRa to BLE if connected
    Message loraMsg;
    if (bleManager->isConnected() && xQueueReceive(loraToBleQueue, &loraMsg, 0) == pdTRUE)
    {
        if (bleManager->sendMessage(loraMsg))
        {
            Serial.println("Message forwarded from LoRa to BLE");
#ifdef ENABLE_BLE_WAKEUP
            sleepManager.updateActivity(); // Activity: message forwarded to BLE
#endif
        }
    }

#ifdef ENABLE_BLE_WAKEUP
    // Check if we should enter sleep (2 minutes of inactivity)
    if (sleepManager.shouldEnterLightSleep())
    {
        Serial.println("\n>>> 2 minutes of inactivity detected <<<");

        // Store any pending messages from the LoRa-to-BLE queue
        while (xQueueReceive(loraToBleQueue, &loraMsg, 0) == pdTRUE)
        {
            Serial.println("Storing pending message before sleep");
            sleepManager.storeMessage(loraMsg);
        }

        sleepManager.enterLightSleep();
        // Execution continues here after wake-up from sleep
        // Will process normally in next loop iteration
    }
#endif

    // Small delay to prevent watchdog issues and allow task switching
    vTaskDelay(pdMS_TO_TICKS(10));

    // Reset watchdog to prevent timeout
    esp_task_wdt_reset();
}