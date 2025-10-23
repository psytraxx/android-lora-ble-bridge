//! ESP32 Firmware for LoRa-BLE Bridge (Power-Optimized)
//!
//! This firmware implements a BLE peripheral that communicates with Android devices
//! and bridges BLE messages to LoRa transmission and reception.
//!
//! Features:
//! - BLE GATT server with TX/RX characteristics for message exchange
//! - LoRa radio for long-range communication (5-10 km typical)
//! - Message queue for inter-task communication
//! - Message buffering (up to 10 messages) when BLE disconnected
//! - Light sleep for power optimization
//! - Interrupt-driven LoRa reception (always listening)

#include <Arduino.h>
#include "lora_config.h"
#include "LoRaManager.h"
#include "BLEManager.h"
#include "Protocol.h"
#include "LEDManager.h"
#include "MessageBuffer.h"
#include <freertos/queue.h>
#include <esp_task_wdt.h>
#include <freertos/task.h>
#include <LoRa.h>
#include "Network.h"
#include <esp_wifi.h>

// Manager objects
LoRaManager loraManager(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS, LORA_RST, LORA_DIO0, LORA_FREQUENCY);
#ifdef LED_PIN
LEDManager ledManager(LED_PIN);
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

// Message buffer for when BLE is disconnected (SINGLE GLOBAL INSTANCE)
MessageBuffer messageBuffer;

// Flag for LoRa activity (set in ISR, checked in loop)
volatile bool loraActivity = false;

/**
 * @brief LoRa receive callback - handles incoming LoRa packets event-driven (ISR)
 */
void IRAM_ATTR onLoRaReceive(int packetSize)
{
    if (packetSize == 0)
        return;

    LoRaPacket packet;
    packet.len = LoRa.readBytes(packet.buffer, sizeof(packet.buffer));
    packet.rssi = LoRa.packetRssi();
    packet.snr = LoRa.packetSnr();

    if (packet.len > 0)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(loRaQueue, &packet, &xHigherPriorityTaskWoken);
        loraActivity = true; // Wake up main loop
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
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

    // Disable WiFi and Bluetooth Classic (we only use BLE)
    esp_wifi_stop();
    esp_wifi_deinit();
    btStop();

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

    // Set up event-driven LoRa reception (CRITICAL: Always listening)
    LoRa.onReceive(onLoRaReceive);

    // Start continuous receive mode
    loraManager.startReceiveMode();

    // Initialize LED
#ifdef LED_PIN
    ledManager.setup();
#endif

    Serial.println("\n===================================");
    Serial.println("All systems initialized successfully");
    Serial.println("System running - waiting for connections...");
    Serial.println("===================================\n");
}

/**
 * @brief Handle LoRa to BLE message forwarding and buffering
 */
void handleLoRaToBleForwarding()
{
    // Send buffered messages if BLE just connected
    static bool justConnected = false;
    static unsigned long connectTime = 0;
    if (bleManager->isConnected() && !messageBuffer.isEmpty())
    {
        if (!justConnected)
        {
            // First time BLE is connected since last disconnect
            justConnected = true;
            connectTime = millis();
            Serial.println("BLE connected - waiting before sending buffered messages...");
        }
        // Wait at least 2s after connection before sending buffered messages
        if (millis() - connectTime < 2000)
        {
            return;
        }
        Serial.print("BLE connected - sending ");
        Serial.print(messageBuffer.getCount());
        Serial.println(" buffered messages");

        Message bufferedMsg;
        while (messageBuffer.get(bufferedMsg))
        {
            if (bleManager->sendMessage(bufferedMsg))
            {
                Serial.println("Buffered message sent successfully");
#ifdef LED_PIN
                ledManager.blink();
#endif
                delay(20); // Small delay between messages to avoid overwhelming BLE
            }
            else
            {
                Serial.println("Failed to send buffered message");
                break; // Stop if send fails
            }
        }
    }
    else if (!bleManager->isConnected())
    {
        // Reset justConnected flag when disconnected
        justConnected = false;
    }

    // Process live queue messages
    Message loraMsg;
    if (xQueueReceive(loraToBleQueue, &loraMsg, 0) == pdTRUE)
    {
        if (bleManager->isConnected())
        {
            if (bleManager->sendMessage(loraMsg))
            {
                Serial.println("Message forwarded from LoRa to BLE");
#ifdef LED_PIN
                ledManager.blink();
#endif
            }
        }
        else
        {
            // Buffer message for later delivery
            messageBuffer.add(loraMsg);
            Serial.print("Buffered message (total: ");
            Serial.print(messageBuffer.getCount());
            Serial.println(")");
        }
    }
}

/**
 * @brief Process received LoRa packet
 */
void processLoRaPacket(const LoRaPacket &packet)
{
    bleManager->updateActivity();

    // If not connected, start advertising to allow Android to reconnect
    if (!bleManager->isConnected())
    {
        Serial.println("LoRa message received but no BLE connection - starting advertising");
        bleManager->startAdvertising();
    }

    Serial.print("LoRa RX: ");
    Serial.print(packet.len);
    Serial.print(" bytes, RSSI: ");
    Serial.print(packet.rssi);
    Serial.print(" dBm, SNR: ");
    Serial.print(packet.snr);
    Serial.println(" dB");

    // Deserialize message
    Message msg;
    if (!msg.deserialize(packet.buffer, packet.len))
    {
        Serial.println("Failed to deserialize LoRa message");
        return;
    }

    Serial.print("Deserialized: type=");
    Serial.println((int)msg.type);

    // Handle message types
    switch (msg.type)
    {
    case MessageType::Text:
    {
        Serial.print("Text - seq: ");
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

            if (loraManager.sendPacket(ackBuf, ackLen))
            {
                Serial.println("ACK sent successfully");
            }
            else
            {
                Serial.println("ACK send failed");
            }
            loraManager.startReceiveMode();
        }

        // Queue or buffer message for BLE delivery
        if (bleManager->isConnected())
        {
            if (xQueueSend(loraToBleQueue, &msg, 0) != pdTRUE)
            {
                Serial.println("Warning: LoRa to BLE queue full, buffering");
                messageBuffer.add(msg);
            }
        }
        else
        {
            messageBuffer.add(msg);
            Serial.print("Buffered text message (total: ");
            Serial.print(messageBuffer.getCount());
            Serial.println(")");
        }

#ifdef LED_PIN
        ledManager.blink();
#endif
        break;
    }

    case MessageType::Ack:
    {
        Serial.print("ACK - seq: ");
        Serial.println(msg.ackData.seq);

        // Queue or buffer ACK for BLE delivery
        if (bleManager->isConnected())
        {
            if (xQueueSend(loraToBleQueue, &msg, 0) != pdTRUE)
            {
                Serial.println("Warning: LoRa to BLE queue full, buffering");
                messageBuffer.add(msg);
            }
        }
        else
        {
            messageBuffer.add(msg);
            Serial.print("Buffered ACK (total: ");
            Serial.print(messageBuffer.getCount());
            Serial.println(")");
        }

#ifdef LED_PIN
        ledManager.blink();
#endif
        break;
    }
    }
}

/**
 * @brief Main loop - handles BLE<->LoRa message bridging with light sleep for power savings
 */
void loop()
{
    // Reset watchdog
    esp_task_wdt_reset();

    // Process BLE events (non-blocking)
    bleManager->process();

    // Check for messages from BLE to send via LoRa
    Message bleMsg;
    if (xQueueReceive(bleToLoraQueue, &bleMsg, 0) == pdTRUE)
    {
        Serial.print("Received from BLE queue: type=");
        Serial.println((int)bleMsg.type);

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
                delay(100);
                sendSuccess = loraManager.sendPacket(buf, len);
            }

            if (sendSuccess)
            {
                Serial.println("LoRa TX successful");
#ifdef LED_PIN
                ledManager.blink(2);
#endif
            }
            else
            {
                Serial.println("LoRa TX failed permanently");
            }

            // Return to RX mode (CRITICAL: Always listening)
            loraManager.startReceiveMode();
            delay(50);
        }
        else
        {
            Serial.println("Failed to serialize message for LoRa TX");
        }
    }

    // Check for LoRa packets (event-driven via ISR callback)
    LoRaPacket packet;
    if (xQueueReceive(loRaQueue, &packet, 0) == pdTRUE)
    {
        processLoRaPacket(packet);
        loraActivity = false;
    }

    // Forward queued/buffered messages from LoRa to BLE
    handleLoRaToBleForwarding();

    // Adaptive delay for power savings (longer delay when idle, shorter when active)
    // Note: Can't use light sleep as it would prevent BLE from waking the device
    bool hasActivity = uxQueueMessagesWaiting(bleToLoraQueue) > 0 ||
                       uxQueueMessagesWaiting(loRaQueue) > 0 ||
                       loraActivity;

    if (hasActivity)
    {
        // Activity detected - short delay for responsiveness
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    else
    {
        // Idle - longer delay to reduce power consumption
        // BLE and LoRa interrupts will still wake the CPU
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
